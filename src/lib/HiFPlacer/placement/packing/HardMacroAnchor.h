#pragma once
#include "PlacementInfo.h"
#include <cmath>
#include <map>
#include <stdexcept>

// A hard site's coordinate belongs to a member, not to the PU anchor.
// Validate all member constraints before changing any placement state.
namespace hard_macro_anchor {
using Anchors = std::map<int, std::pair<float, float>>;
inline std::pair<float,float> fromMember(PlacementInfo::PlacementUnit *pu,
    DesignInfo::DesignCell *cell, float siteX, float siteY) {
    if (auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)) {
        siteX-=macro->getCellOffsetXInMacro(cell);
        siteY-=macro->getCellOffsetYInMacro(cell);
    }
    if (!std::isfinite(siteX) || !std::isfinite(siteY))
        throw std::runtime_error("Nonfinite hard-macro anchor: "+cell->getName());
    return {siteX,siteY};
}
inline bool equal(std::pair<float,float> a, std::pair<float,float> b) {
    return std::fabs(a.first-b.first)<1e-4f && std::fabs(a.second-b.second)<1e-4f;
}
inline void accumulate(Anchors &anchors, PlacementInfo::PlacementUnit *pu,
    DesignInfo::DesignCell *cell, float x, float y) {
    auto anchor=fromMember(pu,cell,x,y);
    if (pu->isLocked() && !equal(anchor,{pu->X(),pu->Y()}))
        throw std::runtime_error("Locked hard-macro site disagrees with anchor: "+cell->getName());
    auto result=anchors.emplace(pu->getId(),anchor);
    if (!result.second && !equal(result.first->second,anchor))
        throw std::runtime_error("Inconsistent member sites for hard macro: "+pu->getName());
}
inline void verify(PlacementInfo::PlacementUnit *pu, DesignInfo::DesignCell *cell,
                   float x, float y) {
    if (!equal(fromMember(pu,cell,x,y),{pu->X(),pu->Y()}))
        throw std::runtime_error("Hard-macro coordinate disagrees with exported site: "+cell->getName());
}
}
