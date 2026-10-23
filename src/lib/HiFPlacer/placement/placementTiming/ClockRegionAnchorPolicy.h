#ifndef AMF_CLOCK_REGION_ANCHOR_POLICY_H
#define AMF_CLOCK_REGION_ANCHOR_POLICY_H

#include "HierarchicalBoundaryPolicy.h"
#include <cmath>
#include <limits>
#include <vector>

namespace hierarchical_boundary {
struct ClockRegionCenter
{
    int column, row;
    float x, y;
};
struct ClockRegionAnchor
{
    float x = 0, y = 0;
    int column = -1, row = -1, pullY = 0;
};

// Select once per cluster generation. The average includes the same real
// cells as the vote, including fixed/locked cells; never recompute per PU.
// Outer dies use their only neighboring seam. At an exact middle-die tie,
// choose its lower seam deterministically.
template <typename Regions>
bool selectClockRegionAnchor(const Regions &regions, int target, bool guideX, bool guideY,
                             int pullX, double meanY, const std::vector<ClockRegionCenter> &centers,
                             ClockRegionAnchor &anchor)
{
    anchor = ClockRegionAnchor{};
    if (target < 0 || target >= int(regions.size()) || !std::isfinite(meanY)) return false;
    const auto &r = regions[target];
    float slrLeft = r.x0, slrRight = r.x1;
    for (const auto &side : regions) if (side.slr == r.slr)
    { slrLeft = std::min(slrLeft, side.x0); slrRight = std::max(slrRight, side.x1); }
    if (guideY) {
        anchor.pullY = slrPullDirection(regions, target);
        if (!anchor.pullY) anchor.pullY = meanY <= (double(r.y0) + r.y1) * .5 ? 1 : -1;
    }
    if (guideX && pullX != -1 && pullX != 1) return false;
    bool haveX = !guideX, haveY = !guideY;
    for (const auto &cr : centers) {
        if (!std::isfinite(cr.x) || !std::isfinite(cr.y) || cr.row < 0 || cr.column < 0 ||
            cr.y <= r.y0 || cr.y >= r.y1 || cr.x <= slrLeft || cr.x >= slrRight) continue;
        if (guideY && (!haveY || (anchor.pullY > 0 ? cr.y < anchor.y : cr.y > anchor.y))) {
            anchor.y = cr.y; anchor.row = cr.row; haveY = true;
        }
        // Left of HPIO: rightmost column. Right of HPIO: leftmost column.
        if (guideX && cr.x > r.x0 && cr.x < r.x1 &&
            (!haveX || (pullX < 0 ? cr.x > anchor.x : cr.x < anchor.x))) {
            anchor.x = cr.x; anchor.column = cr.column; haveX = true;
        }
    }
    return haveX && haveY;
}
}
#endif
