#include "../../utils/RuntimeProfiler.h"
/**
 * @file MinCostBipartiteMatcher.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "MinCostBipartiteMatcher.h"
#include "SparseMinCostMatching.h"
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <omp.h>

void MinCostBipartiteMatcher::solve()
{
    AMF_PROFILE_FUNCTION("bipartite_matching");
    // Optional diagnostic snapshots are replayable without loading a device or
    // netlist. They are disabled in normal runs and never change candidate edges.
    if (const char *directory = std::getenv("AMF_MATCHER_DUMP_DIR"))
    {
        const char *threshold = std::getenv("AMF_MATCHER_DUMP_MIN_LEFT");
        const char *limit = std::getenv("AMF_MATCHER_DUMP_MAX_FILES");
        static std::atomic<int> dumped{0};
        if (numLeftNodes >= (threshold ? std::stoi(threshold) : 10000))
        {
            const int id = dumped.fetch_add(1);
            if (id < (limit ? std::stoi(limit) : 3))
            {
                AMF_PROFILE_SCOPE("diagnostics", "matching_graph_snapshot");
                std::ofstream output(std::string(directory) + "/matching-" + std::to_string(id) + ".txt");
                if (!output) throw std::runtime_error("Cannot create matching graph snapshot");
                output << "AMF_MATCHING_V1 " << numLeftNodes << ' ' << numRightNodes << ' ' << numExpectedMatches << '\n';
                output << std::setprecision(std::numeric_limits<float>::max_digits10);
                for (int left = 0; left < numLeftNodes; ++left)
                    for (const auto &edge : adjList[left]) output << left << ' ' << edge.first << ' ' << edge.second << '\n';
            }
        }
    }
    const auto started = amf_matching::Clock::now();
    if (backend != "legacy")
    {
        auto result = amf_matching::solve(adjList, numRightNodes, numExpectedMatches, maxThreadNum,
                                          backend == "component_ssp", backend == "component_assignment", forwardBias);
        left2right = std::move(result.leftToRight);
        std::fill(right2left.begin(), right2left.end(), -1);
        for (int left = 0; left < numLeftNodes; ++left)
            if (left2right[left] >= 0) right2left[left2right[left]] = left;
        std::ostringstream summary;
        summary << "AMF_MATCHER backend=" << backend << " left=" << numLeftNodes << " right=" << numRightNodes
                << " edges=" << result.edges << " components=" << result.components
                << " largest_left=" << result.largestLeft << " largest_edges=" << result.largestEdges
                << " matched=" << result.cardinality << " forward_bias=" << forwardBias
                << " cost=" << std::setprecision(12) << result.cost
                << " prepare_s=" << result.preparationSeconds << " kernel_s=" << result.solveSeconds
                << " longest_task_s=" << result.longestTaskSeconds << " solve_s=" << amf_matching::seconds(started);
        print_info(summary.str());
        return;
    }
    if (numExpectedMatches == 0) return;
    if(numLeftNodes>=10000) {
        std::vector<int> parent(numLeftNodes+numRightNodes),ls(parent.size(),0),rs(parent.size(),0);
        for(size_t k=0;k<parent.size();++k)parent[k]=k;
        auto root=[&](int x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
        size_t edges=0;
        for(int u=0;u<numLeftNodes;++u)for(const auto &e:adjList[u]) {
            ++edges;int a=root(u),b=root(numLeftNodes+e.first);if(a!=b)parent[b]=a;
        }
        for(int u=0;u<numLeftNodes;++u)++ls[root(u)];
        for(int v=0;v<numRightNodes;++v)++rs[root(numLeftNodes+v)];
        int components=0,largestL=0,largestR=0,deficit=0;
        for(size_t k=0;k<parent.size();++k)if(ls[k]) {
          ++components;if(ls[k]>largestL){largestL=ls[k];largestR=rs[k];}
          deficit+=std::max(0,ls[k]-rs[k]);
        }
        print_info("AMF_DIAG_GRAPH left="+std::to_string(numLeftNodes)+" right="+std::to_string(numRightNodes)+
          " edges="+std::to_string(edges)+" components="+std::to_string(components)+
          " largest_left="+std::to_string(largestL)+" largest_right="+std::to_string(largestR)+
          " component_deficit="+std::to_string(deficit));
    }
    int numSolvers = minCostFlowSolvers.size();
    std::vector<double> workerSeconds(numSolvers);
#pragma omp parallel for schedule(dynamic)
    for (int solverId = 0; solverId < numSolvers; solverId++)
    {
        const auto workerStart=amf_matching::Clock::now();
        minCostFlowSolvers[solverId]->calcMinCostFlow(srcNode, sinkNode, numExpectedMatches);
        workerSeconds[solverId]=amf_matching::seconds(workerStart);
        for (int i = 0; i < numLeftNodes; i++)
        {
            for (unsigned int j = 0; j < minCostFlowSolvers[solverId]->resGraph.adj[i].size(); j++)
            {
                int destination = minCostFlowSolvers[solverId]->resGraph.adj[i][j]->destination;
                if (destination >= numLeftNodes && destination < numLeftNodes + numRightNodes)
                {
                    if (minCostFlowSolvers[solverId]->resGraph.adj[i][j]->residualFlow == 0)
                    {
                        assert(left2right[i] < 0);
                        left2right[i] = destination - numLeftNodes;
                        assert(right2left[destination - numLeftNodes] < 0);
                        right2left[destination - numLeftNodes] = i;
                    }
                }
            }
        }
    }
    if(std::getenv("AMF_MATCHER_WORKER_TRACE")) for(int i=0;i<numSolvers;++i) {
        const auto *solver=minCostFlowSolvers[i];
        std::ostringstream line;
        line<<std::setprecision(12)<<"AMF_MATCHER_WORKER left="<<numLeftNodes<<" right="<<numRightNodes
            <<" worker="<<i<<" seconds="<<workerSeconds[i]<<" visits="<<solver->diagVisits
            <<" relax="<<solver->diagRelax<<" spfa="<<solver->diagSPFA<<" augment="<<solver->diagAugment;
        print_info(line.str());
    }
    if(numLeftNodes>=10000) {
        unsigned long long visits=0,relax=0,spfa=0,augment=0,maxVisits=0;
        for(auto solver:minCostFlowSolvers){visits+=solver->diagVisits;relax+=solver->diagRelax;
          spfa+=solver->diagSPFA;augment+=solver->diagAugment;maxVisits=std::max(maxVisits,solver->diagVisits);}
        print_info("AMF_DIAG_SEARCH left="+std::to_string(numLeftNodes)+" visits="+std::to_string(visits)+
          " relax="+std::to_string(relax)+" spfa="+std::to_string(spfa)+" augment="+std::to_string(augment)+
          " max_solver_visits="+std::to_string(maxVisits));
    }
    print_info("AMF_MATCHER backend=legacy left=" + std::to_string(numLeftNodes) +
               " right=" + std::to_string(numRightNodes) + " solve_s=" + std::to_string(amf_matching::seconds(started)));
}

void MinCostBipartiteMatcher::getConnectedSubgraphAdjList(std::vector<std::vector<std::pair<int, float>>> &adjList,
                                                          std::vector<int> &leftId2ConnectedSubgraphId,
                                                          int &numConnectedSubgraphs, int maxThreadNum)
{
    AMF_PROFILE_FUNCTION("bipartite_matching");
    std::vector<std::vector<int>> inv_adjList;
    inv_adjList.resize(numRightNodes, std::vector<int>());
    for (int i = 0; i < numLeftNodes; i++)
    {
        for (unsigned int j = 0; j < adjList[i].size(); j++)
        {
            unsigned int v = adjList[i][j].first;
            assert(v < inv_adjList.size());
            inv_adjList[v].push_back(i);
        }
    }

    numConnectedSubgraphs = 0;
    for (unsigned int leftNodeId = 0; leftNodeId < adjList.size(); leftNodeId++)
    {
        if (leftId2ConnectedSubgraphId[leftNodeId] >= 0)
            continue;
        int curNode = leftNodeId;

        bool reachedRight[numRightNodes];
        memset(reachedRight, 0, sizeof(reachedRight));
        std::queue<int> leftNodeInQ;
        leftNodeInQ.push(curNode);
        leftId2ConnectedSubgraphId[curNode] = numConnectedSubgraphs % maxThreadNum;
        while (leftNodeInQ.size())
        {
            curNode = leftNodeInQ.front();
            leftNodeInQ.pop();
            for (auto tmpPair : adjList[curNode])
            {
                int rightNode = tmpPair.first;
                if (reachedRight[rightNode])
                    continue;
                reachedRight[rightNode] = 1;
                for (int nextLeftId : inv_adjList[rightNode])
                {
                    if (leftId2ConnectedSubgraphId[nextLeftId] < 0)
                    {
                        leftId2ConnectedSubgraphId[nextLeftId] = numConnectedSubgraphs % maxThreadNum;
                        leftNodeInQ.push(nextLeftId);
                    }
                }
            }
        }

        numConnectedSubgraphs++;
    }

    // print_info("MinCostBipartiteMatcher finds " + std::to_string(numConnectedSubgraphs) +
    //            " connected subgraphs in the input bipartie graph");
}
