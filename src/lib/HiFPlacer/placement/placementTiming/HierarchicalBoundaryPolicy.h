#ifndef AMF_HIERARCHICAL_BOUNDARY_POLICY_H
#define AMF_HIERARCHICAL_BOUNDARY_POLICY_H
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace hierarchical_boundary {
struct Selection
{
    int slr = -1, region = -1;
    double total = 0, slrVotes = 0, sideVotes = 0;
};

// Votes count instances, not PUs. A side majority is conditional on the chosen
// SLR; a side tie retains the SLR decision rather than rejecting the cluster.
template <typename Regions>
Selection select(const Regions &regions, const std::map<int, double> &votes)
{
    Selection result;
    std::map<int, double> slrVotes;
    for (auto entry : votes)
    {
        if (entry.first < 0 || entry.first >= int(regions.size()) || entry.second <= 0) continue;
        slrVotes[regions[entry.first].slr] += entry.second;
        result.total += entry.second;
    }
    for (auto entry : slrVotes)
        if (entry.second > result.total * 0.5)
        { result.slr = entry.first; result.slrVotes = entry.second; break; }
    if (result.slr < 0) return result;
    for (auto entry : votes)
        if (entry.first >= 0 && entry.first < int(regions.size()) &&
            regions[entry.first].slr == result.slr && entry.second > result.slrVotes * 0.5)
        { result.region = entry.first; result.sideVotes = entry.second; break; }
    return result;
}

// Paper Eq. (2)-(3): expand about the current span's midpoint, then translate
// and clip the interval to the available physical region (never across a cut).
inline std::pair<float, float> expandedRange(float low, float high, float lower, float upper,
                                            double incoming, double inside)
{
    if (inside <= 0 || incoming <= 0 || high - low < 0.01f || upper <= lower)
        return {low, high};
    double width = std::min(double(upper - lower), (high - low) * (1.0 + incoming / inside));
    float start = std::max(lower, std::min(float((low + high - width) * 0.5), float(upper - width)));
    return {start, float(start + width)};
}
// direction is chosen once, not recomputed after a PU passes the anchor.
// This is the upstream one-sided blockage rule (including its DSPCritical gate).
inline float anchorWeight(float beta, float directedDistance, size_t nets, bool dspCritical = false)
{
    if (directedDistance <= 0 || beta <= 0 || nets == 0) return 0;
    if (directedDistance > 6) return beta * std::pow(nets, 1.1);
    if (dspCritical) return 0;
    if (directedDistance > 3) return beta * nets;
    return (directedDistance / 3) * beta * nets;
}
}
#endif
