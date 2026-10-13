#ifndef AMF_LEGACY_COMPONENT_MATCHING_H
#define AMF_LEGACY_COMPONENT_MATCHING_H

#include "minCostFlow/MinCostFlow.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

// Keep the native SPFA, float costs, forward +0.01 and eps unchanged. Only
// connected-component boundaries, local indexing and scheduling differ.
namespace amf_legacy_components
{
using Adjacency = std::vector<std::vector<std::pair<int, float>>>;
using Clock = std::chrono::steady_clock;
inline double seconds(Clock::time_point t)
{
    return std::chrono::duration<double>(Clock::now() - t).count();
}

inline void hashWord(std::uint64_t &hash, std::uint64_t word)
{
    for (int i = 0; i < 8; ++i)
    {
        hash = (hash ^ (word & 255)) * UINT64_C(1099511628211);
        word >>= 8;
    }
}

struct Component
{
    std::vector<int> left, right;
    std::size_t edges = 0;
};

struct SearchStats
{
    unsigned long long visits = 0, relax = 0, spfa = 0, augment = 0;
    double buildSeconds = 0, solveSeconds = 0;
};

struct Result
{
    std::vector<int> leftToRight;
    std::vector<SearchStats> searches;
    std::size_t edges = 0, components = 0, largestLeft = 0, largestEdges = 0;
    int cardinality = 0;
    double cost = 0, preparationSeconds = 0, kernelSeconds = 0, longestTaskSeconds = 0;
    std::uint64_t graphHash = UINT64_C(14695981039346656037);
    std::uint64_t assignmentHash = UINT64_C(14695981039346656037);
    std::uint64_t searchCountersHash = UINT64_C(14695981039346656037);
};

inline Result solve(const Adjacency &adj, int rightCount, int requested, int threads)
{
    if (rightCount < 0 || threads < 1 || requested < 0 || requested > static_cast<int>(adj.size()))
        throw std::invalid_argument("legacy_components: invalid dimensions or thread count");
    // All four production legalizer call sites request all left nodes. A partial
    // global quota needs a separate allocation policy; never silently overmatch.
    if (requested != 0 && requested != static_cast<int>(adj.size()))
        throw std::invalid_argument("legacy_components requires a full-left matching request (or zero)");
    const auto started = Clock::now();
    Result result;
    result.leftToRight.assign(adj.size(), -1);
    hashWord(result.graphHash, adj.size());
    hashWord(result.graphHash, rightCount);
    hashWord(result.graphHash, requested);
    std::vector<std::vector<int>> inverse(rightCount);
    for (int u = 0; u < static_cast<int>(adj.size()); ++u)
    {
        hashWord(result.graphHash, adj[u].size());
        for (const auto &edge : adj[u])
        {
            if (edge.first < 0 || edge.first >= rightCount || !std::isfinite(edge.second) || edge.second <= 0.00001f)
                throw std::invalid_argument("legacy_components: invalid endpoint or native legacy cost");
            inverse[edge.first].push_back(u);
            hashWord(result.graphHash, edge.first);
            std::uint32_t bits;
            std::memcpy(&bits, &edge.second, sizeof(bits));
            hashWord(result.graphHash, bits);
            ++result.edges;
        }
    }
    if (requested == 0) return result;

    std::vector<Component> components;
    std::vector<int> leftOwner(adj.size(), -1), rightOwner(rightCount, -1), localRight(rightCount, -1);
    for (int root = 0; root < static_cast<int>(adj.size()); ++root)
    {
        if (leftOwner[root] >= 0) continue;
        const int id = components.size();
        components.emplace_back();
        auto &component = components.back();
        component.left.push_back(root);
        leftOwner[root] = id;
        for (std::size_t head = 0; head < component.left.size(); ++head)
        {
            const int u = component.left[head];
            component.edges += adj[u].size();
            for (const auto &edge : adj[u])
            {
                const int v = edge.first;
                if (rightOwner[v] >= 0) continue;
                rightOwner[v] = id;
                component.right.push_back(v);
                for (int next : inverse[v]) if (leftOwner[next] < 0)
                {
                    leftOwner[next] = id;
                    component.left.push_back(next);
                }
            }
        }
        // Preserve ascending global vertex order and each row's candidate order.
        // IDs and edge insertion order never depend on the worker count.
        std::sort(component.left.begin(), component.left.end());
        std::sort(component.right.begin(), component.right.end());
        for (int j = 0; j < static_cast<int>(component.right.size()); ++j)
            localRight[component.right[j]] = j;
        result.largestLeft = std::max(result.largestLeft, component.left.size());
        result.largestEdges = std::max(result.largestEdges, component.edges);
    }
    result.components = components.size();
    result.searches.resize(components.size());
    std::vector<std::exception_ptr> errors(components.size());
    result.preparationSeconds = seconds(started);
    const auto kernelStart = Clock::now();
    const int workers = std::max(1, std::min(threads, static_cast<int>(components.size())));
#pragma omp parallel for schedule(dynamic, 1) num_threads(workers)
    for (int id = 0; id < static_cast<int>(components.size()); ++id)
    {
        try
        {
            const auto &component = components[id];
            if (component.edges == 0) continue;
            auto &stats = result.searches[id];
            const int nl = component.left.size(), nr = component.right.size();
            const int source = nl + nr, sink = source + 1;
            const auto buildStart = Clock::now();
            MinCostFlow solver(nl + nr + 2, nl, source, sink);
            for (int i = 0; i < nl; ++i)
                for (const auto &edge : adj[component.left[i]])
                    solver.addEdge(i, nl + localRight[edge.first], 1, edge.second);
            for (int i = 0; i < nl; ++i) solver.addEdge(source, i, 1, 0);
            for (int j = 0; j < nr; ++j) solver.addEdge(nl + j, sink, 1, 0);
            stats.buildSeconds = seconds(buildStart);
            const auto solveStart = Clock::now();
            solver.calcMinCostFlow(source, sink, nl);
            stats.solveSeconds = seconds(solveStart);
            stats.visits = solver.diagVisits;
            stats.relax = solver.diagRelax;
            stats.spfa = solver.diagSPFA;
            stats.augment = solver.diagAugment;
            // Each task writes disjoint global left entries. No shared solver,
            // source/sink, queue, distance array or residual edge exists.
            for (int i = 0; i < nl; ++i)
                for (const auto *edge : solver.resGraph.adj[i])
                    if (edge->destination >= nl && edge->destination < nl + nr && edge->residualFlow == 0)
                    {
                        if (result.leftToRight[component.left[i]] >= 0)
                            throw std::logic_error("legacy_components: duplicate matched left");
                        result.leftToRight[component.left[i]] = component.right[edge->destination - nl];
                    }
        }
        catch (...) { errors[id] = std::current_exception(); }
    }
    result.kernelSeconds = seconds(kernelStart);
    std::vector<bool> used(rightCount, false);
    for (std::size_t id = 0; id < components.size(); ++id)
    {
        if (errors[id]) std::rethrow_exception(errors[id]);
        const auto &stats = result.searches[id];
        hashWord(result.searchCountersHash, id);
        for (auto count : {stats.visits, stats.relax, stats.spfa, stats.augment})
            hashWord(result.searchCountersHash, count);
        result.longestTaskSeconds = std::max(result.longestTaskSeconds, stats.buildSeconds + stats.solveSeconds);
    }
    // Deterministic collection: neither sums nor hashes follow task finish order.
    for (int u = 0; u < static_cast<int>(adj.size()); ++u)
    {
        const int v = result.leftToRight[u];
        hashWord(result.assignmentHash, v + 1);
        if (v < 0) continue;
        if (used[v]) throw std::logic_error("legacy_components: duplicate matched right");
        used[v] = true;
        float cost = std::numeric_limits<float>::infinity();
        for (const auto &edge : adj[u]) if (edge.first == v) cost = std::min(cost, edge.second);
        if (!std::isfinite(cost)) throw std::logic_error("legacy_components: matched non-candidate site");
        result.cost += cost;
        ++result.cardinality;
    }
    return result;
}
} // namespace amf_legacy_components
#endif
