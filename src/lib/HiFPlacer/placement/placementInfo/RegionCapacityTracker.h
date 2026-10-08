#ifndef AMF_REGION_CAPACITY_TRACKER_H
#define AMF_REGION_CAPACITY_TRACKER_H
#include "PhysicalBoundaryModel.h"
#include "PlacementInfo.h"
#include <map>
#include <string>
#include <vector>

class RegionCapacityTracker
{
  public:
    using Resources = PhysicalBoundaryModel::Resources;
    using PU = PlacementInfo::PlacementUnit;
    explicit RegionCapacityTracker(PlacementInfo *placement);
    // Also used by small deterministic native tests without a full netlist.
    RegionCapacityTracker(const std::vector<Resources> &capacity, const std::vector<Resources> &occupied);
    bool reserve(int key, const std::vector<Resources> &delta, bool commit, std::string *reason = nullptr);
    void release(int key);
    bool assign(const std::vector<PU *> &units, int targetRegion, bool commit, std::string *reason = nullptr);
    bool assignTargets(const std::map<PU *, int> &targets, bool commit, std::string *reason = nullptr);
    const std::vector<Resources> &getUsage() const { return used; }
    const std::vector<Resources> &getCapacity() const { return capacity; }
    Resources cellDemand(DesignInfo::DesignCell *cell, bool memoryMacro = false) const;
    static bool fits(const Resources &used, const Resources &limit);
  private:
    std::vector<Resources> currentDemand(PU *unit) const;
    PlacementInfo *placement = nullptr;
    PhysicalBoundaryModel *model = nullptr;
    std::vector<Resources> capacity, used;
    std::map<int, std::vector<Resources>> reservations;
};
#endif
