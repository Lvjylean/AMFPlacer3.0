#ifndef AMF_HARD_RESOURCE_UTILS_H
#define AMF_HARD_RESOURCE_UTILS_H
#include "PlacementInfo.h"
#include <map>
#include <string>
#include <vector>

namespace HardResourceUtils
{
using CellSites = std::map<DesignInfo::DesignCell *, DeviceInfo::DeviceSite *>;
bool contiguousWithinSLR(const std::vector<DeviceInfo::DeviceSite *> &sites, int first, int count);
bool isCarryCascade(DesignInfo::DesignPin *driver, DesignInfo::DesignPin *sink);
void validateCascadeMacros(PlacementInfo *placement);
CellSites validatePlacement(PlacementInfo *placement);
void writePlacement(PlacementInfo *placement, const std::string &directory);
float resourcePitch(DeviceInfo *device, const std::string &siteType);
}
#endif
