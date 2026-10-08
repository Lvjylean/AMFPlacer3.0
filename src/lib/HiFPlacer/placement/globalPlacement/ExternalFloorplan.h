#ifndef AMF_EXTERNAL_FLOORPLAN_H
#define AMF_EXTERNAL_FLOORPLAN_H

#include "PlacementInfo.h"
#include <map>
#include <string>

// A one-shot initializer. It never installs region constraints, locks PUs,
// reserves sites, changes connectivity, or retains pointers after packing.
class ExternalFloorplan
{
  public:
    static bool enabled(const std::map<std::string, std::string> &cfg);
    static void initialize(PlacementInfo *placement, const std::map<std::string, std::string> &cfg);
};
#endif

