#include "ExternalFloorplan.h"
#include "../../../utils/RuntimeProfiler.h"
#include "sysInfo.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace
{
using PU = PlacementInfo::PlacementUnit;
using Cell = DesignInfo::DesignCell;
using Site = DeviceInfo::DeviceSite;
enum Kind { Logic, Memory, DSP, BRAM18, BRAM36, URAM, KindCount };
const char *kindNames[] = {"logic", "memory", "dsp", "bram18", "bram36", "uram"};
struct Pool
{
    std::vector<Site *> sites;
    double demand = 0;
    size_t cursor = 0;
};
struct Region
{
    std::string name;
    float centerX = 0, centerY = 0;
    std::array<Pool, KindCount> pools;
};
struct Member { Cell *cell; float x = 0, y = 0; };
struct Unit
{
    PU *pu;
    std::vector<Member> members;
    std::map<int, double> votes;
    std::string sortName;
    Kind kind = Logic;
    float offsetX = 0, offsetY = 0;
    double demand = 1;
    bool mixed = false;
};
std::vector<std::string> fields(std::string line)
{
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string value;
    while (std::getline(in, value, '\t')) out.push_back(value);
    return out;
}
std::ifstream open(const std::string &path)
{
    std::ifstream in(path);
    if (!in) throw std::runtime_error("External floorplan: cannot read " + path);
    return in;
}
int positiveInt(const std::string &s, const std::string &key)
{
    size_t end = 0;
    int value = std::stoi(s, &end);
    if (end != s.size() || value <= 0) throw std::runtime_error("External floorplan: invalid " + key);
    return value;
}
Kind kind(Cell *cell)
{
    if (cell->isURAM()) return URAM;
    if (cell->isDSP()) return DSP;
    if (cell->isBRAM())
        return cell->getOriCellType() == DesignInfo::CellType_RAMB36E2 || cell->getOriCellType() == DesignInfo::CellType_FIFO36E2 ? BRAM36 : BRAM18;
    if (cell->isLUTRAM() || cell->isShifter() || cell->originallyIsLUTRAM() || cell->originallyIsShifter())
        return Memory;
    if (cell->isLUT() || cell->isFF() || cell->isCarry() ||
        cell->getCellType() == DesignInfo::CellType_MUXF7 ||
        cell->getCellType() == DesignInfo::CellType_MUXF8)
        return Logic;
    throw std::runtime_error("External floorplan: unsupported movable primitive at " + cell->getName());
}
}

bool ExternalFloorplan::enabled(const std::map<std::string, std::string> &cfg)
{
    auto has = [&](const char *key) { auto it = cfg.find(key); return it != cfg.end() && !it->second.empty(); };
    bool members = has("external floorplan membership file"), regions = has("external floorplan regions file");
    if (members != regions) throw std::runtime_error("External floorplan requires both membership and regions files");
    return members;
}

void ExternalFloorplan::initialize(PlacementInfo *p, const std::map<std::string, std::string> &cfg)
{
    AMF_PROFILE_FUNCTION("external_floorplan_initialization");
    if (!enabled(cfg)) throw std::runtime_error("External floorplan input is absent");
    auto random = cfg.find("RandomInitialPlacement");
    if (random != cfg.end() && random->second == "true")
        throw std::runtime_error("External floorplan conflicts with RandomInitialPlacement");
    print_status("External floorplan initialization: bypass internal partitioning and SA");

    auto device = p->getDeviceInfo();
    std::vector<Region> regions;
    std::map<std::string, int> regionId;
    for (size_t y = 0; y < device->getClockRegions().size(); ++y)
        for (size_t x = 0; x < device->getClockRegions()[y].size(); ++x)
        {
            auto cr = device->getClockRegions()[y][x];
            std::string name = "X" + std::to_string(x) + "Y" + std::to_string(y);
            regionId[name] = regions.size();
            Region region; region.name = name;
            region.centerX = (cr->getLeft() + cr->getRight()) / 2;
            region.centerY = (cr->getBottom() + cr->getTop()) / 2;
            regions.push_back(region);
        }
    std::map<std::string, std::set<int>> modules;
    auto rin = open(cfg.at("external floorplan regions file"));
    std::string line;
    if (!std::getline(rin, line) || fields(line) != std::vector<std::string>{"module", "clock_regions"})
        throw std::runtime_error("External floorplan: invalid regions header");
    const std::regex moduleName("[A-Za-z0-9_.-]+");
    while (std::getline(rin, line))
    {
        auto f = fields(line);
        if (f.size() != 2 || !std::regex_match(f[0], moduleName) || modules.count(f[0]))
            throw std::runtime_error("External floorplan: invalid or duplicate module row");
        std::istringstream values(f[1]); std::string cr; std::set<int> ids;
        while (values >> cr)
        {
            auto it = regionId.find(cr);
            if (it == regionId.end() || !ids.insert(it->second).second)
                throw std::runtime_error("External floorplan: unknown or duplicate CR " + cr);
        }
        if (ids.empty()) throw std::runtime_error("External floorplan: empty module region " + f[0]);
        modules.emplace(f[0], std::move(ids));
    }
    if (modules.empty()) throw std::runtime_error("External floorplan has no modules");
    std::unordered_map<std::string, Cell *> realCells;
    for (auto cell : p->getDesignInfo()->getCells())
        if (!cell->isVirtualCell()) realCells.emplace(cell->getName(), cell);
    std::unordered_map<Cell *, std::string> membership;
    std::map<std::string, std::array<size_t, 3>> sizes;
    auto min = open(cfg.at("external floorplan membership file"));
    if (!std::getline(min, line) || fields(line) != std::vector<std::string>{"cell", "module", "ref"})
        throw std::runtime_error("External floorplan: invalid membership header");
    while (std::getline(min, line))
    {
        auto f = fields(line);
        if (f.size() != 3 || !modules.count(f[1]))
            throw std::runtime_error("External floorplan: invalid membership or unknown module");
        auto it = realCells.find(f[0]);
        if (it == realCells.end() || p->getDesignInfo()->DesignCellTypeStr[it->second->getOriCellType()] != f[2])
            throw std::runtime_error("External floorplan: unknown cell or primitive mismatch " + f[0]);
        if (!membership.emplace(it->second, f[1]).second)
            throw std::runtime_error("External floorplan: duplicate cell " + f[0]);
        auto &n = sizes[f[1]];
        ++n[0]; n[1] += it->second->isDSP(); n[2] += it->second->isBRAM();
    }
    if (membership.size() != realCells.size())
        throw std::runtime_error("External floorplan: incomplete cell coverage");
    if (sizes.size() != modules.size()) throw std::runtime_error("External floorplan: module without members");

    // Use actual exported site coordinates and CR identities, never equal-width bins.
    // Sites remain unreserved: this is an initial placement, not a legal packing.
    for (auto site : device->getSites())
    {
        if (site->isOccupied() || site->isMapped()) continue;
        auto cr = regionId.at("X" + std::to_string(site->getClockRegionX()) + "Y" + std::to_string(site->getClockRegionY()));
        auto &pool = regions[cr].pools;
        auto type = site->getSiteType();
        if (type == "SLICEL" || type == "SLICEM") pool[Logic].sites.push_back(site);
        if (type == "SLICEM") pool[Memory].sites.push_back(site);
        if (type == "DSP48E2" || type == "DSP48E1") pool[DSP].sites.push_back(site);
        // The AMF fabric represents 36K BRAM as a lower RAMBFIFO18 plus
        // an upper RAMB181. A real RAMB36 macro anchors at the lower half.
        if (type == "RAMB18E2" || type == "RAMB18E1" || type == "FIFO18E2" ||
            type == "RAMBFIFO18" || type == "RAMB181") pool[BRAM18].sites.push_back(site);
        if (type == "RAMB36E2" || type == "RAMB36E1" || type == "FIFO36E2" || type == "RAMBFIFO18") pool[BRAM36].sites.push_back(site);
        if (type == "URAM288" || type == "URAM288_BASE") pool[URAM].sites.push_back(site);
    }
    for (auto &region : regions)
        for (auto &pool : region.pools)
            std::sort(pool.sites.begin(), pool.sites.end(), [](Site *a, Site *b) {
                return std::make_tuple(a->X(), a->Y(), a->getName()) < std::make_tuple(b->X(), b->Y(), b->getName());
            });

    std::vector<Unit> units;
    size_t fixed = 0, mixed = 0;
    for (auto pu : p->getPlacementUnits())
    {
        if (pu->isFixed() || pu->isLocked()) { ++fixed; continue; }
        Unit u; u.pu = pu;
        if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
            for (int i = 0; i < macro->getNumOfCells(); ++i)
            {
                auto cell = macro->getCell(i);
                if (!cell->isVirtualCell())
                    u.members.push_back({cell, macro->getCellOffsetXInMacro(cell), macro->getCellOffsetYInMacro(cell)});
            }
        else if (auto single = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu))
            u.members.push_back({single->getCell(), 0, 0});
        if (u.members.empty()) throw std::runtime_error("External floorplan: movable PU has no real members");
        std::set<std::string> unitModules;
        u.sortName = u.members.front().cell->getName();
        for (const auto &member : u.members)
        {
            auto name = membership.at(member.cell);
            unitModules.insert(name);
            for (int r : modules.at(name)) u.votes[r] += 1;
            u.sortName = std::min(u.sortName, member.cell->getName());
            auto k = kind(member.cell);
            if (k > u.kind) { u.kind = k; u.offsetX = member.x; u.offsetY = member.y; }
        }
        u.mixed = unitModules.size() > 1; mixed += u.mixed;
        if (u.kind == Logic || u.kind == Memory) u.demand = std::max(1, pu->getWeight());
        else
        {
            u.demand = 0;
            for (const auto &member : u.members) u.demand += kind(member.cell) == u.kind;
        }
        units.push_back(std::move(u));
    }
    // Place scarce resources first; stable cell names make the result independent of map/pointer order.
    std::sort(units.begin(), units.end(), [](const Unit &a, const Unit &b) {
        if (a.kind != b.kind) return a.kind > b.kind;
        return a.sortName < b.sortName;
    });
    size_t fallbacks = 0, outside = 0, macroAdjusted = 0;
    std::map<std::string, std::pair<size_t, size_t>> moduleInside;
    std::vector<std::tuple<PU *, float, float>> seeds;
    std::ostringstream locations;
    locations << "pu_id\tname\tresource\tx\ty\tmixed_membership\tresource_fallback\n" << std::setprecision(9);
    for (auto &u : units)
    {
        int selected = -1;
        double best = std::numeric_limits<double>::infinity();
        for (const auto &vote : u.votes)
        {
            auto &pool = regions[vote.first].pools[u.kind];
            if (pool.sites.empty()) continue;
            double score = pool.demand / pool.sites.size() +
                           (1.0 - vote.second / u.members.size());
            if (score < best) { best = score; selected = vote.first; }
        }
        bool fallback = selected < 0;
        if (fallback)
        {
            ++fallbacks;
            // Resource absence in a preferred region must not make a soft floorplan infeasible.
            float tx = 0, ty = 0;
            for (const auto &vote : u.votes)
            {
                tx += regions[vote.first].centerX;
                ty += regions[vote.first].centerY;
            }
            tx /= u.votes.size(); ty /= u.votes.size();
            for (size_t r = 0; r < regions.size(); ++r)
            {
                auto &pool = regions[r].pools[u.kind];
                if (pool.sites.empty()) continue;
                auto site = pool.sites[pool.sites.size() / 2];
                double score = std::abs(site->X() - tx) + std::abs(site->Y() - ty);
                if (score < best) { best = score; selected = r; }
            }
        }
        if (selected < 0) throw std::runtime_error("External floorplan: no available device sites for " + u.sortName);
        auto &pool = regions[selected].pools[u.kind];
        auto site = pool.sites[pool.cursor++ % pool.sites.size()];
        pool.demand += u.demand;
        // Memory and generic logic share the physical SLICE fabric.
        if (u.kind == Memory) regions[selected].pools[Logic].demand += u.demand;
        float x = site->X() - u.offsetX, y = site->Y() - u.offsetY;
        float ox = x, oy = y;
        p->legalizeXYInArea(u.pu, x, y); // Existing device/macro bounds only.
        macroAdjusted += x != ox || y != oy;
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::runtime_error("External floorplan produced non-finite coordinates");
        for (const auto &member : u.members)
        {
            auto name = membership.at(member.cell);
            int cx, cy; device->getClockRegionByLocation(x + member.x, y + member.y, cx, cy);
            bool in = modules.at(name).count(regionId.at("X" + std::to_string(cx) + "Y" + std::to_string(cy)));
            ++moduleInside[name].first; moduleInside[name].second += in; outside += !in;
        }
        seeds.emplace_back(u.pu, x, y);
        locations << u.pu->getId() << '\t' << u.sortName << '\t' << kindNames[u.kind] << '\t'
                  << x << '\t' << y << '\t' << u.mixed << '\t' << fallback << '\n';
    }
    // Preserve the downstream legacy heuristic's input separately from external module count.
    // A recorded legacy count can be supplied for controlled comparisons. Without one,
    // explicitly report a resource-size proxy, not a fictitious PaToH result.
    int clusterCount = 0;
    std::string countSource = "resource-size-proxy";
    double cellLimit = std::max(1, std::min(40000, int(realCells.size() / 4)));
    int dspLimit = cfg.count("clockRegionDSPNum") ? positiveInt(cfg.at("clockRegionDSPNum"), "clockRegionDSPNum") : 96;
    int bramLimit = cfg.count("clockRegionBRAMNum") ? positiveInt(cfg.at("clockRegionBRAMNum"), "clockRegionBRAMNum") : 24;
    for (const auto &s : sizes)
        clusterCount += std::max(1, int(std::ceil(std::max({s.second[0] / cellLimit,
                                      double(s.second[1]) / dspLimit, double(s.second[2]) / bramLimit}))));
    if (cfg.count("ExternalFloorplanReferenceClusterCount"))
    {
        clusterCount = positiveInt(cfg.at("ExternalFloorplanReferenceClusterCount"), "ExternalFloorplanReferenceClusterCount");
        countSource = "explicit-reference";
    }
    for (const auto &seed : seeds)
        std::get<0>(seed)->setAnchorLocationAndForgetTheOriginalOne(std::get<1>(seed), std::get<2>(seed));
    p->setClusterNum(clusterCount);
    p->updateElementBinGrid();

    auto report = cfg.find("external floorplan report file");
    if (report != cfg.end() && !report->second.empty())
    {
        std::ofstream out(report->second), coords(report->second + ".positions.tsv");
        out << "{\n\"schema\":\"amf-external-floorplan-initialization-v1\",\n"
            << "\"modules\":" << modules.size() << ",\"real_cells\":" << realCells.size()
            << ",\"initialized_pus\":" << units.size() << ",\"fixed_or_locked_pus\":" << fixed
            << ",\"mixed_membership_pus\":" << mixed << ",\"resource_fallback_pus\":" << fallbacks
            << ",\"macro_bounds_adjusted_pus\":" << macroAdjusted << ",\"outside_preferred_cells\":" << outside
            << ",\"legacy_cluster_count\":" << clusterCount << ",\"legacy_cluster_count_source\":\"" << countSource
            << "\",\"dense_placement\":" << (p->isDensePlacement() ? "true" : "false")
            << ",\"internal_partitioning_executed\":false,\"sa_executed\":false,"
            << "\"persistent_region_constraints\":false,\"module_cells\":{";
        bool first = true;
        for (const auto &m : moduleInside)
        {
            if (!first) out << ',';
            first = false;
            out << '"' << m.first << "\":{\"movable\":" << m.second.first << ",\"initially_inside\":" << m.second.second << '}';
        }
        out << "}}\n"; coords << locations.str();
        if (!out || !coords) throw std::runtime_error("Cannot write external floorplan initialization report");
    }
    print_info("External floorplan seeded " + std::to_string(units.size()) + " PUs; mixed=" +
               std::to_string(mixed) + ", resource fallbacks=" + std::to_string(fallbacks) +
               ", legacy cluster count=" + std::to_string(clusterCount) + " (" + countSource + ")");
}

