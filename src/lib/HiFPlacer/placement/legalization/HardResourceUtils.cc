#include "HardResourceUtils.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <tuple>

namespace HardResourceUtils
{
using Cell = DesignInfo::DesignCell;
using Site = DeviceInfo::DeviceSite;
struct Edge { Cell *source; Cell *sink; std::string family; };

bool contiguousWithinSLR(const std::vector<Site *> &sites, int first, int count)
{
    if (first < 0 || count <= 0 || static_cast<size_t>(first + count) > sites.size()) return false;
    auto head = sites[first];
    for (int i = 0; i < count; ++i)
    {
        auto site = sites[first + i];
        if (site->isOccupied() || site->getSLRId() != head->getSLRId() ||
            site->getSiteX() != head->getSiteX() || site->getSiteY() != head->getSiteY() + i) return false;
    }
    return true;
}

// UG574: only CO[7] uses the dedicated inter-slice COUT -> CI connection.
// O[] and the other CO[] taps are ordinary fabric connections.
bool isCarryCascade(DesignInfo::DesignPin *driver, DesignInfo::DesignPin *sink)
{
    return driver && sink && driver->getCell()->isCarry() && sink->getCell()->isCarry() &&
           driver->getRefPinName() == "CO[7]" && sink->getRefPinName() == "CI";
}

static bool prefix(const std::string &value, const std::string &head) { return value.find(head) == 0; }
static std::vector<Edge> cascadeEdges(DesignInfo *design)
{
    std::vector<Edge> result;
    std::set<std::pair<int, int>> seen;
    for (auto cell : design->getCells())
    {
        if (!cell->isDSP() && !cell->isCarry() && !cell->isURAM()) continue;
        for (auto pin : cell->getInputPins())
        {
            auto driver = pin->getDriverPin();
            if (!driver) continue;
            auto source = driver->getCell();
            const auto in = pin->getRefPinName(), out = driver->getRefPinName();
            std::string family;
            if (isCarryCascade(driver, pin)) family = "CARRY8";
            if (cell->isDSP() && source->isDSP())
            {
                for (const auto &ports : std::vector<std::pair<std::string, std::string>>{
                         {"ACIN[", "ACOUT["}, {"BCIN[", "BCOUT["}, {"PCIN[", "PCOUT["},
                         {"CARRYCASCIN", "CARRYCASCOUT"}, {"MULTSIGNIN", "MULTSIGNOUT"}})
                    if (prefix(in, ports.first) && prefix(out, ports.second)) family = "DSP48E2";
            }
            if (cell->isURAM() && source->isURAM() && prefix(in, "CAS_IN") && prefix(out, "CAS_OUT"))
                throw std::runtime_error("URAM cascade is not supported by the independent-URAM legalizer: " + cell->getName());
            if (!family.empty() && seen.insert({source->getCellId(), cell->getCellId()}).second)
                result.push_back({source, cell, family});
        }
    }
    return result;
}

static std::vector<Cell *> cores(PlacementInfo::PlacementMacro *macro)
{
    std::vector<Cell *> result;
    for (auto cell : macro->getCells())
        if (cell->isCarry() || cell->isDSP() || cell->isBRAM() || cell->isURAM()) result.push_back(cell);
    std::stable_sort(result.begin(), result.end(), [macro](Cell *a, Cell *b) {
        return macro->getCellOffsetYInMacro(a) < macro->getCellOffsetYInMacro(b);
    });
    return result;
}

void validateCascadeMacros(PlacementInfo *placement)
{
    for (const auto &edge : cascadeEdges(placement->getDesignInfo()))
    {
        auto sourcePU = placement->getPlacementUnitByCell(edge.source);
        auto sinkPU = placement->getPlacementUnitByCell(edge.sink);
        // Explicitly fixed cells are checked by their actual sites after legalization.
        if (sourcePU->isLocked() && sinkPU->isLocked()) continue;
        auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(sourcePU);
        if (!macro || sourcePU != sinkPU)
            throw std::runtime_error("Dedicated cascade is split between placement units: " + edge.sink->getName());
        auto cells = cores(macro);
        auto a = std::find(cells.begin(), cells.end(), edge.source), b = std::find(cells.begin(), cells.end(), edge.sink);
        if (a == cells.end() || b == cells.end() || b - a != 1)
            throw std::runtime_error("Dedicated cascade is not a supported linear chain: " + edge.sink->getName());
    }
}

CellSites validatePlacement(PlacementInfo *placement)
{
    CellSites assigned;
    std::set<Site *> occupied;
    for (const auto &mapping : placement->getPULegalSite())
    {
        auto pu = mapping.first;
        if (!pu->checkHasDSP() && !pu->checkHasBRAM() && !pu->checkHasCARRY() && !pu->checkHasURAM()) continue;
        const auto &sites = mapping.second;
        if (!contiguousWithinSLR(sites, 0, sites.size()))
            throw std::runtime_error("Hard-resource assignment crosses an SLR, a gap or an unavailable site: " + pu->getName());
        std::vector<Cell *> cells;
        if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu)) cells = cores(macro);
        else if (auto single = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) cells.push_back(single->getCell());
        if (cells.size() != sites.size()) throw std::runtime_error("Hard-resource assignment size mismatch: " + pu->getName());
        for (size_t i = 0; i < cells.size(); ++i)
        {
            if (!occupied.insert(sites[i]).second) throw std::runtime_error("Duplicate hard-resource site: " + sites[i]->getName());
            if (!cells[i]->isVirtualCell()) assigned[cells[i]] = sites[i];
        }
    }
    for (auto pu : placement->getPlacementUnpackedCells())
    {
        auto cell = pu->getCell();
        if (pu->isLocked() && (cell->isURAM() || cell->isDSP() || cell->isBRAM() || cell->isCarry()))
        {
            auto site = pu->getLockedSite();
            if (!site || !occupied.insert(site).second)
                throw std::runtime_error("Invalid or duplicate locked hard-resource site: " + cell->getName());
            assigned[cell] = site;
        }
    }
    for (auto cell : placement->getCells())
    {
        if (cell->isVirtualCell() || (!cell->isURAM() && !cell->isDSP() && !cell->isBRAM() && !cell->isCarry())) continue;
        if (!assigned.count(cell)) throw std::runtime_error("Unassigned hard resource: " + cell->getName());
        auto name = assigned.at(cell)->getName();
        if ((cell->isURAM() && !prefix(name, "URAM288_")) || (cell->isDSP() && !prefix(name, "DSP48E2_")) ||
            (cell->isCarry() && !prefix(name, "SLICE_")) || (cell->isBRAM() && !prefix(name, "RAMB")))
            throw std::runtime_error("Incompatible hard-resource site: " + cell->getName());
    }
    for (const auto &edge : cascadeEdges(placement->getDesignInfo()))
    {
        auto a = assigned.at(edge.source), b = assigned.at(edge.sink);
        if (a->getSLRId() != b->getSLRId() || a->getSiteX() != b->getSiteX() || a->getSiteY() + 1 != b->getSiteY())
            throw std::runtime_error("Illegal dedicated cascade adjacency/SLR: " + edge.source->getName() + " -> " + edge.sink->getName());
    }
    return assigned;
}

static std::string tclWord(const std::string &value)
{
    std::string result;
    for (char c : value)
    {
        if (c == '\n' || c == '\r' || c == '\t') throw std::runtime_error("Unsupported whitespace in cell name");
        if (std::string(" \\[]{}$;\"").find(c) != std::string::npos) result += '\\';
        result += c;
    }
    return result;
}

void writePlacement(PlacementInfo *placement, const std::string &directory)
{
    auto assigned = validatePlacement(placement);
    std::ofstream tsv(directory + "/resources.tsv"), tcl(directory + "/place_resources.tcl"), report(directory + "/resources.json");
    if (!tsv || !tcl || !report) throw std::runtime_error("Cannot write resource placement report");
    tsv << "cell\ttype\tsite\tbel\tslr\n";
    tcl << "# Partial hard-resource placement only. No CLB placement or routing.\nset amf_resources [list]\n";
    std::map<std::string, int> counts, edges;
    std::map<int, int> uramBySLR;
    for (const auto &entry : assigned)
    {
        auto cell = entry.first; auto site = entry.second;
        std::string target = site->getName(), bel;
        if (cell->isURAM()) { bel = "URAM_288K_INST"; uramBySLR[site->getSLRId()]++; }
        else if (cell->isDSP()) bel = "DSP_ALU";
        else if (cell->isCarry()) bel = "CARRY8";
        else if (cell->getOriCellType() == DesignInfo::CellType_RAMB36E2 || cell->getOriCellType() == DesignInfo::CellType_FIFO36E2)
            target = "RAMB36_X" + std::to_string(site->getSiteX()) + "Y" + std::to_string(site->getSiteY()/2);
        else bel = site->getSiteY() % 2 ? "RAMB18E2_U" : "RAMB18E2_L";
        auto type = placement->getDesignInfo()->DesignCellTypeStr[cell->getOriCellType()];
        counts[type]++;
        tsv << cell->getName() << '\t' << type << '\t' << target << '\t' << bel << '\t' << site->getSLRId() << '\n';
        tcl << "lappend amf_resources " << tclWord(cell->getName()) << " " << tclWord(target + (bel.empty() ? "" : "/" + bel)) << '\n';
    }
    tcl << "place_cell $amf_resources\nunset amf_resources\n";
    for (const auto &edge : cascadeEdges(placement->getDesignInfo())) edges[edge.family]++;
    auto emit = [&report](const std::map<std::string, int> &values) {
        bool first = true; report << "{";
        for (const auto &v : values) { if (!first) report << ','; first = false; report << std::quoted(v.first) << ':' << v.second; }
        report << "}";
    };
    report << "{\"schema\":\"amf-hard-resource-legalization-v1\",\"full_placement_executed\":false,\"assigned_cells\":" << assigned.size() << ",\"cell_types\":";
    emit(counts); report << ",\"dedicated_cascade_pairs\":"; emit(edges);
    std::map<std::string, int> slrs;
    for (const auto &v : uramBySLR) slrs[std::to_string(v.first)] = v.second;
    report << ",\"uram_by_slr\":"; emit(slrs); report << "}\n";
    tsv.close(); tcl.close(); report.close();
    if (!tsv || !tcl || !report) throw std::runtime_error("Failed writing resource placement");
}

float resourcePitch(DeviceInfo *device, const std::string &siteType)
{
    std::string type = siteType;
    auto sites = device->getSitesInType(type);
    std::sort(sites.begin(), sites.end(), [](Site *a, Site *b) {
        return std::make_tuple(a->getSiteX(), a->getSiteY()) < std::make_tuple(b->getSiteX(), b->getSiteY());
    });
    for (size_t i = 1; i < sites.size(); ++i)
        if (sites[i]->getSiteX() == sites[i-1]->getSiteX() && sites[i]->getSLRId() == sites[i-1]->getSLRId() &&
            sites[i]->getSiteY() == sites[i-1]->getSiteY()+1 && sites[i]->Y() > sites[i-1]->Y())
            return sites[i]->Y()-sites[i-1]->Y();
    throw std::runtime_error("Cannot derive resource row pitch: " + siteType);
}
}
