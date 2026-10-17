#include "LegacyComponentMatching.h"
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

using namespace amf_legacy_components;
static void require(bool ok, const std::string &message)
{
    if (!ok) throw std::runtime_error(message);
}

// Independent maximum-cardinality oracle; native costs intentionally retain the
// historical asymmetric bias, so do not claim exact original-cost optimality.
static int maximumCardinality(const Adjacency &adj, int rights)
{
    std::vector<int> owner(rights, -1);
    std::function<bool(int, std::vector<bool> &)> augment = [&](int u, std::vector<bool> &seen) {
        for (const auto &edge : adj[u]) if (!seen[edge.first])
        {
            seen[edge.first] = true;
            if (owner[edge.first] < 0 || augment(owner[edge.first], seen))
            { owner[edge.first] = u; return true; }
        }
        return false;
    };
    int count = 0;
    for (int u = 0; u < static_cast<int>(adj.size()); ++u)
    { std::vector<bool> seen(rights, false); count += augment(u, seen); }
    return count;
}

static std::vector<int> nativeSingleGraph(const Adjacency &adj, int rights)
{
    const int nl = adj.size(), source = nl + rights, sink = source + 1;
    MinCostFlow solver(sink + 1, nl, source, sink);
    for (int u = 0; u < nl; ++u)
        for (const auto &edge : adj[u]) solver.addEdge(u, nl + edge.first, 1, edge.second);
    for (int u = 0; u < nl; ++u) solver.addEdge(source, u, 1, 0);
    for (int v = 0; v < rights; ++v) solver.addEdge(nl + v, sink, 1, 0);
    solver.calcMinCostFlow(source, sink, nl);
    std::vector<int> result(nl, -1);
    for (int u = 0; u < nl; ++u)
        for (const auto *edge : solver.resGraph.adj[u])
            if (edge->destination >= nl && edge->destination < nl + rights && edge->residualFlow == 0)
                result[u] = edge->destination - nl;
    return result;
}

static void check(const Adjacency &adj, int rights, bool parallel)
{
    const auto serial = solve(adj, rights, adj.size(), 1);
    require(serial.cardinality == maximumCardinality(adj, rights), "maximum cardinality differs");
    if (serial.components == 1 && !adj.empty())
        require(serial.leftToRight == nativeSingleGraph(adj, rights), "single-component native behavior changed");
    if (parallel) for (int threads : {2, 8, 12, 16})
    {
        const auto other = solve(adj, rights, adj.size(), threads);
        require(other.leftToRight == serial.leftToRight && other.cost == serial.cost &&
                other.graphHash == serial.graphHash && other.assignmentHash == serial.assignmentHash &&
                other.searchCountersHash == serial.searchCountersHash,
                "thread count changed assignments or native search counters");
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc == 5 && std::string(argv[1]) == "--replay")
        {
            std::ifstream in(argv[2]);
            std::string magic;
            int nl, nr, target;
            require(bool(in >> magic >> nl >> nr >> target) && magic == "AMF_MATCHING_V1", "bad snapshot");
            Adjacency adj(nl);
            int u, v; float c;
            while (in >> u >> v >> c) { require(u >= 0 && u < nl, "invalid left"); adj[u].emplace_back(v, c); }
            require(in.eof(), "malformed edge");
            const auto result = solve(adj, nr, target, std::stoi(argv[3]));
            require(result.cardinality == maximumCardinality(adj, nr), "replay cardinality mismatch");
            std::ofstream out(argv[4]);
            for (int i = 0; i < nl; ++i) out << i << '\t' << result.leftToRight[i] << '\n';
            unsigned long long visits=0, relax=0, spfa=0, augment=0;
            for (auto s: result.searches) { visits+=s.visits;relax+=s.relax;spfa+=s.spfa;augment+=s.augment; }
            std::cout << std::setprecision(15) << "{\"threads\":" << argv[3]
                      << ",\"components\":" << result.components << ",\"matched\":" << result.cardinality
                      << ",\"cost\":" << result.cost << ",\"graph_hash\":\"" << result.graphHash
                      << "\",\"assignment_hash\":\"" << result.assignmentHash
                      << "\",\"search_counters_hash\":\"" << result.searchCountersHash
                      << "\",\"visits\":" << visits << ",\"relax\":" << relax
                      << ",\"spfa\":" << spfa << ",\"augment\":" << augment
                      << ",\"prepare_seconds\":" << result.preparationSeconds
                      << ",\"kernel_seconds\":" << result.kernelSeconds << "}\n";
            return 0;
        }
        require(argc == 1, "usage: checkLegacyComponents [--replay snapshot threads assignments.tsv]");
        int cases = 0;
        check({}, 0, true); ++cases;
        check(Adjacency(5), 0, true); ++cases;
        check({{{0,1},{1,1.001f}},{{0,1},{1,1.009f}}},2,true); ++cases;
        check({{{0,1},{1,1.001f}},{{0,1},{1,1.02f}}},2,true); ++cases;
        check({{{1,1},{1,2}},{{1,1}}},4,true); ++cases;
        for (int mask = 0; mask < 512; ++mask)
        {
            Adjacency adj(3);
            for (int u = 0; u < 3; ++u) for (int v = 0; v < 3; ++v)
                if (mask & (1 << (u * 3 + v))) adj[u].emplace_back(v, 1.f + ((u * 7 + v * 11) % 13) / 8.f);
            check(adj, 3, mask % 8 == 0); ++cases;
        }
        std::mt19937 random(20261012);
        for (int trial=0;trial<200;++trial)
        {
            const int nl=1+random()%8,nr=random()%10;
            Adjacency adj(nl);
            for(int u=0;u<nl;++u)for(int v=0;v<nr;++v)
                if(random()%3)adj[u].emplace_back(v,1.f+(random()%300)/16.f);
            check(adj,nr,trial%5==0);++cases;
        }
        // Add unrelated components, keeping the original component's row and
        // candidate order. It must not change the original assignments.
        Adjacency a{{{0,1},{1,1.001f}},{{0,1},{1,1.009f}}};
        const auto original=solve(a,2,2,1);
        for(int block=1;block<100;++block)
        {
            a.push_back({{2*block,1},{2*block+1,1.001f}});
            a.push_back({{2*block,1},{2*block+1,1.009f}});
        }
        check(a,200,true);++cases;
        const auto combined=solve(a,200,a.size(),16);
        require(std::equal(original.leftToRight.begin(),original.leftToRight.end(),combined.leftToRight.begin()),
                "unrelated components changed original matching");
        bool rejected=false;
        try { solve(a,200,1,12); } catch(const std::invalid_argument &) { rejected=true; }
        require(rejected,"partial global quota silently accepted");
        for(float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        {
            rejected=false;
            try { solve({{{0,invalid}}},1,1,1); } catch(const std::invalid_argument &) { rejected=true; }
            require(rejected,"invalid legacy cost accepted");
        }
        std::cout << "PASS " << cases << " graphs: cardinality oracle, native connected-graph parity,"
                  << " component independence, 1/2/8/12/16 assignment and search-counter determinism\n";
    }
    catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
