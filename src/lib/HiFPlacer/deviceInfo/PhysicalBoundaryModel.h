#ifndef AMF_PHYSICAL_BOUNDARY_MODEL_H
#define AMF_PHYSICAL_BOUNDARY_MODEL_H
#include <array>
#include <map>
#include <string>
#include <vector>
#include <utility>
class DeviceInfo;

// Capacities describe shared resources; MLUT is a subset of LUT, and RAMB36
// consumes two RAMB18 slots. They must never be summed as independent resources.
class PhysicalBoundaryModel
{
  public:
    enum Resource { LUT, FF, MLUT, CARRY, MUX7, MUX8, MUX9, DSP, BRAM18, BRAM36, URAM, ResourceCount };
    using Resources = std::array<double, ResourceCount>;
    struct Region
    {
        int id = -1, slr = -1;
        float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        Resources capacity{};
        bool contains(float x, float y) const { return x >= x0 && x < x1 && y > y0 && y <= y1; }
    };
    struct Boundary
    {
        std::string id, kind;
        char orientation = 'X';
        float coordinate = 0, low = 0, high = 0, penaltyNs = 0;
        bool active = false;
    };
    explicit PhysicalBoundaryModel(const std::string &path, DeviceInfo *device = nullptr);
    const std::vector<Region> &getRegions() const { return regions; }
    const std::vector<Boundary> &getBoundaries() const { return boundaries; }
    const std::map<std::string, std::string> &getMetadata() const { return metadata; }
    int regionAt(float x, float y) const;
    int siteRegion(const std::string &name) const;
    bool nearestSlice(int region, float x, float y, bool memory, float left, float right,
                      float bottom, float top, float &outX, float &outY) const;
    std::pair<float, float> project(int region, float x, float y, float marginX = 0.1f,
                                   float marginY = 0.1f) const;
    int crossingCount(float x0, float y0, float x1, float y1, const std::string &kind) const;
    float penalty(float x0, float y0, float x1, float y1, float slrPenaltyNs) const;
  private:
    static bool crosses(const Boundary &boundary, float x0, float y0, float x1, float y1);
    std::vector<Region> regions;
    std::vector<Boundary> boundaries;
    std::map<std::string, std::string> metadata;
    std::map<std::string, int> siteRegions;
    std::vector<std::map<float, std::vector<float>>> sliceColumns, memoryColumns;
};
#endif
