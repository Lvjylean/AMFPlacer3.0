/**
 * @file AMFPlacer.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief The major file describing the overall workflow of AMFPlacer (an analytical mixed-size FPGA placer)
 * @version 0.1
 * @date 2021-06-03
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include <iomanip>
#include "../../lib/utils/RuntimeProfiler.h"
#include <stdexcept>
#include "DesignInfo.h"
#include "DeviceInfo.h"
#include "GlobalPlacer.h"
#include "IncrementalBELPacker.h"
#include "InitialPacker.h"
#include "HardResourceUtils.h"
#include "ParallelCLBPacker.h"
#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "utils/simpleJSON.h"
#include <boost/filesystem.hpp>
#include <iostream>
#include <omp.h>

/**
 * @brief AMFPlacer is an analytical mixed-size FPGA placer
 *
 * To enable the performance optimization of application mapping on modern field-programmable gate arrays (FPGAs),
 * certain critical path portions of the designs might be prearranged into many multi-cell macros during synthesis.
 * These movable macros with constraints of shape and resources lead to challenging mixed-size placement for FPGA
 * designs which cannot be addressed by previous works of analytical placers. In this work, we propose AMF-Placer,
 * an open-source analytical mixed-size FPGA placer supporting mixed-size placement on FPGA, with an interface to
 * Xilinx Vivado. To speed up the convergence and improve the quality of the placement, AMF-Placer is equipped with
 * a series of new techniques for wirelength optimization, cell spreading, packing, and legalization. Based on a set
 * of the latest large open-source benchmarks from various domains for Xilinx Ultrascale FPGAs, experimental results
 * indicate that AMF-Placer can improve HPWL by 20.4%-89.3% and reduce runtime by 8.0%-84.2%, compared to the
 * baseline. Furthermore, utilizing the parallelism of the proposed algorithms, with 8 threads, the placement procedure
 * can be accelerated by 2.41x on average.
 *
 */
class AMFPlacer
{
  public:
    /**
     * @brief Construct a new AMFPlacer object according to a given placer configuration file
     *
     * @param JSONFileName
     */
    AMFPlacer(std::string JSONFileName)
    {
        AMF_PROFILE_FUNCTION("initialization");
        JSON = parseJSONFile(JSONFileName);

        assert(JSON.find("vivado extracted device information file") != JSON.end());
        // Special pin offsets are required only by designs using that hard IP.
        assert(JSON.find("vivado extracted design information file") != JSON.end());
        if (JSON.find("dumpDirectory") != JSON.end())
        {
            if (!fileExists(JSON["dumpDirectory"]))
                assert(boost::filesystem::create_directories(JSON["dumpDirectory"]) &&
                       "the specified dump directory should be created successfully.");
        }

        oriTime = std::chrono::steady_clock::now();

        if (JSON.find("jobs") != JSON.end())
        {
            omp_set_num_threads(std::stoi(JSON["jobs"]));
        }
        else
        {
            omp_set_num_threads(1);
        }

        // load device information
        deviceinfo = new DeviceInfo(JSON, JSON.count("device") ? JSON["device"] : "VCU108");
        deviceinfo->printStat();

        // load design information
        designInfo = new DesignInfo(JSON, deviceinfo);
        designInfo->printStat();
    };

    ~AMFPlacer()
    {
        AMF_PROFILE_FUNCTION("cleanup");
        delete placementInfo;
        delete designInfo;
        delete deviceinfo;
        if (incrementalBELPacker)
            delete incrementalBELPacker;
        if (globalPlacer)
            delete globalPlacer;
        if (initialPacker)
            delete initialPacker;
    }

    void inspectInputs(const std::string &reportPath)
    {
        if (JSON.count("clock file"))
        {
            std::set<std::string> connectedClocks;
            for (auto clock : designInfo->getClocksInDesign())
                if (!designInfo->getCellsUnderClock(clock).empty()) connectedClocks.insert(clock->getName());
            std::ifstream clocks(JSON["clock file"]);
            std::string name;
            while (clocks >> name)
                if (!connectedClocks.count(name))
                    throw std::runtime_error("Configured clock has no connected net: " + name);
        }
        std::map<int, std::map<std::string, int>> siteCounts, unavailableCounts;
        std::map<std::string, int> cellCounts;
        int uramCells = 0, uramSites = 0;
        for (auto site : deviceinfo->getSites())
        {
            siteCounts[site->getSLRId()][site->getSiteType()]++;
            if (site->isOccupied())
                unavailableCounts[site->getSLRId()][site->getSiteType()]++;
            if (site->getSiteType() == "URAM288" && !site->isOccupied())
                uramSites++;
        }
        for (auto cell : designInfo->getCells())
        {
            cellCounts[designInfo->DesignCellTypeStr[cell->getCellType()]]++;
            if (cell->isURAM()) uramCells++;
        }
        if (uramCells > uramSites)
            throw std::runtime_error("URAM demand exceeds available URAM sites.");
        std::ofstream out(reportPath);
        if (!out) throw std::runtime_error("Cannot open input report: " + reportPath);
        auto counts = [&out](const std::map<std::string, int> &values) {
            out << "{";
            bool first = true;
            for (const auto &item : values)
            {
                if (!first) out << ",";
                first = false;
                out << std::quoted(item.first) << ":" << item.second;
            }
            out << "}";
        };
        out << "{\n\"schema\":\"amf-input-inspection-v1\",\n\"device\":"
            << std::quoted(deviceinfo->getDeviceName()) << ",\n\"placement_executed\":false,\n"
            << "\"slr_count\":" << siteCounts.size() << ",\n\"cell_count\":"
            << designInfo->getNumCells() << ",\n\"cell_types\":";
        counts(cellCounts);
        out << ",\n\"net_count\":" << designInfo->getNumNets()
            << ",\n\"clock_count\":" << designInfo->getClocksInDesign().size()
            << ",\n\"clock_loads\":{";
        bool firstClock = true;
        for (auto clock : designInfo->getClocksInDesign())
        {
            if (!firstClock) out << ",";
            firstClock = false;
            out << std::quoted(clock->getName()) << ":" << designInfo->getCellsUnderClock(clock).size();
        }
        out << "},\n\"clock_resource_capacity\":";
        deviceinfo->writeClockResourceCapacityJson(out);
        out << ",\n\"slrs\":[";
        bool first = true;
        for (const auto &slr : siteCounts)
        {
            if (!first) out << ",";
            first = false;
            out << "{\"id\":" << slr.first << ",\"sites\":";
            counts(slr.second);
            out << ",\"unavailable_sites\":";
            counts(unavailableCounts[slr.first]);
            out << "}";
        }
        out << "]\n}\n";
        out.close();
        if (!out) throw std::runtime_error("Failed writing input report: " + reportPath);
        print_status("AMF_INPUT_INSPECTION_OK: " + reportPath);
    }

    void legalizeResources(const std::string &directory)
    {
        inspectInputs(directory + "/inputs.json");
        for (const auto &key : {"cellType2fixedAmo file", "cellType2sharedCellType file", "sharedCellType2BELtype file"})
            if (!JSON.count(key)) throw std::runtime_error(std::string("Missing resource mapping: ") + key);
        JSON["allow floating placement"] = "true";
        JSON["dumpDirectory"] = directory;
        placementInfo = new PlacementInfo(designInfo, deviceinfo, JSON);
        InitialPacker packer(designInfo, deviceinfo, placementInfo, JSON);
        packer.pack(true);
        HardResourceUtils::validateCascadeMacros(placementInfo);
        float x = (placementInfo->getGlobalMinX() + placementInfo->getGlobalMaxX()) / 2;
        float y = (placementInfo->getGlobalMinY() + placementInfo->getGlobalMaxY()) / 2;
        // This standalone stage has no global placement to supply initial positions.
        // Deterministically spread resource anchors to avoid an artificial all-at-centre matching hotspot.
        auto radicalInverse = [](unsigned int value, unsigned int base) {
            double fraction = 1.0, result = 0.0;
            while (value) { fraction /= base; result += fraction * (value % base); value /= base; }
            return result;
        };
        unsigned int resourceIndex = 0;
        for (auto pu : placementInfo->getPlacementUnits())
        {
            if (pu->isLocked()) continue;
            float initialX = x, initialY = y;
            if (pu->checkHasDSP() || pu->checkHasBRAM() || pu->checkHasCARRY() || pu->checkHasURAM())
            {
                float height = 0;
                if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
                    for (auto cell : macro->getCells()) height = std::max(height, macro->getCellOffsetYInMacro(cell));
                ++resourceIndex;
                initialX = placementInfo->getGlobalMinX() + radicalInverse(resourceIndex, 2) *
                    (placementInfo->getGlobalMaxX() - placementInfo->getGlobalMinX());
                initialY = placementInfo->getGlobalMinY() + radicalInverse(resourceIndex, 3) *
                    std::max(0.0f, placementInfo->getGlobalMaxY() - placementInfo->getGlobalMinY() - height);
            }
            pu->setAnchorLocationAndForgetTheOriginalOne(initialX, initialY);
        }
        if (JSON.count("resource initial locations file"))
        {
            std::ifstream file(JSON["resource initial locations file"]);
            if (!file) throw std::runtime_error("Cannot open resource initial locations");
            std::string line, name, extra;
            std::set<PlacementInfo::PlacementUnit *> seeded;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream values(line);
                if (!(values >> name >> x >> y) || (values >> extra) || !std::isfinite(x) || !std::isfinite(y))
                    throw std::runtime_error("Invalid resource initial location: " + line);
                auto cell = designInfo->getCell(name);
                if (!cell) throw std::runtime_error("Unknown resource seed cell: " + name);
                auto pu = placementInfo->getPlacementUnitByCell(cell);
                if (pu->isLocked() || !seeded.insert(pu).second)
                    throw std::runtime_error("Duplicate or locked resource seed: " + name);
                if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
                {
                    x -= macro->getCellOffsetXInMacro(cell);
                    y -= macro->getCellOffsetYInMacro(cell);
                }
                pu->setAnchorLocationAndForgetTheOriginalOne(x, y);
            }
        }
        placementInfo->createGridBins(5.0, 5.0);
        placementInfo->updateElementBinGrid();
        placementInfo->updateB2BAndGetTotalHPWL();
        std::vector<DesignInfo::DesignCellType> types{DesignInfo::CellType_RAMB18E2, DesignInfo::CellType_RAMB36E2,
            DesignInfo::CellType_FIFO18E2, DesignInfo::CellType_FIFO36E2, DesignInfo::CellType_DSP48E2,
            DesignInfo::CellType_URAM288, DesignInfo::CellType_URAM288_BASE};
        MacroLegalizer hard("HardResourceStage", placementInfo, deviceinfo, types, JSON);
        hard.legalize(true, false, true);
        types = {DesignInfo::CellType_CARRY8};
        MacroLegalizer carry("CarryResourceStage", placementInfo, deviceinfo, types, JSON);
        // Column assignment plus exact DP is sufficient for this legality-only stage.
        carry.legalize(true, true, true);
        for (auto pair : placementInfo->getPULegalXY().first)
            pair.first->setAnchorLocationAndForgetTheOriginalOne(pair.second, placementInfo->getPULegalXY().second.at(pair.first));
        HardResourceUtils::writePlacement(placementInfo, directory);
        print_status("AMF_HARD_RESOURCE_LEGALIZATION_OK: " + directory);
    }

    void clearSomeAttributesCannotRecord()
    {
        for (auto PU : placementInfo->getPlacementUnits())
        {
            if (PU->isPacked())
                PU->resetPacked();
        }
        for (auto pair : placementInfo->getPULegalXY().first)
        {
            if (pair.first->isFixed() && !pair.first->isLocked())
            {
                pair.first->setUnfixed();
            }
        }
    }

    /**
     * @brief launch the analytical mixed-size FPGA placement procedure
     *
     */
    void run(const std::string &packingReport = "", const std::string &initialReport = "",
             const std::string &globalReport = "")
    {
        AMF_PROFILE_FUNCTION("placement_orchestration");
        // Full multi-SLR flow remains an explicit experimental opt-in.
        for (auto site : deviceinfo->getSites())
            if (packingReport.empty() && JSON["experimental multi-SLR placement"] != "true" && site->getSLRId() != 0)
                throw std::runtime_error("Multi-SLR placement is not enabled yet; use --inspect-input.");
        for (auto cell : designInfo->getCells())
            if (packingReport.empty() && JSON["experimental multi-SLR placement"] != "true" && cell->isURAM())
                throw std::runtime_error("URAM placement is not enabled yet; use --inspect-input.");
        assert(JSON.find("cellType2fixedAmo file") != JSON.end());
        assert(JSON.find("cellType2sharedCellType file") != JSON.end());
        assert(JSON.find("sharedCellType2BELtype file") != JSON.end());
        assert(JSON.find("GlobalPlacementIteration") != JSON.end());
        // initialize placement information, including how to map cells to BELs
        placementInfo = new PlacementInfo(designInfo, deviceinfo, JSON);

        // we have to pack cells in design info into placement units in placement info with packer
        InitialPacker *initialPacker = new InitialPacker(designInfo, deviceinfo, placementInfo, JSON);
        initialPacker->pack();
        HardResourceUtils::validateCascadeMacros(placementInfo);
        if (!packingReport.empty())
        {
            std::ofstream report(packingReport);
            report << "macro\tcell\tprimitive\tbel\tslicem_required\n";
            for (auto macro : placementInfo->getPlacementMacros())
                for (const auto &fixed : macro->getFixedCellInfoVec())
                    report << macro->getName() << "\t" << fixed.cell->getName() << "\t"
                           << fixed.cell->getOriCellType() << "\t" << fixed.BELName << "\t" << macro->isMCLB() << "\n";
            if (!report) throw std::runtime_error("Cannot write packing report: " + packingReport);
            // Exercise the trial-commit path used by exception handling and
            // detailed placement, not just initial macro recognition.
            DeviceInfo::DeviceSite *testSite = nullptr;
            for (auto site : deviceinfo->getSites())
                if (site->getSiteType() == "SLICEL") { testSite = site; break; }
            unsigned int checkedMuxClusters = 0;
            std::vector<ParallelCLBPacker::PackingCLBSite *> unused;
            for (auto macro : placementInfo->getPlacementMacros())
            {
                if (macro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_MUX7 &&
                    macro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_MUX8) continue;
                if (!testSite) throw std::runtime_error("No SLICEL for MUX packing inspection");
                ParallelCLBPacker::PackingCLBSite site(placementInfo, testSite, 3, 10, 0.25, 0.5, 6, 10, 0.4, 0.02, unused);
                ParallelCLBPacker::PackingCLBSite::PackingCLBCluster cluster(&site);
                if (!cluster.tryAddPU(macro) || cluster.getNumMuxes() != 1)
                    throw std::runtime_error("MUX trial insertion lost cluster state: " + macro->getName());
                ParallelCLBPacker::PackingCLBSite::PackingCLBCluster copy(&cluster);
                copy.removePUToConstructDetCluster(macro);
                if (copy.getNumMuxes() != 0 || !copy.tryAddPU(macro) || copy.getNumMuxes() != 1 || copy.tryAddPU(macro))
                    throw std::runtime_error("MUX cluster copy/remove/reinsert failed: " + macro->getName());
                ++checkedMuxClusters;
            }
            print_info("MUX cluster trial-commit checks: " + std::to_string(checkedMuxClusters));
            print_status("Initial Packing Done");
            return;
        }
        placementInfo->resetLUTFFDeterminedOccupation();

        placementInfo->printStat();
        placementInfo->createGridBins(5.0, 5.0);
        placementInfo->verifyDeviceForDesign();

        placementInfo->buildSimpleTimingGraph();
        PlacementTimingOptimizer *timingOptimizer = new PlacementTimingOptimizer(placementInfo, JSON);
        int longPathThr = placementInfo->getLongPathThresholdLevel();
        // int mediumPathThr = placementInfo->getMediumPathThresholdLevel();

        // go through several glable placement iterations to get initial placement
        globalPlacer = new GlobalPlacer(placementInfo, JSON);

        // enable the timing optimization, start initial placement and global placement.

        globalPlacer->clusterPlacement();
        if (!initialReport.empty())
        {
            std::ofstream report(initialReport);
            size_t fixed = 0, locked = 0;
            for (auto pu : placementInfo->getPlacementUnits())
            { fixed += pu->isFixed(); locked += pu->isLocked(); }
            report << "{\"schema\":\"amf-initial-placement-v1\",\"placement_units\":"
                   << placementInfo->getPlacementUnits().size() << ",\"fixed_pus\":" << fixed
                   << ",\"locked_pus\":" << locked << ",\"legacy_cluster_count\":" << placementInfo->getClusterNum()
                   << ",\"region_preferences\":" << placementInfo->getRegionPreferences().size()
                   << ",\"full_placement_executed\":false,\"routing_executed\":false}\n";
            if (!report) throw std::runtime_error("Cannot write initial placement report");
            print_status("Initial placement inspection done; no global placement or routing");
            return;
        }
        if(placementInfo->boundaryClusteringEnabled()) timingOptimizer->clusterCriticalPathsByPhysicalRegion();
        else timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);
        globalPlacer->GlobalPlacement_fixedCLB(1, 0.0002);

        placementInfo->getTimingInfo()->setDSPInnerDelay();

        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) / 3, false, 5, true, true,
                                                  200, timingOptimizer);
        if(placementInfo->boundaryClusteringEnabled()) timingOptimizer->clusterCriticalPathsByPhysicalRegion();
        else timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);
        globalPlacer->setPseudoNetWeight(globalPlacer->getPseudoNetWeight() * 0.85);
        globalPlacer->setMacroLegalizationParameters(globalPlacer->getMacroPseudoNetEnhanceCnt() * 0.8,
                                                     globalPlacer->getMacroLegalizationWeight() * 0.8);
        placementInfo->createGridBins(2.5, 2.5);
        placementInfo->adjustLUTFFUtilization(-10, true);
        // globalPlacer->spreading(-1);
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 200, timingOptimizer);
        if(placementInfo->boundaryClusteringEnabled()) placementInfo->clearRegionPreferences();
        else placementInfo->getPU2ClockRegionCenters().clear();
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        // pack simple LUT-FF pairs and go through several global placement iterations
        incrementalBELPacker = new IncrementalBELPacker(designInfo, deviceinfo, placementInfo, JSON);
        incrementalBELPacker->LUTFFPairing(4.0);
        incrementalBELPacker->FFPairing(4.0);
        placementInfo->printStat();
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        if(placementInfo->boundaryClusteringEnabled()) timingOptimizer->clusterCriticalPathsByPhysicalRegion();
        else timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);

        globalPlacer->setPseudoNetWeight(globalPlacer->getPseudoNetWeight() * 0.85);
        globalPlacer->setMacroLegalizationParameters(globalPlacer->getMacroPseudoNetEnhanceCnt() * 0.8,
                                                     globalPlacer->getMacroLegalizationWeight() * 0.8);
        globalPlacer->setNeighborDisplacementUpperbound(3.0);

        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 25, timingOptimizer);
        // placementInfo->getPU2ClockRegionCenters().clear();

        globalPlacer->setPseudoNetWeight(globalPlacer->getPseudoNetWeight() * 0.9);
        globalPlacer->setMacroLegalizationParameters(globalPlacer->getMacroPseudoNetEnhanceCnt() * 0.9,
                                                     globalPlacer->getMacroLegalizationWeight() * 0.9);
        placementInfo->createGridBins(2, 2);
        placementInfo->adjustLUTFFUtilization(-10, true);
        // placementInfo->getDesignInfo()->resetNetEnhanceRatio();
        // timingOptimizer->enhanceNetWeight_LevelBased(mediumPathThr);
        globalPlacer->setNeighborDisplacementUpperbound(2.0);

        // timingOptimizer->moveDriverIntoBetterClockRegion(longPathThr, 0.75);
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 25, timingOptimizer);
        JSON["SpreaderSimpleExpland"] = "true";
        // placementInfo->getPU2ClockRegionCenters().clear();
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) / 2, true, 5, true, false,
                                                  25, timingOptimizer);

        // // currently, some fixed/packed flag cannot be stored in the check-point (TODO)
        // clearSomeAttributesCannotRecord();

        // // test the check-point mechanism
        // placementInfo->dumpPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoBeforeFinalPacking");
        // placementInfo->loadPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoBeforeFinalPacking.gz");
        // print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        timingOptimizer->conductStaticTimingAnalysis();
        timingOptimizer->auditPhysicalBoundaries("amf-before-pack");
        if (!globalReport.empty())
        {
            // Diagnostic exit only: execute the identical placement prefix,
            // then stop before final CLB packing and all Vivado operations.
            std::stringstream coordinates;
            coordinates << "cell\tprimitive\tpu\tx\ty\n" << std::setprecision(9);
            size_t realCells = 0;
            for (auto cell : designInfo->getCells())
            {
                if (cell->isVirtualCell()) continue;
                auto pu = placementInfo->getPlacementUnitByCellId(cell->getCellId());
                float x = pu->X(), y = pu->Y();
                if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
                {
                    x += macro->getCellOffsetXInMacro(cell);
                    y += macro->getCellOffsetYInMacro(cell);
                }
                if (!std::isfinite(x) || !std::isfinite(y))
                    throw std::runtime_error("Non-finite global placement coordinate: " + cell->getName());
                coordinates << cell->getName() << '\t' << cell->getOriCellType() << '\t'
                            << pu->getId() << '\t' << x << '\t' << y << '\n';
                ++realCells;
            }
            writeStrToGZip(globalReport + ".cells.tsv.gz", coordinates);
            std::ofstream report(globalReport);
            report << std::setprecision(12)
                   << "{\"schema\":\"amf-global-placement-inspection-v1\",\"real_cells\":" << realCells
                   << ",\"placement_units\":" << placementInfo->getPlacementUnits().size()
                   << ",\"hpwl\":" << placementInfo->updateB2BAndGetTotalHPWL()
                   << ",\"global_placement_executed\":true,\"full_placement_executed\":false,"
                      "\"final_packing_executed\":false,\"routing_executed\":false}\n";
            if (!report) throw std::runtime_error("Cannot write global placement report");
            print_status("Global placement inspection done; final packing and routing skipped");
            return;
        }
        // Final packing replaces PUs: retire all transient region preferences.
        if(placementInfo->boundaryClusteringEnabled()) placementInfo->clearRegionPreferences();
        // finally pack the elements into sites on the FPGA device
        parallelCLBPacker =
            new ParallelCLBPacker(designInfo, deviceinfo, placementInfo, JSON, 3, 10, 0.25, 0.5, 6, 10, 0.02, "first",
                                  timingOptimizer, globalPlacer->getWirelengthOptimizer());
        parallelCLBPacker->packCLBs(30, true);
        parallelCLBPacker->setPULocationToPackedSite();
        if (JSON["experimental multi-SLR placement"] == "true")
            HardResourceUtils::writePlacement(placementInfo, JSON["dumpDirectory"]);
        timingOptimizer->conductStaticTimingAnalysis();
        timingOptimizer->auditPhysicalBoundaries("amf-final-packed");
        placementInfo->checkClockUtilization(true);
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));
        placementInfo->resetLUTFFDeterminedOccupation();
        parallelCLBPacker->updatePackedMacro(true, true);
        placementInfo->dumpOverflowClockUtilization();
        placementInfo->adjustLUTFFUtilization(1, true);
        placementInfo->dumpCongestion(JSON["dumpDirectory"] + "/congestionInfo");

        if (parallelCLBPacker)
        {
            delete parallelCLBPacker;
            parallelCLBPacker = nullptr;
        }

        // currently, some fixed/packed flag cannot be stored in the check-point (TODO)
        clearSomeAttributesCannotRecord();
        placementInfo->dumpPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoFinal");
        placementInfo->checkClockUtilization(true);

        print_status("Placement Done");
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        // auto nowTime = std::chrono::steady_clock::now();
        // auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - oriTime).count();

        return;
    }

  private:
    /**
     * @brief information related to the device (BELs, Sites, Tiles, Clock Regions)
     *
     */
    DeviceInfo *deviceinfo = nullptr;

    /**
     * @brief information related to the design (cells, pins and nets)
     *
     */
    DesignInfo *designInfo = nullptr;

    /**
     * @brief inforamtion related to placement (locations, interconnections, status, constraints, legalization)
     *
     */
    PlacementInfo *placementInfo = nullptr;

    /**
     * @brief initially packing for macro extraction based on pre-defined rules
     *
     */
    InitialPacker *initialPacker = nullptr;

    /**
     * @brief incremental pairing of some FFs and LUTs into small macros
     *
     */
    IncrementalBELPacker *incrementalBELPacker = nullptr;

    /**
     * @brief global placer acconting for initial placement, quadratic placement, cell spreading and macro legalization.
     *
     */
    GlobalPlacer *globalPlacer = nullptr;

    /**
     * @brief final packing of instances into CLB sites
     *
     */
    ParallelCLBPacker *parallelCLBPacker = nullptr;

    /**
     * @brief the user-defined settings of placement
     *
     */
    std::map<std::string, std::string> JSON;
};
