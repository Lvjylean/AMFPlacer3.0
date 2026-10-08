#ifndef AMF_CLOCK_RESOURCE_CAPACITY_H
#define AMF_CLOCK_RESOURCE_CAPACITY_H

#include <map>
#include <string>

// Nominal architectural capacities, not remaining capacity after placement or
// routing. The BBox limit is a placement model limit, not a sum of track types.
struct ClockRegionCapacity
{
    int slr = 0;
    int hroute = -1, hdistr = -1, vroute = -1, vdistr = -1;
    int halfColumnLimit = 12;
    int bboxClockLimit = 24;
};

class ClockResourceCapacityTable
{
  public:
    // expectedRegions maps every region in the loaded device archive to its SLR.
    // Archive SHA256 is provenance here; the Python entry point validates bytes.
    ClockResourceCapacityTable(const std::string &path, const std::string &expectedPart,
                               const std::map<std::string, int> &expectedRegions);
    const std::map<std::string, std::string> &getMetadata() const { return metadata; }
    const std::map<std::string, ClockRegionCapacity> &getRegions() const { return regions; }
    const std::string &getPath() const { return path; }

  private:
    std::string path;
    std::map<std::string, std::string> metadata;
    std::map<std::string, ClockRegionCapacity> regions;
};

#endif
