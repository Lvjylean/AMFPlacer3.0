#include "../../utils/RuntimeProfiler.h"
#include "PhysicalBoundaryModel.h"
#include "DeviceInfo.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error("Physical boundary model: " + message);
}
bool finite(float v) { return std::isfinite(v); }
}
PhysicalBoundaryModel::PhysicalBoundaryModel(const std::string &path, DeviceInfo *device)
{
    AMF_PROFILE_FUNCTION("input_device");
    std::ifstream input(path);
    require(input.good(), "cannot read " + path);
    std::string line;
    std::getline(input, line);
    require(line == "AMF_PHYSICAL_STRUCTURE\t1", "unsupported schema");
    std::unordered_map<std::string, DeviceInfo::DeviceSite *> expected;
    if (device)
        for (auto site : device->getSites()) expected.emplace(site->getName(), site);
    std::set<std::string> ids;
    size_t count = 0;
    while (std::getline(input, line))
    {
        if (line.empty()) continue;
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "META")
        {
            std::string key, value;
            require(bool(row >> key >> value), "invalid metadata");
            require(metadata.emplace(key, value).second, "duplicate metadata: " + key);
        }
        else if (tag == "REGION")
        {
            Region r;
            require(bool(row >> r.id >> r.slr >> r.x0 >> r.x1 >> r.y0 >> r.y1), "invalid region");
            require(r.id == int(regions.size()) && r.slr >= 0 && finite(r.x0) && finite(r.x1) &&
                        finite(r.y0) && finite(r.y1) && r.x0 < r.x1 && r.y0 < r.y1, "invalid region geometry");
            for (auto &value : r.capacity)
                require(bool(row >> value) && std::isfinite(value) && value >= 0, "invalid region capacity");
            for (const auto &old : regions)
                require(!(r.x0 < old.x1 && old.x0 < r.x1 && r.y0 < old.y1 && old.y0 < r.y1),
                        "overlapping regions");
            regions.push_back(r);
            sliceColumns.emplace_back(); memoryColumns.emplace_back();
        }
        else if (tag == "BOUNDARY")
        {
            Boundary b; int active;
            require(bool(row >> b.id >> b.kind >> b.orientation >> b.coordinate >> b.low >> b.high >>
                         b.penaltyNs >> active), "invalid boundary");
            require(ids.insert(b.id).second, "duplicate boundary ID");
            require((b.kind == "SLR" || b.kind == "IO" || b.kind == "LOCAL_IP") &&
                        (b.orientation == 'X' || b.orientation == 'Y') && finite(b.coordinate) &&
                        finite(b.low) && finite(b.high) && b.low < b.high && finite(b.penaltyNs) &&
                        b.penaltyNs >= 0 && (active == 0 || active == 1), "invalid boundary fields");
            b.active = active;
            require(!(b.active && b.kind == "LOCAL_IP"), "unproven local IP cannot be enabled as a cut");
            for (const auto &old : boundaries)
                require(!(b.active && old.active && b.orientation == old.orientation &&
                          std::fabs(b.coordinate-old.coordinate) < 1e-5 &&
                          b.low < old.high && old.low < b.high), "overlapping duplicate cut");
            boundaries.push_back(b);
        }
        else if (tag == "SITE")
        {
            std::string name, type; float x,y; int slr, prohibited, region;
            require(bool(row >> name >> type >> x >> y >> slr >> prohibited >> region), "invalid site");
            require(finite(x) && finite(y) && (prohibited == 0 || prohibited == 1) &&
                    region >= 0 && region < int(regions.size()), "invalid site fields");
            require(regions[region].slr == slr && regions[region].contains(x,y), "site region mismatch: " + name);
            require(siteRegions.emplace(name, region).second, "duplicate site: " + name);
            if (device)
            {
                auto it = expected.find(name);
                require(it != expected.end(), "site absent from fabric: " + name);
                auto site = it->second;
                require(site->getSiteType() == type && std::fabs(site->X()-x)<1e-4 &&
                        std::fabs(site->Y()-y)<1e-4 && site->getSLRId()==slr &&
                        site->isOccupied()==bool(prohibited), "fabric mismatch: " + name);
            }
            if (!prohibited && (type=="SLICEL" || type=="SLICEM"))
            {
                sliceColumns[region][x].push_back(y);
                if(type=="SLICEM") memoryColumns[region][x].push_back(y);
            }
            ++count;
        }
        else require(false, "unknown record: " + tag);
        std::string extra;
        require(!(row >> extra), "trailing fields in " + tag);
    }
    require(!regions.empty() && count > 0, "empty geometry");
    for (const auto &key : {"part","rules_version","fabric_sha256","coordinates_sha256",
                           "rules_sha256","raw_sites_sha256","raw_tiles_sha256","site_count"})
        require(metadata.count(key), "missing metadata: " + std::string(key));
    require(count == std::stoull(metadata.at("site_count")), "site count mismatch");
    if (device) require(count == expected.size(), "incomplete fabric coverage");
    for(auto *columns : {&sliceColumns,&memoryColumns})
        for(auto &region:*columns) for(auto &column:region)
            std::sort(column.second.begin(),column.second.end());
}
int PhysicalBoundaryModel::regionAt(float x, float y) const
{
    if (!finite(x) || !finite(y)) return -1;
    for (const auto &r : regions) if (r.contains(x,y)) return r.id;
    // During analytical placement points may be slightly outside the fabric.
    float best = std::numeric_limits<float>::max(); int result = -1;
    for (const auto &r : regions)
    {
        auto p = project(r.id,x,y);
        float d = std::fabs(x-p.first)+std::fabs(y-p.second);
        if (d < best) { best=d; result=r.id; }
    }
    return result;
}
int PhysicalBoundaryModel::siteRegion(const std::string &name) const
{
    auto it=siteRegions.find(name);
    return it==siteRegions.end() ? -1 : it->second;
}
std::pair<float,float> PhysicalBoundaryModel::project(int id,float x,float y,float mx,float my) const
{
    const auto &r=regions.at(id);
    mx=std::max(0.0f,mx); my=std::max(0.0f,my);
    if (2*mx >= r.x1-r.x0 || 2*my >= r.y1-r.y0)
        throw std::runtime_error("Physical boundary region is too small for the macro");
    return {std::max(r.x0+mx,std::min(r.x1-mx,x)), std::max(r.y0+my,std::min(r.y1-my,y))};
}
bool PhysicalBoundaryModel::crosses(const Boundary &b,float x0,float y0,float x1,float y1)
{
    if (!b.active) return false;
    float a=b.orientation=='X'?x0:y0, c=b.orientation=='X'?x1:y1;
    float p=b.orientation=='X'?y0:x0, q=b.orientation=='X'?y1:x1;
    if (b.kind=="SLR")
        return ((a <= b.coordinate)!=(c <= b.coordinate));
    // IO endpoints on the band are not full through-crossings. Both ends must
    // be within the confirmed continuous extent, otherwise geometry is uncertain.
    return ((a < b.coordinate && c > b.coordinate) || (c < b.coordinate && a > b.coordinate)) &&
            p >= b.low && p <= b.high && q >= b.low && q <= b.high;
}
int PhysicalBoundaryModel::crossingCount(float x0,float y0,float x1,float y1,const std::string &kind) const
{
    int n=0;
    for (const auto &b : boundaries) if (b.kind==kind && crosses(b,x0,y0,x1,y1)) ++n;
    return n;
}
float PhysicalBoundaryModel::penalty(float x0,float y0,float x1,float y1,float slrPenaltyNs) const
{
    float result=0;
    for (const auto &b : boundaries)
        if (crosses(b,x0,y0,x1,y1)) result += b.kind=="SLR" ? slrPenaltyNs : b.penaltyNs;
    return result;
}


bool PhysicalBoundaryModel::nearestSlice(int id,float x,float y,bool memory,float left,float right,
                                         float bottom,float top,float &outX,float &outY) const
{
    const auto &columns=(memory?memoryColumns:sliceColumns).at(id);
    float best=std::numeric_limits<float>::max(); bool found=false;
    for(auto it=columns.lower_bound(left);it!=columns.end() && it->first<=right;++it)
    {
        const auto &ys=it->second;
        auto lo=std::lower_bound(ys.begin(),ys.end(),bottom);
        auto hi=std::upper_bound(ys.begin(),ys.end(),top);
        if(lo==hi)continue;
        auto upper=std::lower_bound(lo,hi,y);
        for(int side=0;side<2;++side)
        {
            auto cand=upper;
            if(side==0){if(cand==hi)continue;}
            else {if(cand==lo)continue;--cand;}
            float dist=std::fabs(it->first-x)+std::fabs(*cand-y);
            if(dist<best){best=dist;outX=it->first;outY=*cand;found=true;}
        }
    }
    return found;
}
