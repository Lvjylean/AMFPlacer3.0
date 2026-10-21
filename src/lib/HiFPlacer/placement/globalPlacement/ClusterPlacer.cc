#include "../../../utils/RuntimeProfiler.h"
/**
 * @file ClusterPlacer.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief  This implementation file contains APIs' implementation of the ClusterPlacer which cluster nodes in the given
 * netlist and place the clusters on the device based on simulated-annealing as initial placement.
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "ClusterPlacer.h"
#include "ExternalFloorplan.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>

ClusterPlacer::ClusterPlacer(PlacementInfo *placementInfo, std::map<std::string, std::string> &JSONCfg,
                             float connectionToFixedFactor)
    : placementInfo(placementInfo), JSONCfg(JSONCfg), connectionToFixedFactor(connectionToFixedFactor)
{
    AMF_PROFILE_FUNCTION("clustering");
    if (JSONCfg.find("ClusterPlacerVerbose") != JSONCfg.end())
        verbose = JSONCfg["ClusterPlacerVerbose"] == "true";
    if (JSONCfg.find("y2xRatio") != JSONCfg.end())
        y2xRatio = std::stof(JSONCfg["y2xRatio"]);

    if (JSONCfg.find("jobs") != JSONCfg.end())
    {
        jobs = std::stoul(JSONCfg["jobs"]);
    }
    else
    {
        jobs = 1;
    }

    dfxPartitionFloorplan=placementInfo->getHardRegions().partitionFloorplan;
    clockRegionXNum = std::stoi(JSONCfg["clockRegionXNum"]);
    clockRegionYNum = std::stoi(JSONCfg["clockRegionYNum"]);

    if (JSONCfg.find("RandomInitialPlacement") != JSONCfg.end())
    {
        randomInitialPlacement = JSONCfg["RandomInitialPlacement"] == "true";
    }
    if (JSONCfg.find("noPathLenCluster") != JSONCfg.end())
    {
        noPathLenCluster = JSONCfg["noPathLenCluster"] == "true";
    }
}

void ClusterPlacer::ClusterPlacement()
{
    AMF_PROFILE_FUNCTION("clustering");
    print_status("Cluster Placement Start.");
    if(dfxPartitionFloorplan && (randomInitialPlacement||ExternalFloorplan::enabled(JSONCfg)))
        throw std::runtime_error("DFX partition floorplan cannot be bypassed by random/external initialization");

    if (ExternalFloorplan::enabled(JSONCfg))
    {
        ExternalFloorplan::initialize(placementInfo, JSONCfg);
    }
    else if (!randomInitialPlacement)
    {
        clusterPlacementUnits();
        placeClusters();

        if (JSONCfg.find("drawClusters") != JSONCfg.end())
        {
            if (JSONCfg["drawClusters"] == "true")
                drawClusters();
        }
        if (JSONCfg.find("Dump Cluster file") != JSONCfg.end())
        {
            dumpClusters();
        }
    }
    else
    {
        srandom(20213654);
        for (auto curPU : placementInfo->getPlacementUnits())
        {
            if (!curPU->isFixed())
            {
                float fX = random_float(placementInfo->getGlobalMinX() + 0.1, placementInfo->getGlobalMaxX() - 0.1);
                float fY = random_float(placementInfo->getGlobalMinY() + 0.1, placementInfo->getGlobalMaxY() - 0.1);
                placementInfo->legalizeXYInArea(curPU, fX, fY);
                curPU->setAnchorLocation(fX, fY);
            }
        }
    }

    placementInfo->updateB2BAndGetTotalHPWL();
    placementInfo->checkClockUtilization(true);

    print_status("Cluster Placement Done.");
}

void ClusterPlacer::clusterPlacementUnits()
{
    AMF_PROFILE_FUNCTION("clustering");
    print_status("Clustering Start.");

    hypergraphPartitioning();
    placementInfo->setClusterNum(clusters.size());
    // refineClustersWithPredefinedClusters();

    setClusterNetsAdjMat();

    print_status("Placement Unit Clustering Done.");
}

void ClusterPlacer::hypergraphPartitioning()
{
    AMF_PROFILE_FUNCTION("clustering");
    int minClusterCellNum =
        std::min(unsigned(placementInfo->getNumCells() * 0.25),
                 unsigned(avgClusterSizeRequirement)); // 22000 is the rough number of cells in a clock region, Please
                                                       // note that minCellClusterSize is not the number of
                                                       // placementunits but the number of cells

    int eachClusterDSPNum = 96;
    int eachClusterBRAMNum = 24;
    int gridH = std::stoi(JSONCfg["clockRegionYNum"]);
    int gridW = std::stoi(JSONCfg["clockRegionXNum"]);

    if (JSONCfg.find("clockRegionDSPNum") != JSONCfg.end())
    {
        eachClusterDSPNum = std::stoi(JSONCfg["clockRegionDSPNum"]);
    }
    if (JSONCfg.find("clockRegionBRAMNum") != JSONCfg.end())
    {
        eachClusterBRAMNum = std::stoi(JSONCfg["clockRegionBRAMNum"]);
    }

    if(dfxPartitionFloorplan) {
        size_t staticCells=0;
        for(auto cell:placementInfo->getCells())
            if(!cell->isVirtualCell()&&placementInfo->hardRegionForCell(cell)==0)++staticCells;
        minClusterCellNum=std::max(1,std::min(int(staticCells*.25),avgClusterSizeRequirement));
    }
    // FFPartitioning();

    clockBasedPartitioning(minClusterCellNum, eachClusterDSPNum, eachClusterBRAMNum);
    const size_t partitionedCount=dfxPartitionFloorplan?
        std::count(clusterRegions.begin(),clusterRegions.end(),0):clusters.size();
    if (isClustersToLarges() && partitionedCount < 0.6 * gridW * gridH)
    {
        maxMinCutRate = 0.1;
        clockBasedPartitioning(minClusterCellNum, eachClusterDSPNum, eachClusterBRAMNum);
        isClustersToLarges();
    }
}

bool ClusterPlacer::hasTimingEndpoint(DesignInfo::DesignCell *cell)
{
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    return timingNodes[cell->getCellId()]->checkIsRegister();
}

void ClusterPlacer::FFPartitioning()
{
    std::set<DesignInfo::DesignCell *> coloredCells;
    std::vector<std::vector<DesignInfo::DesignCell *>> FFBased_Clusters;
    for (auto tmpCell : placementInfo->getCells())
    {
        if (!hasTimingEndpoint(tmpCell) && coloredCells.find(tmpCell) == coloredCells.end())
        {
            std::vector<DesignInfo::DesignCell *> curCluster;
            std::queue<DesignInfo::DesignCell *> nodeQ;
            curCluster.clear();
            curCluster.push_back(tmpCell);
            coloredCells.insert(tmpCell);
            nodeQ.push(tmpCell);

            while (nodeQ.size())
            {
                auto curCell = nodeQ.front();
                nodeQ.pop();

                for (auto curNet : (curCell->getInputNets()))
                {
                    if (curNet->checkIsGlobalClock() || curNet->checkIsPowerNet())
                        continue;
                    for (auto driverPin : curNet->getDriverPins())
                    {
                        if (driverPin)
                        {
                            if (auto nextCell = driverPin->getCell())
                            {
                                if (coloredCells.find(nextCell) == coloredCells.end() && !hasTimingEndpoint(nextCell))
                                {
                                    curCluster.push_back(nextCell);
                                    coloredCells.insert(nextCell);
                                    nodeQ.push(nextCell);
                                }
                            }
                        }
                    }
                }

                for (auto curNet : curCell->getOutputNets())
                {
                    if (curNet->checkIsGlobalClock() || curNet->checkIsPowerNet())
                        continue;
                    for (auto pinBeDriven : curNet->getPinsBeDriven())
                    {
                        if (pinBeDriven)
                        {
                            if (auto nextCell = pinBeDriven->getCell())
                            {
                                if (coloredCells.find(nextCell) == coloredCells.end())
                                {
                                    curCluster.push_back(nextCell);
                                    coloredCells.insert(nextCell);
                                    if (!hasTimingEndpoint(nextCell))
                                        nodeQ.push(nextCell);
                                }
                            }
                        }
                    }
                }
            }

            if (curCluster.size() > 1000)
                FFBased_Clusters.push_back(curCluster);
        }
    }

    print_info("#PU=" + std::to_string(placementInfo->getPlacementUnits().size()) +
               " #coloredPU=" + std::to_string(coloredCells.size()));
    for (auto &tmpCluster : FFBased_Clusters)
    {
        std::cout << tmpCluster.size() << "\n";
    }

    std::string dumpClustersFile = "testCluster";
    print_status("dumping cluster information to " + dumpClustersFile);
    std::ofstream outfile0((dumpClustersFile + ".tcl").c_str());
    assert(outfile0.is_open() && outfile0.good() &&
           "The path for cluster result dumping does not exist and please check your path settings");
    for (unsigned int cluster_id = 0; cluster_id < FFBased_Clusters.size(); cluster_id++)
    {
        outfile0 << "highlight -color_index " << (cluster_id) % 20 + 1 << "  [get_cells {";
        for (auto tmpCell : FFBased_Clusters[cluster_id])
        {
            outfile0 << tmpCell->getName() << " ";
        }
        outfile0 << "}]\n";
    }
    outfile0.close();
}

void ClusterPlacer::createLongPathClusterUnits()
{
    AMF_PROFILE_FUNCTION("clustering");
    if (noPathLenCluster)
        return;
    // handle the long paths
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    // auto simpleTimingGraph = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph();
    int pathLengthThr = placementInfo->getLongPathThresholdLevel();
    std::set<int> extractedCellIds;
    extractedCellIds.clear();

    print_status("ClusterPlacer:: create long path cluster units (pathLengthThr=" + std::to_string(pathLengthThr) +
                 ")");
    unsigned int maxSize = 0;
    for (auto timingNode : timingNodes)
    {
        if (timingNode->getLongestPathLength() > pathLengthThr && timingNode->getForwardLevel() > pathLengthThr * 0.333)
        {
            auto curCell = timingNode->getDesignNode();
            auto tmpPU = placementInfo->getPlacementUnitByCell(curCell);
            if (placementUnitId2ClusterUnitId[tmpPU->getId()] < 0)
            {
                std::vector<int> candidateCellIds; //= simpleTimingGraph->BFSFromNode(timingNode->getId(),
                                                   // pathLengthThr, 1000000, extractedCellIds);
                candidateCellIds.clear();

                if (timingNode->getOutEdges().size() >= 16)
                {

                    for (auto outEdge : timingNode->getOutEdges())
                    {
                        if (extractedCellIds.find(outEdge->getSink()->getId()) == extractedCellIds.end())
                            candidateCellIds.push_back(outEdge->getSink()->getId());
                    }

                    // if (candidateCellIds.size() < 16)
                    //     continue;
                }
                else
                {
                    continue;
                }

                std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                PUsInLongPaths.clear();
                for (auto &candidateCellId : candidateCellIds)
                {
                    extractedCellIds.insert(candidateCellId);
                    auto PUInPath = placementInfo->getPlacementUnitByCellId(candidateCellId);
                    // the clusters should not be overlapped with each other
                    if (placementUnitId2ClusterUnitId[PUInPath->getId()] < 0)
                        PUsInLongPaths.insert(PUInPath);
                }

                if (PUsInLongPaths.size() == 0)
                    continue;

                PlacementInfo::ClusterUnit *curClusterUnit = new PlacementInfo::ClusterUnit(clusterUnits.size());
                for (auto PUInPath : PUsInLongPaths)
                {
                    assert(placementUnitId2ClusterUnitId[PUInPath->getId()] < 0);
                    placementUnitId2ClusterUnitId[PUInPath->getId()] = curClusterUnit->getId();
                    curClusterUnit->addPlacementUnit(PUInPath);

                    if (!PUInPath->isFixed())
                    {
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PUInPath))
                        {
                            int cellId = unpackedCell->getCell()->getCellId();
                            extractedCellIds.insert(cellId);
                        }
                        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(PUInPath))
                        {
                            for (auto tmpCell : curMacro->getCells())
                            {
                                int cellId = tmpCell->getCellId();
                                extractedCellIds.insert(cellId);
                            }
                        }
                    }
                }

                if (PUsInLongPaths.size() > maxSize)
                    maxSize = PUsInLongPaths.size();

                clusterUnits.push_back(curClusterUnit);
            }
        }
        assert(clusterUnits.size() <= placementInfo->getPlacementUnits().size());
    }
    print_info("ClusterPlacer: largest long-path cluster size=" + std::to_string(maxSize));
}

void ClusterPlacer::createClockBasedClusterUnits()
{
    AMF_PROFILE_FUNCTION("clustering");
    if (placementInfo->getDesignInfo()->getClocksInDesign().size() > 20)
        return;
    // add the predefined clusters
    for (auto curClock : placementInfo->getDesignInfo()->getClocksInDesign())
    {
        auto &cellSet = placementInfo->getDesignInfo()->getCellsUnderClock(curClock);
        if (cellSet.size() > 5000 || cellSet.size() < 20)
            continue;

        std::set<PlacementInfo::PlacementUnit *> PUsInuserDefinedClusterCluster;
        PUsInuserDefinedClusterCluster.clear();
        for (auto &curCell : cellSet)
        {
            auto tmpPU = placementInfo->getPlacementUnitByCell(curCell);
            // the clusters should not be overlapped with each other
            if (placementUnitId2ClusterUnitId[tmpPU->getId()] < 0)
                PUsInuserDefinedClusterCluster.insert(tmpPU);
        }

        PlacementInfo::ClusterUnit *curClusterUnit = new PlacementInfo::ClusterUnit(clusterUnits.size());
        for (auto tmpPU : PUsInuserDefinedClusterCluster)
        {
            assert(placementUnitId2ClusterUnitId[tmpPU->getId()] < 0);
            placementUnitId2ClusterUnitId[tmpPU->getId()] = curClusterUnit->getId();
            curClusterUnit->addPlacementUnit(tmpPU);
        }
        print_info("build a cluster (size=" + std::to_string(curClusterUnit->getUnits().size()) + ") for clock [" +
                   curClock->getName() + "].");
        clusterUnits.push_back(curClusterUnit);
    }
}

void ClusterPlacer::createUserDefinedClusterBasedClusterUnits()
{
    AMF_PROFILE_FUNCTION("clustering");
    std::vector<std::vector<DesignInfo::DesignCell *>> &predefinedCellClusters =
        placementInfo->getDesignInfo()->getPredefinedClusters();
    placementUnitId2ClusterUnitId = std::vector<int>(placementInfo->getPlacementUnits().size(), -1);
    clusterUnits.clear();
    clusterNets.clear();

    // add the predefined clusters
    int overlapCnt = 0;
    for (auto &clusterCellSet : predefinedCellClusters)
    {

        bool noOverlap = true;
        std::set<PlacementInfo::PlacementUnit *> PUsInuserDefinedClusterCluster;
        PUsInuserDefinedClusterCluster.clear();
        for (auto tmpCell : clusterCellSet)
        {
            auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);
            // the clusters should not be overlapped with each other
            if (placementUnitId2ClusterUnitId[tmpPU->getId()] >= 0)
            {
                noOverlap = false;
                break;
            }
            PUsInuserDefinedClusterCluster.insert(tmpPU);
        }
        if (noOverlap)
        {
            PlacementInfo::ClusterUnit *curClusterUnit = new PlacementInfo::ClusterUnit(clusterUnits.size());
            for (auto tmpPU : PUsInuserDefinedClusterCluster)
            {
                assert(placementUnitId2ClusterUnitId[tmpPU->getId()] < 0);
                placementUnitId2ClusterUnitId[tmpPU->getId()] = curClusterUnit->getId();
                curClusterUnit->addPlacementUnit(tmpPU);
            }
            clusterUnits.push_back(curClusterUnit);
        }
        else
        {
            overlapCnt++;
        }
    }

    print_info("There are " + std::to_string(predefinedCellClusters.size()) + " predefined userDefinedClusters and " +
               std::to_string(overlapCnt) + " overlapped clusters and totally " + std::to_string(clusterUnits.size()) +
               " userDefinedCluster clusters are identified.");

    // handle the other placement units
    createSinglePUClusterUnits();

    // create the nets between the cluster units
    creaeClusterNets();
}

void ClusterPlacer::createSinglePUClusterUnits()
{
    AMF_PROFILE_FUNCTION("clustering");
    for (auto tmpPU : placementInfo->getPlacementUnits())
    {
        if (placementUnitId2ClusterUnitId[tmpPU->getId()] < 0)
        {
            PlacementInfo::ClusterUnit *curClusterUnit = new PlacementInfo::ClusterUnit(clusterUnits.size());
            clusterUnits.push_back(curClusterUnit);
            curClusterUnit->addPlacementUnit(tmpPU);
            placementUnitId2ClusterUnitId[tmpPU->getId()] = curClusterUnit->getId();
        }
    }
}

void ClusterPlacer::basicPartitioning(int minClusterCellNum, int eachClusterDSPNum, int eachClusterBRAMNum)
{
    AMF_PROFILE_FUNCTION("clustering");
    basicGraphPartitioner =
        new GraphPartitioner<std::vector<PlacementInfo::PlacementUnit *>, std::vector<PlacementInfo::PlacementNet *>>(
            placementInfo->getPlacementUnits(), placementInfo->getPlacementNets(), minClusterCellNum, jobs, verbose);
    basicGraphPartitioner->setMaxCutRate(maxMinCutRate);
    basicGraphPartitioner->solve(eachClusterDSPNum, eachClusterBRAMNum);
    clusters = basicGraphPartitioner->getClustersPUIdSets();
    delete basicGraphPartitioner;

    placementUnit2ClusterId.clear();
    placementUnit2ClusterId.resize(placementInfo->getPlacementUnits().size(), -1);
    int clusterId = 0;
    for (auto cluster : clusters)
    {
        for (int placementUnitId : cluster)
        {
            placementUnit2ClusterId[placementUnitId] = clusterId;
        }
        clusterId++;
    }
}

void ClusterPlacer::userDefinedClusterBasedPartitioning(int minClusterCellNum, int eachClusterDSPNum,
                                                        int eachClusterBRAMNum)
{
    AMF_PROFILE_FUNCTION("clustering");
    createUserDefinedClusterBasedClusterUnits();
    userDefinedClusterBasedGraphPartitioner =
        new GraphPartitioner<std::vector<PlacementInfo::ClusterUnit *>, std::vector<PlacementInfo::ClusterNet *>>(
            clusterUnits, clusterNets, minClusterCellNum, jobs, verbose);
    userDefinedClusterBasedGraphPartitioner->setMaxCutRate(maxMinCutRate);
    userDefinedClusterBasedGraphPartitioner->solve(eachClusterDSPNum, eachClusterBRAMNum);
    clusters = userDefinedClusterBasedGraphPartitioner->getClustersPUIdSets();
    delete userDefinedClusterBasedGraphPartitioner;

    placementUnit2ClusterId.clear();
    placementUnit2ClusterId.resize(placementInfo->getPlacementUnits().size(), -1);
    int clusterId = 0;

    std::vector<std::set<int>> newClusters(0);
    for (auto cluster : clusters)
    {
        std::set<int> curCluster;
        int tmpWeight = 0;
        for (int clusterUnitId : cluster)
        {
            for (auto tmpPU : clusterUnits[clusterUnitId]->getUnits())
            {
                assert(placementUnit2ClusterId[tmpPU->getId()] < 0);
                placementUnit2ClusterId[tmpPU->getId()] = clusterId;
                curCluster.insert(tmpPU->getId());
                tmpWeight += tmpPU->getWeight();
            }
        }
        newClusters.push_back(curCluster);
        clusterId++;
    }

    clusters = newClusters;
}

void ClusterPlacer::clockBasedPartitioning(int minClusterCellNum, int eachClusterDSPNum, int eachClusterBRAMNum)
{
    AMF_PROFILE_FUNCTION("clustering");
    if(dfxPartitionFloorplan) {
        for(auto n:clusterNets)delete n;
        for(auto u:clusterUnits)delete u;
    }
    resetClusterInfo();

    if(dfxPartitionFloorplan)createProtectedRegionClusterUnits();
    const int protectedCount=dfxPartitionFloorplan?placementInfo->getHardRegions().regions.size()-1:0;
    createClockBasedClusterUnits();
    print_info("ClusterPlacer: There are " +
               std::to_string(placementInfo->getDesignInfo()->getClocksInDesign().size()) +
               " global clocks and totally " + std::to_string(clusterUnits.size()) + " clock clusters are identified.");

    int clusterNumRecord = clusterUnits.size();
    createLongPathClusterUnits();
    print_info("ClusterPlacer: There are totally " + std::to_string(clusterUnits.size() - clusterNumRecord) +
               " long-path clusters are identified.");

    clusterNumRecord = clusterUnits.size();
    createSinglePUClusterUnits();
    print_info("ClusterPlacer: There are totally " + std::to_string(clusterUnits.size() - clusterNumRecord) +
               " Single-PU clusters are identified.");

    // handle the other placement units
    creaeClusterNets();

    clockBasedGraphPartitioner =
        new GraphPartitioner<std::vector<PlacementInfo::ClusterUnit *>, std::vector<PlacementInfo::ClusterNet *>>(
            clusterUnits, clusterNets, minClusterCellNum, jobs, verbose);
    clockBasedGraphPartitioner->setMaxCutRate(maxMinCutRate);
    if(dfxPartitionFloorplan) {
        std::vector<int> staticNodes;
        for(size_t i=protectedCount;i<clusterUnits.size();++i)
            if(!clusterUnits[i]->getUnits().empty())staticNodes.push_back(i);
        clockBasedGraphPartitioner->solveSubset(staticNodes,eachClusterDSPNum,eachClusterBRAMNum);
    }else clockBasedGraphPartitioner->solve(eachClusterDSPNum, eachClusterBRAMNum);
    clusters = clockBasedGraphPartitioner->getClustersPUIdSets();
    clusterRegions.assign(clusters.size(),0);
    for(int i=0;i<protectedCount;++i){clusters.push_back({i});clusterRegions.push_back(i+1);}
    delete clockBasedGraphPartitioner;

    placementUnit2ClusterId.clear();
    placementUnit2ClusterId.resize(placementInfo->getPlacementUnits().size(), -1);
    int clusterId = 0;

    std::vector<std::set<int>> newClusters(0);
    unsigned int PUCnt = 0;
    for (auto cluster : clusters)
    {
        std::set<int> curCluster;
        int tmpWeight = 0;
        for (int clusterUnitId : cluster)
        {
            for (auto tmpPU : clusterUnits[clusterUnitId]->getUnits())
            {
                assert(placementUnit2ClusterId[tmpPU->getId()] < 0);
                placementUnit2ClusterId[tmpPU->getId()] = clusterId;
                curCluster.insert(tmpPU->getId());
                tmpWeight += tmpPU->getWeight();
                PUCnt++;
            }
        }
        newClusters.push_back(curCluster);
        clusterId++;
    }
    if(dfxPartitionFloorplan && PUCnt!=placementUnit2ClusterId.size())
        throw std::runtime_error("DFX partition lost placement-unit coverage");
    assert(PUCnt == placementUnit2ClusterId.size());
    clusters = newClusters;

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    int pathLenThr = placementInfo->getLongPathThresholdLevel();
    std::set<DesignInfo::DesignCell *> highFanoutRegs;
    enhancedClusterCluster.clear();
    int enhancedCnt = 0;
    if (false) // if (clusters.size() < 20)
    {
        std::set<int> extractedCellIds;
        std::set<PlacementInfo::PlacementUnit *> extractedPUs;
        extractedCellIds.clear();
        extractedPUs.clear();

        for (unsigned int nodeId = 0; nodeId < timingNodes.size(); nodeId++)
        {
            auto timingNode = timingNodes[nodeId];
            if (timingNode->getForwardLevel() == 1 && timingNode->getLongestPathLength() > pathLenThr * 0.9)
            {
                for (auto inEdge : timingNode->getInEdges())
                {
                    auto driverNode = inEdge->getSource();
                    if (auto driverCell = driverNode->getDesignNode())
                    {
                        if (driverNode->getForwardLevel() == 0)
                        {
                            if (driverNode->getOutEdges().size() >= 64)
                            {
                                if (highFanoutRegs.find(driverCell) == highFanoutRegs.end())
                                {
                                    highFanoutRegs.insert(driverCell);
                                    std::vector<int> clustersBeDrivenCnt(clusters.size(), 0);
                                    for (auto outEdge : driverNode->getOutEdges())
                                    {
                                        auto fanoutNode = outEdge->getSink();
                                        if (fanoutNode->getLongestPathLength() > pathLenThr * 0.9)
                                        {
                                            auto curPU = placementInfo->getPlacementUnitByCellId(fanoutNode->getId());
                                            int tmpClusterId = placementUnit2ClusterId[curPU->getId()];

                                            clustersBeDrivenCnt[tmpClusterId]++;
                                        }
                                    }

                                    int nonZeroCnt = 0;
                                    for (auto cnt : clustersBeDrivenCnt)
                                    {
                                        nonZeroCnt += (cnt > 0);
                                    }

                                    if (nonZeroCnt >= 3)
                                    {
                                        std::cout << driverCell << " level=" << timingNode->getLongestPathLength()
                                                  << " fanout=" << driverNode->getOutEdges().size()
                                                  << " #cluster=" << nonZeroCnt << "\n";

                                        auto candidateCellIds =
                                            placementInfo->getTimingInfo()
                                                ->getSimplePlacementTimingGraph()
                                                ->BFSFromNode(driverNode->getId(), pathLenThr * 0.666, 20000,
                                                              extractedCellIds);
                                        std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                                        PUsInLongPaths.clear();

                                        float totalWeight = 0;
                                        std::set<int> tmpNewCluster;
                                        tmpNewCluster.clear();

                                        for (auto candidateCellId : candidateCellIds)
                                        {
                                            auto PUInPath = placementInfo->getPlacementUnitByCellId(candidateCellId);
                                            if (extractedPUs.find(PUInPath) == extractedPUs.end() &&
                                                PUsInLongPaths.find(PUInPath) == PUsInLongPaths.end())
                                            {
                                                PUsInLongPaths.insert(PUInPath);
                                                tmpNewCluster.insert(PUInPath->getId());
                                                totalWeight += PUInPath->getWeight();
                                            }
                                        }
                                        print_warning("trying to enhance new cluster generated from " +
                                                      driverCell->getName() + " with " +
                                                      std::to_string(tmpNewCluster.size()) + " PUs and " +
                                                      std::to_string(candidateCellIds.size()) + " cells");
                                        if (tmpNewCluster.size() > 4000)
                                        {
                                            print_warning("enhanced new cluster generated from " +
                                                          driverCell->getName() + " with " +
                                                          std::to_string(tmpNewCluster.size()) + " PUs");
                                            for (auto candidateCellId : candidateCellIds)
                                            {
                                                auto PUInPath =
                                                    placementInfo->getPlacementUnitByCellId(candidateCellId);
                                                if (PUsInLongPaths.find(PUInPath) != PUsInLongPaths.end())
                                                {
                                                    extractedPUs.insert(PUInPath);
                                                    extractedCellIds.insert(candidateCellId);
                                                }
                                            }
                                            inEdge->getSourcePin()->getNet()->setOverallTimingNetEnhancement(2);
                                            enhancedCnt++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (enhancedCnt > 3)
                break;
        }
    }
}

void ClusterPlacer::refineClustersWithPredefinedClusters()
{
    AMF_PROFILE_FUNCTION("clustering");
    std::vector<std::vector<DesignInfo::DesignCell *>> &predefinedCellClusters =
        placementInfo->getDesignInfo()->getPredefinedClusters();

    std::set<PlacementInfo::PlacementUnit *> reallocatedPUs;

    int reallocateCnt = 0;
    for (auto &clusterCellSet : predefinedCellClusters)
    {
        std::vector<int> cluster2overlappedCellNum(clusters.size(), 0);
        int maxOverlappedClusterId = 0;
        int maxOverlapCnt = 0;

        int DSPCnt = 0;
        int BRAMCnt = 0;
        for (auto tmpCell : clusterCellSet)
        {
            DSPCnt += tmpCell->isDSP();
            BRAMCnt += tmpCell->isBRAM();
        }

        if (DSPCnt >= 8 || BRAMCnt >= 8)
            continue;

        // find the most overlapped partitioned cluster for the predefined cluster
        for (auto tmpCell : clusterCellSet)
        {
            auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);
            auto tmpPUId = tmpPU->getId();

            for (unsigned int i = 0; i < clusters.size(); i++)
            {
                if (clusters[i].find(tmpPUId) != clusters[i].end())
                {
                    cluster2overlappedCellNum[i]++;
                    if (cluster2overlappedCellNum[i] > maxOverlapCnt)
                    {
                        cluster2overlappedCellNum[i] = cluster2overlappedCellNum[i];
                        maxOverlappedClusterId = i;
                    }
                    break;
                }
            }
        }

        // move cells in predefined Cluster togather
        for (auto tmpCell : clusterCellSet)
        {
            auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);
            auto tmpPUId = tmpPU->getId();

            // don't reallocate PU twice
            if (reallocatedPUs.find(tmpPU) != reallocatedPUs.end())
                continue;
            reallocatedPUs.insert(tmpPU);

            if (placementUnit2ClusterId[tmpPUId] != maxOverlappedClusterId)
            {
                clusters[placementUnit2ClusterId[tmpPUId]].erase(tmpPUId);
                clusters[maxOverlappedClusterId].insert(tmpPUId);
                placementUnit2ClusterId[tmpPUId] = maxOverlappedClusterId;
                reallocateCnt++;
            }
        }
    }

    print_info("reallocate " + std::to_string(reallocateCnt) + " PU(s)");
}

bool ClusterPlacer::isClustersToLarges()
{
    AMF_PROFILE_FUNCTION("clustering");
    std::string infoStr = "Recursive partitioning generates " + std::to_string(clusters.size()) +
                          " clusters and the numbers of the placement units for them are: ";

    auto &PUs = placementInfo->getPlacementUnits();

    int totalCellsinCluster = 0;
    for (auto cluster : clusters)
    {
        int DSPNum = 0;
        int BRAMNum = 0;
        int cellsinCluster = 0;
        for (int PUId : cluster)
        {
            if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(PUs[PUId]))
            {
                for (auto tmpCell : curMacro->getCells())
                {
                    DSPNum += tmpCell->isDSP();
                    BRAMNum += tmpCell->isBRAM();
                }
            }
            else if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PUs[PUId]))
            {
                DSPNum += curUnpackedCell->getCell()->isDSP();
                BRAMNum += curUnpackedCell->getCell()->isBRAM();
            }

            cellsinCluster += PUs[PUId]->getWeight();
            totalCellsinCluster += PUs[PUId]->getWeight();
        }
        infoStr += std::to_string(cluster.size()) + "-" + std::to_string(cellsinCluster) +
                   "(DSP:" + std::to_string(DSPNum) + ",BRAM:" + std::to_string(BRAMNum) + "), ";
    }

    print_info(infoStr);

    if(dfxPartitionFloorplan) {
        size_t staticWeight=0,staticClusters=0;
        for(size_t c=0;c<clusters.size();++c)if(clusterRegions[c]==0) {
            ++staticClusters;
            for(int id:clusters[c])staticWeight+=PUs[id]->getWeight();
        }
        return staticClusters&&double(staticWeight)/staticClusters>0.75*avgClusterSizeRequirement;
    }
    if ((totalCellsinCluster / clusters.size()) > 0.75 * avgClusterSizeRequirement)
    {
        print_warning("Average cluster size is too large = " + std::to_string((totalCellsinCluster / clusters.size())));
        return true;
    }
    return false;
}

bool containIOCells(PlacementInfo::PlacementUnit *curPU)
{
    if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
    {
        return unpackedCell->getCell()->isIO();
    }
    else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
    {
        for (auto tmpCell : curMacro->getCells())
        {
            if (tmpCell->isIO())
            {
                return true;
            }
        }
        return false;
    }
    return false;
}

void ClusterPlacer::setClusterNetsAdjMat()
{
    AMF_PROFILE_FUNCTION("clustering");
    clusterAdjMat = std::vector<std::vector<float>>(clusters.size(), std::vector<float>(clusters.size(), 0));
    clusterCLBCellWeights = std::vector<float>(clusters.size(), 0);
    clusterDSPCellWeights = std::vector<float>(clusters.size(), 0);
    clusterBRAMCellWeights = std::vector<float>(clusters.size(), 0);
    cluster2FixedUnitMat = std::vector<std::vector<float>>(
        clusters.size(), std::vector<float>(placementInfo->getFixedPlacementUnits().size(), 0));

    fixedX.clear();
    fixedY.clear();

    float clockRegionW = (placementInfo->getGlobalMaxX() - placementInfo->getGlobalMinX()) / clockRegionXNum;

    std::map<PlacementInfo::PlacementUnit *, int> fixedPlacementUnit2LocId;
    fixedPlacementUnit2LocId.clear();

    float pathThr = placementInfo->getMediumPathThresholdLevel();

    int ignoredConnectCnt = 0;
    bool cutShortPathNets = !isDensePlacement();
    for (auto net : placementInfo->getPlacementNets())
    {
        for (auto driveU : net->getDriverUnits())
        {
            if (driveU->isFixed())
            {
                if (fixedPlacementUnit2LocId.find(driveU) == fixedPlacementUnit2LocId.end())
                {
                    int amo = fixedPlacementUnit2LocId.size();
                    fixedPlacementUnit2LocId[driveU] = amo;
                    if (containIOCells(driveU))
                    {
                        fixedX.push_back(driveU->X() + clockRegionW / 2.0);
                        fixedY.push_back(driveU->Y());
                    }
                    else
                    {
                        fixedX.push_back(driveU->X());
                        fixedY.push_back(driveU->Y());
                    }
                }
            }

            for (auto UBeDriven : net->getUnitsBeDriven())
            {
                int A = driveU->getId();
                int B = UBeDriven->getId();
                int maxLength = getPlacementUnitMaxPathLen(UBeDriven);

                if (maxLength > 0 && maxLength < 3 && net->getUnitsBeDriven().size() < 8 && cutShortPathNets)
                    continue;

                int clusterA = placementUnit2ClusterId[A];
                int clusterB = placementUnit2ClusterId[B];
                assert(clusterA >= 0 && clusterB >= 0);
                assert((unsigned int)clusterA < cluster2FixedUnitMat.size() &&
                       (unsigned int)clusterB < cluster2FixedUnitMat.size());
                if (UBeDriven->isFixed())
                {
                    if (fixedPlacementUnit2LocId.find(UBeDriven) == fixedPlacementUnit2LocId.end())
                    {
                        int amo = fixedPlacementUnit2LocId.size();
                        fixedPlacementUnit2LocId[UBeDriven] = amo;
                        if (containIOCells(UBeDriven))
                        {
                            fixedX.push_back(UBeDriven->X() + clockRegionW / 2.0);
                            fixedY.push_back(UBeDriven->Y());
                        }
                        else
                        {
                            fixedX.push_back(UBeDriven->X());
                            fixedY.push_back(UBeDriven->Y());
                        }
                    }
                }

                if (net->getPinOffsetsInUnit().size() > 1)
                {
                    if (UBeDriven->isFixed() && !driveU->isFixed())
                    {
                        assert(fixedPlacementUnit2LocId.find(UBeDriven) != fixedPlacementUnit2LocId.end());
                        assert((unsigned int)fixedPlacementUnit2LocId[UBeDriven] <
                               cluster2FixedUnitMat[clusterA].size());
                        cluster2FixedUnitMat[clusterA][fixedPlacementUnit2LocId[UBeDriven]] +=
                            net->getDesignNet()->getOverallEnhanceRatio() / (net->getPinOffsetsInUnit().size() - 1);
                    }
                    else if (!UBeDriven->isFixed() && driveU->isFixed())
                    {
                        assert(fixedPlacementUnit2LocId.find(driveU) != fixedPlacementUnit2LocId.end());
                        assert((unsigned int)fixedPlacementUnit2LocId[driveU] < cluster2FixedUnitMat[clusterB].size());
                        cluster2FixedUnitMat[clusterB][fixedPlacementUnit2LocId[driveU]] +=
                            net->getDesignNet()->getOverallEnhanceRatio() / (net->getPinOffsetsInUnit().size() - 1);
                    }
                    else if (!UBeDriven->isFixed() && !driveU->isFixed())
                    {
                        clusterAdjMat[clusterA][clusterB] +=
                            net->getDesignNet()->getOverallEnhanceRatio() / (net->getPinOffsetsInUnit().size() - 1);
                        clusterAdjMat[clusterB][clusterA] +=
                            net->getDesignNet()->getOverallEnhanceRatio() / (net->getPinOffsetsInUnit().size() - 1);
                    }
                }
            }
        }
    }

    for (auto curPU : placementInfo->getPlacementUnits())
    {
        int PUId = curPU->getId();
        int clusterId = placementUnit2ClusterId[PUId];
        clusterCLBCellWeights[clusterId] += curPU->getWeight();
        clusterDSPCellWeights[clusterId] += curPU->getDSPNum();
        clusterBRAMCellWeights[clusterId] += curPU->getBRAMNum();
    }
}

void ClusterPlacer::placeClusters()
{
    AMF_PROFILE_FUNCTION("clustering");
    print_status("SA-based Cluster Placement Start.");
    assert(JSONCfg.find("clockRegionXNum") != JSONCfg.end());
    assert(JSONCfg.find("clockRegionYNum") != JSONCfg.end());

    int gridH = std::stoi(JSONCfg["clockRegionYNum"]);
    int gridW = std::stoi(JSONCfg["clockRegionXNum"]);
    int SAIterNum = 100000;
    int restartNum = 10;

    if (JSONCfg.find("Simulated Annealing IterNum") != JSONCfg.end())
        SAIterNum = std::stoi(JSONCfg["Simulated Annealing IterNum"]);
    if (JSONCfg.find("Simulated Annealing restartNum") != JSONCfg.end())
        restartNum = std::stoi(JSONCfg["Simulated Annealing restartNum"]);
    float deviceW = (placementInfo->getGlobalMaxX() - placementInfo->getGlobalMinX());
    float deviceH = (placementInfo->getGlobalMaxY() - placementInfo->getGlobalMinY());

    // SA-based cluster placement
    const float saY2xRatio = SAPlacer::configuredY2XRatio(JSONCfg, y2xRatio);
    print_status("SA effective y2xRatio = " + std::to_string(saY2xRatio) +
                 (JSONCfg.count("Simulated Annealing y2xRatio") ? " (explicit)" : " (legacy y2xRatio * 0.8)"));
    if(dfxPartitionFloorplan) {
        placeDFXClusters(gridH,gridW,SAIterNum,restartNum,saY2xRatio);
        placementInfo->updateElementBinGrid();
        writeDFXPartitionAudit();
        print_status("DFX partition/floorplan completed; RP representatives fixed, internal cells remain movable.");
        return;
    }
    saPlacer = new SAPlacer("ClusterSA", clusterAdjMat, enhancedClusterCluster, clusterCLBCellWeights, cluster2FixedUnitMat, fixedX, fixedY,
                            gridH, gridW, deviceH, deviceW, connectionToFixedFactor, saY2xRatio, SAIterNum, jobs,
                            restartNum, verbose);
    saPlacer->solve();
    cluster2XY = saPlacer->getCluster2XY();

    placeUnitBaseOnClusterPlacement(cluster2XY);

    placementInfo->updateElementBinGrid();
    print_status("SA-based Cluster Placement Done.");
}

void ClusterPlacer::placeUnitBaseOnClusterPlacement(const std::vector<std::pair<int, int>> &cluster2XY)
{
    AMF_PROFILE_FUNCTION("clustering");
    int gridH = std::stoi(JSONCfg["clockRegionYNum"]);
    int gridW = std::stoi(JSONCfg["clockRegionXNum"]);

    float deviceW = (placementInfo->getGlobalMaxX() - placementInfo->getGlobalMinX());
    float deviceH = (placementInfo->getGlobalMaxY() - placementInfo->getGlobalMinY());

    float regionW = deviceW / gridW;
    float regionH = deviceH / gridH;

    cluster2FP_XY.clear();
    for (auto intXY : cluster2XY)
    {
        float fX = intXY.first * regionW + regionW / 2;
        float fY = intXY.second * regionH + regionH / 2;
        if(placementInfo->sparseResourcePlacementEnabled()) {
            fX+=placementInfo->getGlobalMinX();
            fY+=placementInfo->getGlobalMinY();
        }
        cluster2FP_XY.push_back(std::pair<float, float>(fX, fY));
    }

    assert(!randomInitialPlacement);

    for (auto curPU : placementInfo->getPlacementUnits())
    {
        if (!curPU->isFixed())
        {
            // float fX = random_float(cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].first - regionW / 2,
            //                         cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].first + regionW / 2);
            // float fY = random_float(cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].second - regionH / 2,
            //                         cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].second + regionH / 2);
            float fX = cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].first;
            float fY = cluster2FP_XY[placementUnit2ClusterId[curPU->getId()]].second;
            placementInfo->legalizeXYInArea(curPU, fX, fY);
            curPU->setAnchorLocation(fX, fY);
        }
    }
}

void ClusterPlacer::dumpClusters()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    std::string dumpClustersFile = JSONCfg["Dump Cluster file"];
    if (dumpClustersFile != "")
    {
        print_status("dumping cluster information to " + dumpClustersFile);
        std::ofstream outfile0((dumpClustersFile + ".tcl").c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for cluster result dumping does not exist and please check your path settings");
        for (unsigned int cluster_id = 0; cluster_id < clusters.size(); cluster_id++)
        {
            outfile0 << "# placed at region coordinate: X " << cluster2XY[cluster_id].first << " Y "
                     << cluster2XY[cluster_id].second << "\n";
            outfile0 << "highlight -color_index " << (cluster_id) % 20 + 1 << "  [get_cells {";
            for (int id : clusters[cluster_id])
            {
                if (auto tmpMacro =
                        dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnits()[id]))
                {
                    for (auto cell : tmpMacro->getCells())
                    {
                        outfile0 << cell->getName() << " ";
                    }
                }
                else if (auto tmpUnpacked = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(
                             placementInfo->getPlacementUnits()[id]))
                {
                    outfile0 << tmpUnpacked->getName() << " ";
                }
                else
                {
                    assert(false);
                }
            }
            outfile0 << "}]\n";
        }
        outfile0.close();
        outfile0 = std::ofstream(dumpClustersFile.c_str());
        for (unsigned int cluster_id = 0; cluster_id < clusters.size(); cluster_id++)
        {
            for (int id : clusters[cluster_id])
            {
                if (auto tmpMacro =
                        dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnits()[id]))
                {
                    for (auto cell : tmpMacro->getCells())
                    {
                        outfile0 << cell->getName() << " ";
                    }
                }
                else if (auto tmpUnpacked = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(
                             placementInfo->getPlacementUnits()[id]))
                {
                    outfile0 << tmpUnpacked->getName() << " ";
                }
                else
                {
                    assert(false);
                }
            }
            outfile0 << "\n";
        }
        outfile0.close();
    }
}

void ClusterPlacer::drawClusters()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    std::vector<std::pair<int, int>> lines;
    lines.clear();
    for (unsigned int clusterA = 1; clusterA < clusterAdjMat.size(); clusterA++)
        for (unsigned int clusterB = 0; clusterB < clusterA; clusterB++)
        {
            if (clusterAdjMat[clusterA][clusterB] > 0)
            {
                lines.push_back(std::pair<int, int>(clusterA, clusterB));
            }
        }
    // paintClusterNodeLine(cluster2FP_XY, lines, cluster2FixedUnitMat, fixedX, fixedY);
}

void ClusterPlacer::createProtectedRegionClusterUnits()
{
    const auto &regions=placementInfo->getHardRegions().regions;
    for(size_t r=1;r<regions.size();++r)
        clusterUnits.push_back(new PlacementInfo::ClusterUnit(clusterUnits.size()));
    for(auto pu:placementInfo->getPlacementUnits()) {
        int r=placementInfo->hardRegionForPU(pu);
        if(r==0)continue;
        clusterUnits.at(r-1)->addPlacementUnit(pu);
        placementUnitId2ClusterUnitId.at(pu->getId())=r-1;
    }
    for(size_t r=1;r<regions.size();++r)
        if(clusterUnits[r-1]->getUnits().empty())
            throw std::runtime_error("Empty protected RP: "+regions[r].name);
}

void ClusterPlacer::placeDFXClusters(int gridH,int gridW,int iterations,int restarts,float ratio)
{
    const auto &hard=placementInfo->getHardRegions();
    const float x0=placementInfo->getGlobalMinX(),y0=placementInfo->getGlobalMinY();
    const float w=placementInfo->getGlobalMaxX()-x0,h=placementInfo->getGlobalMaxY()-y0;
    if(gridH<=0||gridW<=0||iterations<=0||restarts<=0||jobs<=0||
       !std::isfinite(connectionToFixedFactor)||connectionToFixedFactor<=0)
        throw std::runtime_error("Invalid DFX floorplan SA configuration");
    std::vector<int> movable,pinned;
    cluster2FP_XY.resize(clusters.size());cluster2XY.resize(clusters.size());
    for(size_t i=0;i<clusters.size();++i) {
        if(clusterRegions.at(i)==0)movable.push_back(i);
        else {
            pinned.push_back(i);
            const auto &b=hard.regions.at(clusterRegions[i]).box;
            cluster2FP_XY[i]={(b.lx+b.hx)/2,(b.ly+b.hy)/2};
            cluster2XY[i]={std::min(gridW-1,std::max(0,int((cluster2FP_XY[i].first-x0)*gridW/w))),
                           std::min(gridH-1,std::max(0,int((cluster2FP_XY[i].second-y0)*gridH/h)))};
        }
    }
    // Retain RP-to-static adjacency as fixed-terminal costs of equal weight.
    // RP-to-RP cost is constant and does not affect the static SA optimum.
    std::vector<std::vector<float>> adj(movable.size(),std::vector<float>(movable.size()));
    std::vector<std::vector<float>> terminals;std::vector<float> weights,tx=fixedX,ty=fixedY;
    for(int rp:pinned){tx.push_back(cluster2FP_XY[rp].first);ty.push_back(cluster2FP_XY[rp].second);}
    for(size_t i=0;i<movable.size();++i) {
        int original=movable[i];weights.push_back(clusterCLBCellWeights[original]);
        terminals.push_back(cluster2FixedUnitMat[original]);
        for(int rp:pinned)terminals.back().push_back(clusterAdjMat[original][rp]/connectionToFixedFactor);
        for(size_t j=0;j<movable.size();++j)adj[i][j]=clusterAdjMat[original][movable[j]];
    }
    std::vector<std::pair<float,float>> points;std::vector<bool> allowed;
    for(int gy=0;gy<gridH;++gy)for(int gx=0;gx<gridW;++gx) {
        float x=x0+(gx+.5f)*w/gridW,y=y0+(gy+.5f)*h/gridH,px=x,py=y;
        hard.project(0,0,0,0,0,px,py);
        points.emplace_back(x,y);allowed.push_back(std::fabs(px-x)<1e-5f&&std::fabs(py-y)<1e-5f);
    }
    if(!movable.empty()) {
        std::vector<std::vector<int>> enhanced;
        SAPlacer placer("DFXStaticClusterSA",adj,enhanced,weights,terminals,tx,ty,
                        gridH,gridW,h,w,connectionToFixedFactor,ratio,iterations,jobs,restarts,verbose);
        placer.setPhysicalGrid(points,allowed);
        placer.solve();
        const auto &xy=placer.getCluster2XY();
        for(size_t i=0;i<movable.size();++i) {
            cluster2XY[movable[i]]=xy[i];
            cluster2FP_XY[movable[i]]=points.at(xy[i].second*gridW+xy[i].first);
        }
    }
    for(auto pu:placementInfo->getPlacementUnits())if(!pu->isFixed()) {
        auto xy=cluster2FP_XY.at(placementUnit2ClusterId.at(pu->getId()));
        placementInfo->legalizeXYInArea(pu,xy.first,xy.second);
        pu->setAnchorLocation(xy.first,xy.second);
    }
}

void ClusterPlacer::writeDFXPartitionAudit()
{
    const auto &hard=placementInfo->getHardRegions();
    std::vector<size_t> rpClusters(hard.regions.size(),0);
    std::ofstream out(JSONCfg.at("DFX partition audit file"));
    out<<"cluster\tregion\tinstance\tplacement_units\trepresentative_x\trepresentative_y\texternal_weight\n";
    for(size_t c=0;c<clusters.size();++c) {
        int r=clusterRegions.at(c);++rpClusters.at(r);
        for(int id:clusters[c])
            if(placementInfo->hardRegionForPU(placementInfo->getPlacementUnits().at(id))!=r)
                throw std::runtime_error("DFX partition mixed regions");
        float external=0;
        for(size_t j=0;j<clusters.size();++j)if(j!=c)external+=clusterAdjMat[c][j];
        out<<c<<'\t'<<hard.regions[r].name<<'\t'<<hard.regions[r].prefix<<'\t'<<clusters[c].size()
           <<'\t'<<cluster2FP_XY[c].first<<'\t'<<cluster2FP_XY[c].second<<'\t'<<external<<'\n';
    }
    for(size_t r=1;r<rpClusters.size();++r)
        if(rpClusters[r]!=1)throw std::runtime_error("Protected RP was split or lost");
    out.close();if(!out)throw std::runtime_error("Cannot write DFX partition audit");
}
