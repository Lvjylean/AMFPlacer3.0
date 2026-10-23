#pragma once
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace AMFRegionColumnDP
{
struct Group
{
    std::vector<int> items;
    std::vector<int> sites;
};

// Input items are already in preferred Y order. Exact site ownership is
// disjoint, so there is no ordering or capacity dependency between regions.
inline std::map<int, Group> partition(const std::vector<int> &itemRegions,
                                     const std::vector<int> &siteRegions)
{
    std::map<int, Group> groups;
    for (int i = 0; i < static_cast<int>(itemRegions.size()); ++i)
        groups[itemRegions[i]].items.push_back(i);
    for (int j = 0; j < static_cast<int>(siteRegions.size()); ++j)
    {
        auto it = groups.find(siteRegions[j]);
        if (it != groups.end()) it->second.sites.push_back(j);
    }
    return groups;
}

// Preserve order within one region, while allowing holes in its site mask.
// legal(i, first, height) must check actual physical adjacency and all hard
// constraints; adjacent indices in a filtered mask are NOT necessarily adjacent
// physical sites. cost(i, first) is evaluated only for legal complete macros.
template <typename Legal, typename Cost>
bool solveOrdered(const std::vector<int> &heights, int siteCount,
                  Legal legal, Cost cost, std::vector<int> &bottoms)
{
    bottoms.clear();
    if (siteCount < 0) throw std::invalid_argument("Negative region site count");
    if (heights.empty()) return true;
    long long demand = 0;
    for (int h : heights)
    {
        if (h <= 0) throw std::invalid_argument("Nonpositive macro height");
        demand += h;
    }
    if (demand > siteCount) return false;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> previous(siteCount + 1, 0.0), current(siteCount + 1, inf);
    std::vector<std::vector<unsigned char>> take(heights.size(),
                                                std::vector<unsigned char>(siteCount + 1, 0));
    for (int i = 0; i < static_cast<int>(heights.size()); ++i)
    {
        current.assign(siteCount + 1, inf);
        const int h = heights[i];
        for (int end = 1; end <= siteCount; ++end)
        {
            current[end] = current[end - 1];
            const int first = end - h;
            if (first < 0 || !std::isfinite(previous[first]) || !legal(i, first, h)) continue;
            const double delta = cost(i, first);
            if (!std::isfinite(delta)) throw std::runtime_error("Nonfinite region placement cost");
            const double value = previous[first] + delta;
            if (value < current[end])
            {
                current[end] = value;
                take[i][end] = 1;
            }
        }
        previous.swap(current);
    }
    if (!std::isfinite(previous[siteCount])) return false;
    bottoms.resize(heights.size());
    int end = siteCount;
    for (int i = static_cast<int>(heights.size()) - 1; i >= 0; --i)
    {
        while (end > 0 && !take[i][end]) --end;
        if (end < heights[i]) throw std::logic_error("Invalid region DP traceback");
        bottoms[i] = end - heights[i];
        end = bottoms[i];
    }
    return true;
}
}
