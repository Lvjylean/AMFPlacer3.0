#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "HardResourceUtils.h"
#include "simpleJSON.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <omp.h>

// Diagnostic only. Calls unchanged production initial packing and CARRY slot
// reconstruction, before global placement. The probe SLICE is not a placement
// result: this mapping depends on macro membership and connections, not X/Y.
int main(int argc, char **argv)
{
    try {
        if (argc != 3 && argc != 4) throw std::runtime_error("usage: checkCarrySlotMembership config.json report-prefix [--assert-first]");
        const bool assertFirst = argc == 4 && std::string(argv[3]) == "--assert-first";
        auto cfg = parseJSONFile(argv[1]);
        omp_set_num_threads(std::stoi(cfg.at("jobs")));
        DeviceInfo device(cfg, cfg.at("device"));
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        InitialPacker initial(&design, &device, &placement, cfg);
        initial.pack();
        HardResourceUtils::validateCascadeMacros(&placement);
        DeviceInfo::DeviceSite *probe = nullptr;
        for (auto s : device.getSites())
            if (s->getSiteType() == "SLICEL" && !s->isOccupied()) { probe = s; break; }
        if (!probe) throw std::runtime_error("no unoccupied SLICEL probe");
        using Site = ParallelCLBPacker::PackingCLBSite;
        std::vector<Site *> unused(placement.getPlacementUnits().size(), nullptr);
        const std::string prefix = argv[2];
        std::ofstream overlaps(prefix + ".overlaps.tsv"), slots(prefix + ".slots.tsv"), pins(prefix + ".pins.tsv");
        overlaps << "macro_id\tmacro\tcarry\toffset\tcell_id\tcell\tlut6_2\tcurrent_lut6\tmember_offset\tin_single\tin_pair\n";
        slots << "macro_id\tcarry\toffset\tbit\tside\tcell_id\tcell\tvirtual\tin_single\tin_pair\n";
        pins << "macro_id\tcarry\toffset\tinput\tdriver\toutput\tfanout\tin_macro\tmember_offset\tsame_hard_region\n";
        size_t macros = 0, sites = 0, conflictSites = 0, overlapEvents = 0, repeatedSites = 0;
        std::set<int> affectedMacros, uniqueOverlaps;
        for (auto macro : placement.getPlacementMacros()) {
            if (macro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY) continue;
            ++macros;
            for (auto carry : macro->getCells()) {
                if (!carry->isCarry()) continue;
                ++sites;
                const float offset = macro->getCellOffsetYInMacro(carry);
                Site site(&placement, probe, 3, 10, .25, .5, 6, 10, .4, .02, unused);
                site.mapCarryRelatedCellsToSlots(macro, offset);
                std::set<DesignInfo::DesignCell *> paired;
                for (const auto &pair : site.getFixedPairedLUTs()) { paired.insert(pair.first); paired.insert(pair.second); }
                std::set<DesignInfo::DesignCell *> overlap;
                for (auto cell : site.getConflictLUTs()) if (paired.count(cell)) overlap.insert(cell);
                std::set<int> mapped;
                bool repeated = false;
                const auto &map = site.getSlotMapping();
                for (int bit = 0; bit < 8; ++bit) for (int side = 0; side < 2; ++side) {
                    auto cell = map.LUTs[bit / 4][side][bit % 4];
                    if (cell && !mapped.insert(cell->getCellId()).second) repeated = true;
                }
                repeatedSites += repeated;
                if (overlap.empty()) continue;
                ++conflictSites;
                affectedMacros.insert(macro->getId());
                overlapEvents += overlap.size();
                for (auto cell : overlap) {
                    uniqueOverlaps.insert(cell->getCellId());
                    overlaps << macro->getId() << '\t' << macro->getName() << '\t' << carry->getName() << '\t' << offset
                             << '\t' << cell->getCellId() << '\t' << cell->getName() << '\t'
                             << (cell->getOriCellType() == DesignInfo::CellType_LUT6_2) << '\t' << cell->isLUT6() << '\t'
                             << macro->getCellOffsetYInMacro(cell) << "\t1\t1\n";
                }
                // Save compact detailed evidence for the first twenty affected sites.
                if (conflictSites <= 20) {
                    for (int bit = 0; bit < 8; ++bit) for (int side = 0; side < 2; ++side) {
                        auto cell = map.LUTs[bit / 4][side][bit % 4];
                        if (!cell) continue;
                        slots << macro->getId() << '\t' << carry->getName() << '\t' << offset << '\t' << bit << '\t'
                              << (side ? "DI/5LUT" : "S/6LUT") << '\t' << cell->getCellId() << '\t' << cell->getName()
                              << '\t' << cell->isVirtualCell() << '\t' << site.getConflictLUTs().count(cell) << '\t' << paired.count(cell) << '\n';
                    }
                    for (auto pin : carry->getInputPins()) {
                        const auto &ref = pin->getRefPinName();
                        if (ref.find("S[") != 0 && ref.find("DI[") != 0) continue;
                        auto driver = pin->getDriverPin();
                        if (!driver) continue;
                        auto cell = driver->getCell();
                        const bool inMacro = placement.getPlacementUnitByCell(cell) == macro;
                        pins << macro->getId() << '\t' << carry->getName() << '\t' << offset << '\t' << ref << '\t'
                             << cell->getName() << '\t' << driver->getRefPinName() << '\t' << pin->getNet()->getPinsBeDriven().size()
                             << '\t' << inMacro << '\t' << (inMacro ? macro->getCellOffsetYInMacro(cell) : -9999)
                             << '\t' << placement.sameHardRegion(carry, cell) << '\n';
                    }
                }
                if (conflictSites == 1) {
                    std::cout << "FIRST_OVERLAP macro=" << macro->getName() << " carry=" << carry->getName()
                              << " offset=" << offset << " cell=" << (*overlap.begin())->getName() << std::endl;
                }
                if (assertFirst) {
                    overlaps.flush(); slots.flush(); pins.flush();
                    std::cout << "CALLING_UNCHANGED_PRODUCTION_ADD_CARRY" << std::endl;
                    site.addCarry();
                    throw std::runtime_error("expected production assertion did not occur");
                }
            }
        }
        if (!overlaps || !slots || !pins) throw std::runtime_error("diagnostic output failure");
        std::ofstream summary(prefix + ".summary.json");
        summary << "{\n\"carry_macros\":" << macros << ",\n\"carry_sites\":" << sites
                << ",\n\"sites_with_duplicate_lut_slots\":" << repeatedSites
                << ",\n\"sites_with_set_overlap\":" << conflictSites << ",\n\"affected_macros\":" << affectedMacros.size()
                << ",\n\"overlap_events\":" << overlapEvents << ",\n\"unique_overlap_cells\":" << uniqueOverlaps.size()
                << ",\n\"production_slot_mapping_unchanged\":true,\n\"global_placement_executed\":false,\n\"routing_executed\":false\n}\n";
        std::cout << "DIAG_COMPLETE macros=" << macros << " sites=" << sites << " overlap_sites=" << conflictSites
                  << " overlap_cells=" << uniqueOverlaps.size() << std::endl;
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
}
