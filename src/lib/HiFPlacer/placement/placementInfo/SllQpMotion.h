#pragma once
#include "SllVirtualModel.h"

namespace amf_sll_plan {
// Match real-PU writeback: an unweighted 2D displacement bound in AMF
// coordinates. Non-positive limits disable clipping. Apply BEFORE projection;
// snapping to a legal tile is intentionally not subject to this bound.
inline Point limitedQpPosition(Point origin,Point solved,double limit) {
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||
       !std::isfinite(solved.x)||!std::isfinite(solved.y)||!std::isfinite(limit))
        throw std::runtime_error("Non-finite SLL QP displacement input");
    if(limit<=0)return solved;
    const double dx=solved.x-origin.x,dy=solved.y-origin.y;
    const double distance=std::hypot(dx,dy);
    if(!std::isfinite(distance))throw std::runtime_error("Non-finite SLL QP displacement");
    if(distance<=limit)return solved;
    const double scale=limit/distance;
    return {origin.x+dx*scale,origin.y+dy*scale};
}
}
