#pragma once
#include <map>
#include <stdexcept>
#include <string>

namespace amf_config {
using FeatureMap = std::map<std::string, std::string>;
inline bool feature(const FeatureMap &c, const std::string &key, bool fallback=false) {
    auto i=c.find(key); if(i==c.end()) return fallback;
    if(i->second!="true" && i->second!="false") throw std::invalid_argument(key+" must be true/false");
    return i->second=="true";
}
inline bool sllRoute(const FeatureMap &c) {
    return feature(c,"SllPlanningEnable") && feature(c,"SllRouteConstraintEnable",true);
}
inline bool sllTiming(const FeatureMap &c) {
    return feature(c,"SllPlanningEnable") && feature(c,"SllTimingCorrectionEnable",true);
}
inline void validatePlacementFeatures(const FeatureMap &c) {
    for(const char *key:{"SllQuantityEnable","SllDynamicTauEnable","SllPlanningEnable",
        "SllCapacityOptimizationEnable","SllTimingCorrectionEnable","SllRouteConstraintEnable",
        "DieFirstSpreadingEnable","TimingDrivenPlacementEnable","DetailedDiagnosticsEnable",
        "IterativeMacroLegalizationEnable","SllTauCompletionRequired"}) feature(c,key);
    for(const char *key:{"SllTimingCorrectionEnable","SllRouteConstraintEnable"})
        if(feature(c,key) && !feature(c,"SllPlanningEnable"))
            throw std::invalid_argument(std::string(key)+" requires SllPlanningEnable=true");
    auto policy=c.find("SllTauPolicy");
    if(policy!=c.end() && policy->second!="adaptive" && policy->second!="periodic" && policy->second!="drop-or-timeout")
        throw std::invalid_argument("SllTauPolicy must be adaptive/periodic/drop-or-timeout");
    auto region=c.find("SllRegionPolicy");
    if(region!=c.end() && region->second!="preserve" && region->second!="release-single")
        throw std::invalid_argument("SllRegionPolicy must be preserve/release-single");
}
} // namespace amf_config
