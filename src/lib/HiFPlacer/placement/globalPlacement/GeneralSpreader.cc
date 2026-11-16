#include "../../../utils/PhaseWaitProfile.h"
#include "../../../utils/PlacementFeatureConfig.h"
#include "../../../utils/RuntimeProfiler.h"
/**
 * @file GeneralSpreader.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief  This implementation file contains APIs' implementation of the GeneralSpreader which accounts for the cell
 * spreading, which controls the cell density of specific resource type, under the device constraints for specific
 * regions.
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "GeneralSpreader.h"
#include "BatchedSpreadPlan.h"

#include "MacroStudyControl.h"

#include <cmath>
#include <omp.h>
#include <queue>
#include <thread>
#include "SpreadReplayCapture.h"
#include <filesystem>
#include <iomanip>

namespace {
constexpr float residualSuggestionWeight = 1.0f;

// A macro can straddle a normal and a secondary rectangle. Combine the
// member proposals about the same historical anchor, then write the PU once.
struct ResidualMacroSuggestions {
    int count = 0;
    double x = 0, y = 0;
    void add(float proposedX, float proposedY) {
        ++count;
        x += proposedX;
        y += proposedY;
    }
    float combine(double &sumX, double &sumY, int members, float ordinaryWeight) const {
        if (!count) return ordinaryWeight;
        if (count == members) return residualSuggestionWeight;
        const double totalWeight = ordinaryWeight * double(members - count) +
                                   residualSuggestionWeight * double(count);
        const double meanWeight = totalWeight / members;
        sumX = ((sumX - x) * ordinaryWeight + x * residualSuggestionWeight) / meanWeight;
        sumY = ((sumY - y) * ordinaryWeight + y * residualSuggestionWeight) / meanWeight;
        return float(meanWeight);
    }
};

template<class F> void spreadMembers(PlacementInfo::PlacementUnit *pu,F visit) {
    if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)) {
        for(int i=0;i<macro->getNumOfCells();++i) {
            float x,y;DesignInfo::DesignCellType type;macro->getVirtualCellInfo(i,x,y,type);
            visit(macro->getCell(i),x,y);
        }
    } else if(auto cell=dynamic_cast<PlacementInfo::PlacementUnpackedCell*>(pu))visit(cell->getCell(),0.f,0.f);
}
// U250 has four physical SLRs. A missing/incomplete boundary file cannot
// certify same-die movement; fail before mutating a placement.
std::vector<float> spreadingSeams(PlacementInfo *pi,bool strict) {
    std::vector<float> seams;
    if(auto model=pi->getDeviceInfo()->getPhysicalBoundaryModel())
        for(const auto &b:model->getBoundaries())
            if(b.active && b.kind=="SLR" && b.orientation=='Y')seams.push_back(b.coordinate);
    std::sort(seams.begin(),seams.end());
    seams.erase(std::unique(seams.begin(),seams.end()),seams.end());
    if(strict && pi->getDeviceInfo()->getPlacementPolicy().kind==amf_device::Kind::U250 &&
       (seams.size()!=3 || !std::all_of(seams.begin(),seams.end(),[](float v){return std::isfinite(v);})))
        throw std::invalid_argument("DirectionalResidualSpreading requires three physical U250 SLR seams");
    return seams;
}
}

void GeneralSpreader::prepareRoundLimit(PlacementInfo *placement,
    const std::map<std::string,std::string> &config, CrossDieSpreadLimit &limit, int iteration)
{
    AMF_PROFILE_SCOPE("spreading", "region_normalization");
    static size_t round = 0;
    const auto roundId = ++round;
    const auto started = std::chrono::steady_clock::now();
    using PU = PlacementInfo::PlacementUnit;
    std::vector<std::pair<PU*, CrossDieSpreadLimit::Point>> before;
    const bool normalize = placement->paperBoundaryClusteringEnabled() &&
        amf_config::feature(config, "BoundaryRegionClipping", true);
    if (normalize) {
        const auto &preferences = placement->getRegionPreferences();
        for (auto pu : placement->getPlacementUnits())
            if (!pu->isFixed() && !pu->isLocked() && preferences.count(pu))
                before.push_back({pu, {pu->X(), pu->Y()}});
        // A preference correction runs inside the spreading entry point, before a
        // hotspot even exists. It cannot use the residual cross-die exception.
        const bool guard=amf_config::feature(config,"DirectionalResidualSpreading",true);
        const auto seams=guard?spreadingSeams(placement,true):std::vector<float>{};
        for(auto &entry:before) {
            auto pu=entry.first;
            const PlacementInfo::Location last{pu->lastX(),pu->lastY()};
            if(!placement->clipPaperRegionLocation(pu) || seams.empty()) continue;
            bool crossed=false;
            spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float offset){
                crossed |= die_spread::dieAt(seams,entry.second.y+offset)!=die_spread::dieAt(seams,pu->Y()+offset);
            });
            if(crossed) {
                pu->setAnchorLocationAndForgetTheOriginalOne(last.X,last.Y);
                pu->setAnchorLocation(entry.second.x,entry.second.y,false);
            }
        }
    }
    // Publish PU/member coordinates and density before constructing private die
    // views or recording any resource pass's displacement reference.
    placement->updateElementBinGrid(false);
    initializeRoundLimit(placement, config, limit);

    auto dir = config.find("BoundaryReportDirectory");
    if (!normalize || dir == config.end() || dir->second.empty()) return;
    const std::filesystem::path output(dir->second);
    std::filesystem::create_directories(output);
    auto open = [&](std::ofstream &file, const char *name, const char *header) {
        const auto path = output / name;
        const bool fresh = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        file.open(path, std::ios::app);
        if (!file) throw std::runtime_error("Cannot write spreading region correction report");
        file << std::setprecision(12);
        if (fresh) file << header << '\n';
    };
    std::ofstream summary, details;
    open(summary, "spreading-region-correction-summary.tsv",
         "round\tphase\titeration\tcorrected_pus\tmax_correction_bins\tsum_correction_bins\tpreparation_seconds\tbudget_origin");
    const bool detailed = amf_config::feature(config, "DetailedDiagnosticsEnable", true);
    if (detailed) open(details, "spreading-region-corrections.tsv",
         "round\tphase\titeration\tpu_id\tpu_name\told_x\told_y\tnew_x\tnew_y\tdx\tdy\tcorrection_bins");
    size_t moved = 0;
    double maximum = 0, sum = 0;
    for (const auto &entry : before) {
        auto pu = entry.first;
        const auto old = entry.second;
        const double dx = double(pu->X()) - old.x, dy = double(pu->Y()) - old.y;
        if (dx == 0 && dy == 0) continue;
        const double bins = std::hypot(dx / placement->getBinGridW(), dy / placement->getBinGridH());
        ++moved; maximum = std::max(maximum, bins); sum += bins;
        if (detailed) {
            std::string name = pu->getName();
            for (char &c : name) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
            details << roundId << '\t' << placement->paperBoundaryPhaseName() << '\t' << iteration << '\t'
                    << pu->getId() << '\t' << name << '\t' << old.x << '\t' << old.y << '\t'
                    << pu->X() << '\t' << pu->Y() << '\t' << dx << '\t' << dy << '\t' << bins << '\n';
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    summary << roundId << '\t' << placement->paperBoundaryPhaseName() << '\t' << iteration << '\t'
            << moved << '\t' << maximum << '\t' << sum << '\t' << seconds << "\tpost_region_correction\n";
}

void GeneralSpreader::initializeRoundLimit(PlacementInfo *placement,
    const std::map<std::string,std::string> &config, CrossDieSpreadLimit &limit)
{
    double bins = amf_config::crossDieSpreadMaxBins(config);
    bool seams = false;
    if (auto model = placement->getDeviceInfo()->getPhysicalBoundaryModel())
        for (const auto &b : model->getBoundaries())
            seams |= b.active && b.kind == "SLR" && b.orientation == 'Y';
    if (!seams || !amf_config::feature(config, "DieFirstSpreadingEnable", true)) bins = 0;
    limit.reset(bins, placement->getBinGridW(), placement->getBinGridH(), placement->getPlacementUnits());
}

GeneralSpreader::GeneralSpreader(PlacementInfo *placementInfo, std::map<std::string, std::string> &JSONCfg,
                                 std::string &sharedCellType, int currentIteration, float capacityShrinkRatio,
                                 bool verbose, CrossDieSpreadLimit *sharedRoundLimit)
    : placementInfo(placementInfo), JSONCfg(JSONCfg), sharedCellType(sharedCellType),
      currentIteration(currentIteration), capacityShrinkRatio(capacityShrinkRatio), verbose(verbose),
      binGrid(placementInfo->getBinGrid(placementInfo->getSharedBELTypeId(sharedCellType)))
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    overflowBins.clear();
    if (JSONCfg.find("jobs") != JSONCfg.end())
    {
        nJobs = std::stoi(JSONCfg["jobs"]);
    }
    if (JSONCfg.find("SpreaderSimpleExpland") != JSONCfg.end())
    {
        useSimpleExpland = JSONCfg["SpreaderSimpleExpland"] == "true";
    }
    if (!sharedRoundLimit)
        prepareRoundLimit(placementInfo, JSONCfg, ownedRoundLimit, currentIteration);
    directionalResidual=amf_config::feature(JSONCfg,"DirectionalResidualSpreading",true);
    sharedResidual = directionalResidual && amf_config::feature(JSONCfg,"SharedResidualSpreading",false);
    const auto seams=spreadingSeams(placementInfo,directionalResidual);
    if(directionalResidual && !seams.empty() && !amf_config::feature(JSONCfg,"DieFirstSpreadingEnable",true))
        throw std::invalid_argument("DirectionalResidualSpreading requires the physical die view on multi-die devices");
    if (directionalResidual || (!seams.empty() && amf_config::feature(JSONCfg,"DieFirstSpreadingEnable",true))) {
        auto &original=placementInfo->getBinGrid(placementInfo->getSharedBELTypeId(sharedCellType));
        dieView.reset(new die_spread::GridView(original,placementInfo->getCompatiblePlacementTable(),seams,
            placementInfo->getCellId2location().size(),
            [&](int id){return placementInfo->getCellId2location().at(id);},
            [&](int id){return int(placementInfo->getCellId2CellBinInfo().at(id).occupation);}));
        binGrid=dieView->rows;
        roundLimit = sharedRoundLimit ? sharedRoundLimit : &ownedRoundLimit;
        if(amf_config::feature(JSONCfg,"DetailedDiagnosticsEnable",true)) {
        auto dir=JSONCfg.find("SllReportDirectory");
        const std::filesystem::path output=dir==JSONCfg.end()?std::filesystem::path("."):std::filesystem::path(dir->second);
        std::filesystem::create_directories(output);
        auto open=[&](std::ofstream &f,const char *name,const char *header){
            auto path=output/name;bool fresh=!std::filesystem::exists(path)||std::filesystem::file_size(path)==0;
            f.open(path,std::ios::app);if(!f)throw std::runtime_error("Cannot write spreading diagnostics");
            f<<std::setprecision(10);if(fresh)f<<header<<'\n';
        };
        open(regionLog,"spreading-regions.tsv","resource\tcall\titeration\tinner\tseed_x\tseed_y\tdie\tphase\treason\tlocal_stop\tlocal_bins\tlocal_demand\tlocal_capacity\tdie_demand\tdie_nominal_capacity\tdie_effective_capacity\tbins\tleft\tright\tbottom\ttop\tremaining_overflow\tseed_remaining_overflow\tpu_count\tmoved_pus\tmax_displacement\tmean_displacement\tmax_abs_dy\tmax_move_pu\tcrossing_cells");
        if(directionalResidual) open(residualLog,"spreading-residual-groups.tsv",
            "resource\tcall\tinner\tseed_x\tseed_y\tgroup_bins\thot_before\thot_after\tleft\tright\tbottom\ttop\tmode");
        if(sharedResidual) open(quotaLog,"spreading-quotas.tsv",
            "resource\tcall\tinner\tx\ty\tsources\tplanned\tbefore\tafter\tbox_bins\taffected_bins");
        open(passLog,"spreading-passes.tsv","resource\tcall\titeration\tinner\tremaining_bins\tremaining_overflow\tregions\tcross_search_regions");
        open(crossingLog,"spreading-crossings.tsv","resource\tcall\titeration\tinner\tpu_id\tpu_name\told_x\told_y\tnew_x\tnew_y\tdx\tdy\tcrossing_cells\tlocal_guard");
        }
    }

    if (sharedCellType == "SLICEL_LUT" || sharedCellType == "SLICEL_FF")
        logicMask = placementInfo->logicEmptyBins.match(binGrid, sharedCellType,
                            placementInfo->getBinGridW(), placementInfo->getBinGridH());

    reserveFirst = sharedCellType == "SLICEL_FF" && placementInfo->getBinGridW() == 5.0f &&
        placementInfo->getBinGridH() == 5.0f && amf_config::feature(JSONCfg,"CoarseFFReserveFirst",false);
    if (reserveFirst) for (auto &row : binGrid) for (auto b : row) b->setSpreadingReserveRatio(capacityShrinkRatio);
    triggerStats = amf_config::feature(JSONCfg,"SpreadTriggerStats",false);
    if (triggerStats) for (auto &row : binGrid) triggerCounts.emplace_back(row.size());
}

void GeneralSpreader::writeTriggerStats(int call)
{
    if (!triggerStats) return;
    const auto started = std::chrono::steady_clock::now();
    const auto output = std::filesystem::path(JSONCfg.at("SllReportDirectory"));
    std::filesystem::create_directories(output);
    const auto path = output / "spreading-triggers.tsv";
    const bool fresh = !std::filesystem::exists(path) || std::filesystem::file_size(path)==0;
    std::ofstream log(path,std::ios::app);
    if (!log) throw std::runtime_error("Cannot write spread trigger statistics");
    if (fresh) log << "resource\tcall\titeration\tgrid_w\tgrid_h\tratio\treserve_first\tx\ty\tdie\tleft\tright\tbottom\ttop\tchecks\toverflow_events\tseed_events\n";
    log << std::setprecision(10);
    unsigned long long checks=0,overflow=0,seeds=0;
    for (size_t y=0;y<binGrid.size();++y) for(size_t x=0;x<binGrid[y].size();++x) {
        const auto &c=triggerCounts[y][x]; auto b=binGrid[y][x];
        checks+=c.checks; overflow+=c.overflow; seeds+=c.seeds;
        if (!c.overflow && !c.seeds) continue;
        log << sharedCellType << '\t' << call << '\t' << currentIteration << '\t'
            << placementInfo->getBinGridW() << '\t' << placementInfo->getBinGridH() << '\t'
            << capacityShrinkRatio << '\t' << reserveFirst << '\t' << x << '\t' << y << '\t'
            << (dieView ? dieView->rowDie[y] : -1) << '\t' << b->left() << '\t' << b->right() << '\t'
            << b->bottom() << '\t' << b->top() << '\t' << c.checks << '\t' << c.overflow << '\t' << c.seeds << '\n';
    }
    log.close();
    if (!log) throw std::runtime_error("Cannot finish spread trigger statistics");
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cout << "SPREAD_TRIGGERS resource=" << sharedCellType << " call=" << call << " iteration=" << currentIteration
        << " grid_w=" << placementInfo->getBinGridW() << " grid_h=" << placementInfo->getBinGridH()
        << " reserve_first=" << reserveFirst << " checks=" << checks << " overflow_events=" << overflow
        << " seed_events=" << seeds << " report_seconds=" << seconds << std::endl;
}

void GeneralSpreader::spreadPlacementUnits(float forgetRatio, bool enableClockRegionAware, float displacementLimit,
                                           unsigned int spreadRegionBinSizeLimit)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    if (spreadRegionBinSizeLimit == 0)
        throw std::invalid_argument("Spread region bin limit must be positive");
    if (verbose) // usually commented for debug
        print_status("GeneralSpreader: starts to spreadPlacementUnits for type: [" + sharedCellType + "]");

    static std::map<std::string,int> replayCalls;
    const int replayCall = ++replayCalls[sharedCellType];
    std::deque<int> overflowBinNumQ;

    if (dieView && (sharedCellType == "SLICEL_LUT" || sharedCellType == "SLICEL_FF") &&
        amf_config::feature(JSONCfg,"BatchedHotspotSpreading",false)) {
        spreadInBatches(replayCall, spreadRegionBinSizeLimit, forgetRatio, enableClockRegionAware, displacementLimit);
        writeTriggerStats(replayCall);
        recordSpreadedCellLocations();
        if (JSONCfg.count("Dump Cell Density")) dumpSiteGridDensity(JSONCfg["Dump Cell Density"]);
        dumpLUTFFCoordinate();
        print_status("GeneralSpreader: accomplished spreadPlacementUnits for type: [" + sharedCellType + "]");
        return;
    }
    int loopCnt = 0;
    if (sharedResidual && dieView)
        { residualHistory.assign(binGrid.size()*binGrid.front().size(), {}); fixedDemand.clear(); }
    // int nthreads = omp_get_num_threads();
    // float totalBinNum = binGrid.size() * binGrid[0].size();

    std::vector<int> historyTotalCellNum(0);
    while (true)
    {
        if (loopCnt % 5 == 0)
        {
            for (auto &row : binGrid)
            {
                for (auto curBin : row)
                {
                    curBin->resetBinShrinkRatio();
                    curBin->resetNoOverflowCounter();
                    curBin->resetOverflowCounter();
                }
            }
        }
        loopCnt++;
        if (loopCnt > 10)
            break;
        if (JSONCfg.find("DumpLUTFFCoordTrace-GeneralSpreader") != JSONCfg.end())
        {
            std::string dumpFile = JSONCfg["DumpLUTFFCoordTrace-GeneralSpreader"];
        }

        if (verbose)
            print_status("GeneralSpreader: finding overflow regions");
        findOverflowBins(capacityShrinkRatio);
        if(dieView) {
            dieSupply=dieView->supply();
            for(auto it=localFailures.begin();it!=localFailures.end();) {
                if(!binGrid[it->first.second][it->first.first]->isSpreadingOverflow(capacityShrinkRatio)) it=localFailures.erase(it);
                else ++it;
            }
        }
        if (overflowBins.size() == 0)
            break;
        int totalCellNum = 0;
        for (auto curBin : overflowBins)
            totalCellNum += curBin->getCells().size();

        if (verbose) // usually commented for debug
        {
            print_info("found " + std::to_string(overflowBins.size()) + " overflowed bins");
            print_info("found " + std::to_string(totalCellNum) + " cells in them");
            print_info("spread for " + std::to_string(loopCnt) + " iterations");
        }
        coveredBinSet.clear();
        expandedRegions.clear();
        std::set<PlacementInfo::PlacementUnit *> involvedPUs;
        std::set<DesignInfo::DesignCell *> involvedCells;
        std::vector<PlacementInfo::PlacementUnit *> involvedPUVec;
        involvedPUVec.clear();
        involvedPUs.clear();
        involvedCells.clear();

        if (sharedResidual && dieView) {
            preWriteOverflow.clear();
            for (const auto &row : binGrid) for (auto bin : row)
                preWriteOverflow.push_back(std::max(0., double(bin->getUtilization()) -
                                                     bin->spreadingDemandLimit(capacityShrinkRatio,true)));
        }
        std::set<die_spread::Bin*> handledResiduals;
        if (directionalResidual && loopCnt>1)
            planResidualRegions(loopCnt, std::min(spreadRegionBinSizeLimit,200u), handledResiduals);
        for (auto curBin : overflowBins)
        {
            if (directionalResidual && (handledResiduals.count(curBin) || !curBin->isSpreadingOverflow(capacityShrinkRatio))) continue;
            if (coveredBinSet.find(curBin) != coveredBinSet.end())
                continue;
            if (triggerStats) ++triggerCounts[curBin->Y()][curBin->X()].seeds;
            GeneralSpreader::SpreadRegion *newRegion =
                expandFromABin(curBin, capacityShrinkRatio, spreadRegionBinSizeLimit);
            coveredBinSet.insert(curBin);
            bool overlappedWithPreviousRegion = false;
            for (auto curRegion : expandedRegions)
            {
                if (curRegion->isRegionOverlap(newRegion))
                {
                    overlappedWithPreviousRegion = true;
                    std::cout << "newRegion: "
                              << " left:" << newRegion->left() << " right:" << newRegion->right()
                              << " top:" << newRegion->top() << " bottom:" << newRegion->bottom() << "\n";
                    std::cout << "====================================================\n";
                    for (auto curbin0 : newRegion->getBinsInRegion())
                    {
                        std::cout << "-----------------\n";
                        std::cout << curbin0 << " sharedBELStr:" << curbin0->getSharedCellType()
                                  << " X: " << curbin0->X() << " Y: " << curbin0->Y() << " left:" << curbin0->left()
                                  << " right:" << curbin0->right() << " top:" << curbin0->top()
                                  << " bottom:" << curbin0->bottom() << "\n";
                    }
                    std::cout << "\n\n\nexistingRegion: "
                              << " left:" << curRegion->left() << " right:" << curRegion->right()
                              << " top:" << curRegion->top() << " bottom:" << curRegion->bottom() << "\n";
                    std::cout << "====================================================\n";
                    for (auto curbin0 : curRegion->getBinsInRegion())
                    {
                        std::cout << "-----------------\n";
                        std::cout << curbin0 << " sharedBELStr:" << curbin0->getSharedCellType()
                                  << " X: " << curbin0->X() << " Y: " << curbin0->Y() << " left:" << curbin0->left()
                                  << " right:" << curbin0->right() << " top:" << curbin0->top()
                                  << " bottom:" << curbin0->bottom() << "\n";
                    }
                    break;
                }
            }
            if (!overlappedWithPreviousRegion)
            {
                coveredBinSet.insert(curBin);
                expandedRegions.push_back(newRegion);
            }
            else
            {
                assert(false && "should not overlap");
                delete newRegion;
            }
        }

        amf_spread_capture::Capture replay(placementInfo, expandedRegions, binGrid, sharedCellType,
            replayCall, loopCnt, currentIteration, nJobs, coveredBinSet.size(), capacityShrinkRatio,
            forgetRatio, enableClockRegionAware, displacementLimit);
        auto runRegion = [&](int regionId) {
            auto start=amf_spread_capture::Clock::now();
            auto &task=replay.tasks[regionId];
            task.worker=omp_get_thread_num(); task.team=omp_get_num_threads();
            task.level=omp_get_level();task.active=omp_get_active_level();
            auto curRegion=expandedRegions[regionId];
            assert(curRegion && curRegion->getCells().size()>0);
            if (!curRegion->decision.movableTarget.empty()) {
                partitionShared(*curRegion);
                task.seconds=std::chrono::duration<double>(amf_spread_capture::Clock::now()-start).count();
                return;
            }
            auto newBox=new SpreadRegion::SubBox(placementInfo,curRegion,binGrid,capacityShrinkRatio,100,true,logicMask);
            newBox->spreadAndPartition();delete newBox;
            task.seconds=std::chrono::duration<double>(amf_spread_capture::Clock::now()-start).count();
        };
        replay.begin();
        // we don't need that high parallelism here since regionNum will be small in later iterations
        // if (nJobs > 4)
        //     omp_set_num_threads(4);
#pragma omp parallel sections
        {
#pragma omp section
            {

                if (verbose) // usually commented for debug
                    print_status("involved regions cover " + std::to_string(coveredBinSet.size()) + " bins.");
                if (verbose)
                {
                    print_status("GeneralSpreader: spreading cells in the regions");
                }

                int regionNum = expandedRegions.size();
                if (coveredBinSet.size() > 256 && regionNum > 32)
                {
#pragma omp parallel for schedule(dynamic, 4)
                    for (int regionId = 0; regionId < regionNum; regionId++)
                    {
                        runRegion(regionId);
                    }
                }
                else
                {
                    for (int regionId = 0; regionId < regionNum; regionId++)
                    {
                        runRegion(regionId);
                    }
                }
            }
#pragma omp section
            {
                if (verbose)
                    print_status("GeneralSpreader: loading involved placement units");

                const auto collectionStart=amf_spread_capture::Clock::now();
                for (auto curRegion : expandedRegions)
                {
                    assert(curRegion);
                    assert(curRegion->getCells().size() > 0);
                    for (auto curCell : curRegion->getCells())
                    {
                        if (involvedCells.find(curCell) == involvedCells.end())
                        {
                            auto tmpPU = placementInfo->getPlacementUnitByCell(curCell);
                            if (involvedPUs.find(tmpPU) == involvedPUs.end())
                            {
                                involvedPUs.insert(tmpPU);
                                involvedPUVec.push_back(tmpPU);
                            }
                            involvedCells.insert(curCell);
                        }
                    }
                }
                replay.collectionSeconds=std::chrono::duration<double>(amf_spread_capture::Clock::now()-collectionStart).count();
            }
        }

        replay.afterKernel();
        replay.puState(involvedPUVec,"before");
        omp_set_num_threads(nJobs);

        std::map<int,PlacementInfo::Location> before;
        if(dieView) for(auto pu:involvedPUVec) before.emplace(pu->getId(),PlacementInfo::Location{pu->X(),pu->Y()});
        if (verbose)
            print_status("GeneralSpreader: updating Placement Units With Spreaded Cell Locations");
        activateCrossDieLimits();
        updatePlacementUnitsWithSpreadedCellLocations(involvedPUs, involvedCells, involvedPUVec, forgetRatio,
                                                      enableClockRegionAware, displacementLimit);
        if(dieView) {
            constrainLocalWrites(involvedPUVec,before);
            constrainCrossDieWrites(involvedPUVec,before);
            if(!crossTargetDie.empty()) constrainLocalWrites(involvedPUVec,before);
            finishDieFirstIteration(replayCall,loopCnt,involvedPUVec,before);
        }
        replay.puState(involvedPUVec,"after");
        for(auto region:expandedRegions) delete region;
        expandedRegions.clear();
        if (verbose)
            print_status("GeneralSpreader: updated Placement Units With Spreaded Cell Locations");
        dumpLUTFFCoordinate();
    }

    if (verbose)
        print_status("GeneralSpreader: processed all overflow regions");

    writeTriggerStats(replayCall);
    recordSpreadedCellLocations();

    if (JSONCfg.find("Dump Cell Density") != JSONCfg.end())
    {
        dumpSiteGridDensity(JSONCfg["Dump Cell Density"]);
    }
    dumpLUTFFCoordinate();

    print_status("GeneralSpreader: accomplished spreadPlacementUnits for type: [" + sharedCellType + "]");
}

bool siteSortCmp(PlacementInfo::PlacementBinInfo *a, PlacementInfo::PlacementBinInfo *b)
{
    return (a->getUtilizationRate() > b->getUtilizationRate());
}

void GeneralSpreader::findOverflowBins(float overflowThreshold)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    overflowBins.clear();
    overflowBinSet.clear();
    for (auto &row : binGrid)
    {
        for (auto curBin : row)
        {
            if (triggerStats) ++triggerCounts[curBin->Y()][curBin->X()].checks;
            if (curBin->isSpreadingOverflow(overflowThreshold))
            {
                if (triggerStats) ++triggerCounts[curBin->Y()][curBin->X()].overflow;
                overflowBins.push_back(curBin);
                overflowBinSet.insert(curBin);
                curBin->countOverflow();
                if (curBin->getOverflowCounter() > 5)
                {
                    if (curBin->getBinShrinkRatio() > 0.8)
                    {
                        curBin->shrinkBinBy(0.015);
                    }
                    else
                    {
                        curBin->resetBinShrinkRatio();
                    }
                    curBin->resetOverflowCounter();
                }
            }
            else
            {
                curBin->countNoOverflow();
                curBin->resetOverflowCounter();
                if (curBin->getNoOverflowCounter() > 5)
                {
                    curBin->resetBinShrinkRatio();
                }
            }
        }
    }
    sort(overflowBins.begin(), overflowBins.end(), siteSortCmp);
}

void GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocationsWorker(
    PlacementInfo *placementInfo, std::set<PlacementInfo::PlacementUnit *> &involvedPUs,
    std::set<DesignInfo::DesignCell *> &involvedCells, std::vector<PlacementInfo::PlacementUnit *> &involvedPUVec,
    float forgetRatio, float displacementLimit, int startId, int endId,
    const CrossDieSpreadLimit *roundLimit, const ResidualWriteScope *scope)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    bool displacementLimitEnable = displacementLimit > 0;
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    for (int curPUID = startId; curPUID < endId; curPUID++)
    {
        assert((unsigned int)curPUID < involvedPUVec.size());
        auto curPU = involvedPUVec[curPUID];
        if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
        {
            float cellX = curUnpackedCell->X();
            float cellY = curUnpackedCell->Y();
            DesignInfo::DesignCell *curCell = curUnpackedCell->getCell();
            if (curPU->isFixed() || curPU->isLocked())
            {
                cellLoc[curCell->getCellId()].X = cellX;
                cellLoc[curCell->getCellId()].Y = cellY;
            }
            else
            {
                makeCellInLegalArea(placementInfo, cellLoc[curCell->getCellId()].X, cellLoc[curCell->getCellId()].Y);
                const bool residual = scope && scope->contains(curCell);
                const float weight = residual ? residualSuggestionWeight : forgetRatio;
                if (displacementLimitEnable && !(roundLimit && roundLimit->active(curPU->getId())))
                    curPU->setSpreadLocation_WithLimitDisplacement(cellLoc[curCell->getCellId()].X,
                                                                   cellLoc[curCell->getCellId()].Y, weight,
                                                                   displacementLimit, residual);
                else
                    curPU->setSpreadLocation(cellLoc[curCell->getCellId()].X, cellLoc[curCell->getCellId()].Y,
                                             weight, residual);
                // Clip after blending/displacement limiting: blending itself
                // can otherwise move a PU back across the selected boundary.
                placementInfo->clipPaperRegionLocation(curPU);
                placementInfo->transferCellBinInfo(curCell->getCellId(), curPU->X(), curPU->Y());
                cellLoc[curCell->getCellId()].X = curPU->X();
                cellLoc[curCell->getCellId()].Y = curPU->Y();
            }
        }
        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
        {
            if (curPU->isFixed() || curPU->isLocked())
            {
                for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                {
                    float offsetX_InMacro, offsetY_InMacro;
                    DesignInfo::DesignCellType cellType;
                    curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);

                    float cellX = curMacro->X() + offsetX_InMacro;
                    float cellY = curMacro->Y() + offsetY_InMacro;

                    DesignInfo::DesignCell *curCell = curMacro->getCell(vId);

                    cellLoc[curCell->getCellId()].X = cellX;
                    cellLoc[curCell->getCellId()].Y = cellY;
                }
            }
            else
            {
                ResidualMacroSuggestions residualMembers;
                double tmpTotalX = 0.0;
                double tmpTotalY = 0.0;

                int numCellsInvolvedInSpreading = 0;
                for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                {
                    DesignInfo::DesignCell *curCell = curMacro->getCell(vId);
                    if (involvedCells.find(curCell) != involvedCells.end())
                    {
                        float offsetX_InMacro, offsetY_InMacro;
                        DesignInfo::DesignCellType cellType;
                        curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);
                        makeCellInLegalArea(placementInfo, cellLoc[curCell->getCellId()].X,
                                            cellLoc[curCell->getCellId()].Y);
                        tmpTotalX += cellLoc[curCell->getCellId()].X - offsetX_InMacro;
                        tmpTotalY += cellLoc[curCell->getCellId()].Y - offsetY_InMacro;
                        if (scope && scope->contains(curCell))
                            residualMembers.add(cellLoc[curCell->getCellId()].X - offsetX_InMacro,
                                                cellLoc[curCell->getCellId()].Y - offsetY_InMacro);
                        numCellsInvolvedInSpreading++;
                    }
                }
                const float weight = residualMembers.combine(tmpTotalX, tmpTotalY, numCellsInvolvedInSpreading, forgetRatio);
                tmpTotalX /= (double)numCellsInvolvedInSpreading;
                tmpTotalY /= (double)numCellsInvolvedInSpreading;

                float curNewPUX = tmpTotalX;
                float curNewPUY = tmpTotalY;
                const float studyX0=curPU->X(),studyY0=curPU->Y();
                placementInfo->legalizeXYInArea(curPU, curNewPUX, curNewPUY);
                if (displacementLimitEnable && !(roundLimit && roundLimit->active(curPU->getId())))
                    curPU->setSpreadLocation_WithLimitDisplacement(curNewPUX, curNewPUY, weight,
                                                                   displacementLimit, residualMembers.count != 0);
                else
                    curPU->setSpreadLocation(curNewPUX, curNewPUY, weight, residualMembers.count != 0);
                if(macro_study::active("carry-spread-y-half") && curPU->checkHasCARRY() && macro_study::carryEligible.count(curPU->getId()))
                    curPU->setAnchorLocationAndForgetTheOriginalOne(curPU->X(),studyY0+0.5f*(curPU->Y()-studyY0));
                const float studyXB=curPU->X(),studyYB=curPU->Y();
                placementInfo->enforceLegalizeXYInArea(curPU);
                const float studyXE=curPU->X(),studyYE=curPU->Y();
                placementInfo->clipPaperRegionLocation(curPU);
                macro_study::workerStep(placementInfo,curPU,studyX0,studyY0,studyXB,studyYB,studyXE,studyYE,curPU->X(),curPU->Y());
                for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                {
                    float offsetX_InMacro, offsetY_InMacro;
                    DesignInfo::DesignCellType cellType;
                    curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);

                    float cellX = curMacro->X() + offsetX_InMacro;
                    float cellY = curMacro->Y() + offsetY_InMacro;

                    DesignInfo::DesignCell *curCell = curMacro->getCell(vId);
                    placementInfo->transferCellBinInfo(curCell->getCellId(), cellX, cellY);
                    cellLoc[curCell->getCellId()].X = cellX;
                    cellLoc[curCell->getCellId()].Y = cellY;
                }
            }
        }
    }
}

void GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocations(
    std::set<PlacementInfo::PlacementUnit *> &involvedPUs, std::set<DesignInfo::DesignCell *> &involvedCells,
    std::vector<PlacementInfo::PlacementUnit *> &involvedPUVec, float forgetRatio, bool enableClockRegionAware,
    float displacementLimit)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    ResidualWriteScope secondary;
    if (sharedResidual && dieView) {
        for (auto *region : expandedRegions) {
            if (region->decision.residualBins.empty()) continue;
            if (!secondary.view) {
                secondary.view = dieView.get();
                secondary.width = int(binGrid.front().size());
                secondary.bins.resize(binGrid.size() * secondary.width);
            }
            for (auto *bin : region->getBinsInRegion())
                secondary.bins[bin->Y() * secondary.width + bin->X()] = 1;
        }
    }
    const ResidualWriteScope *scope = secondary.view ? &secondary : nullptr;
    bool displacementLimitEnable = displacementLimit > 0;
    // if (enableClockRegionAware)
    // {
    //     auto &PU2ClockRegionColumn = placementInfo->getPU2ClockRegionColumn();
    //     auto &clockRegions = placementInfo->getDeviceInfo()->getClockRegions();
    //     for (auto curCell : involvedCells)
    //     {
    //         auto PU = placementInfo->getPlacementUnitByCellId(curCell->getCellId());
    //         if (PU2ClockRegionColumn.find(PU) != PU2ClockRegionColumn.end())
    //         {
    //             int clockRegionX = PU2ClockRegionColumn[PU];
    //             float rLeft = clockRegions[0][clockRegionX]->getLeft();
    //             float rRight = clockRegions[0][clockRegionX]->getRight();
    //             if (cellLoc[curCell->getCellId()].X < rLeft)
    //                 cellLoc[curCell->getCellId()].X = rLeft + 0.1;
    //             if (cellLoc[curCell->getCellId()].X > rRight)
    //                 cellLoc[curCell->getCellId()].X = rRight - 0.1;
    //         }
    //     }
    // }

    // Region-gain targets remain soft. The paper strategy clips complete PUs
    // in the worker after blending, within its transient selected region.
    // The next QP/STA refresh rechecks capacity and benefit.
    // VCU108 Optimization is retained only by the legacy clustering mode.
    if (placementInfo->getDeviceInfo()->getPlacementPolicy().legacySideConstraint(
            enableClockRegionAware, placementInfo->boundaryClusteringEnabled()))
    {
        auto &PU2ClockRegionColumn = placementInfo->getPU2ClockRegionColumn();
        auto &clockRegions = placementInfo->getDeviceInfo()->getClockRegions();
        std::set<PlacementInfo::PlacementUnit *> touchedPUs;
        touchedPUs.clear();
        for (auto curCell : involvedCells)
        {
            auto PU = placementInfo->getPlacementUnitByCellId(curCell->getCellId());
            if (PU2ClockRegionColumn.find(PU) != PU2ClockRegionColumn.end())
            {
                int clockRegionX = PU2ClockRegionColumn[PU];
                bool constrained = false;
                if (clockRegionX == 2)
                {
                    float rLeft = clockRegions[0][clockRegionX]->getLeft();
                    float rRight = clockRegions[0][clockRegionX]->getRight();
                    if (cellLoc[curCell->getCellId()].X < rLeft)
                    {
                        cellLoc[curCell->getCellId()].X = rLeft + 0.1;
                        constrained = true;
                    }
                    if (cellLoc[curCell->getCellId()].X > rRight)
                    {
                        cellLoc[curCell->getCellId()].X = rRight - 0.1;
                        constrained = true;
                    }
                }
                else if (clockRegionX < 2)
                {
                    float rRight = clockRegions[0][1]->getRight();
                    if (cellLoc[curCell->getCellId()].X > rRight)
                    {
                        cellLoc[curCell->getCellId()].X = rRight - 0.1;
                        constrained = true;
                    }
                }
                else if (clockRegionX > 2)
                {
                    float rLeft = clockRegions[0][3]->getLeft();
                    if (cellLoc[curCell->getCellId()].X < rLeft)
                    {
                        cellLoc[curCell->getCellId()].X = rLeft + 0.1;
                        constrained = true;
                    }
                }

                if (constrained && touchedPUs.find(PU) == touchedPUs.end() && (placementInfo->getClusterNum() <= 8))
                {
                    touchedPUs.insert(PU);
                    if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PU))
                    {
                        float XDiff = std::min(std::fabs(PU->X() - cellLoc[curCell->getCellId()].X), (float)5.0);

                        if (XDiff > 2)
                        {
                            float YDiff =
                                std::max(std::min(cellLoc[curCell->getCellId()].Y - PU->Y(), (float)5.0), (float)-5.0);
                            if (YDiff > 0)
                                cellLoc[curCell->getCellId()].Y += 2;
                            else
                                cellLoc[curCell->getCellId()].Y -= 2;
                        }
                    }
                    else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                    {
                        float oriX = curMacro->X() + curMacro->getCellOffsetXInMacro(curCell);
                        float oriY = curMacro->Y() + curMacro->getCellOffsetYInMacro(curCell);

                        float XDiff = std::min(std::fabs(oriX - cellLoc[curCell->getCellId()].X), (float)5.0);

                        if (XDiff > 2)
                        {
                            float YDiff =
                                std::max(std::min(cellLoc[curCell->getCellId()].Y - oriY, (float)5.0), (float)-5.0);
                            if (YDiff > 0)
                                cellLoc[curCell->getCellId()].Y += 2;
                            else
                                cellLoc[curCell->getCellId()].Y -= 2;
                        }
                    }
                }
            }
        }
    }

    if (involvedPUs.size() > 100)
    {
        std::vector<std::thread *> threadsVec;
        threadsVec.clear();
        int eachThreadPUNum = involvedPUVec.size() / nJobs;
        std::vector<std::pair<int, int>> startEndPairs;
        startEndPairs.clear();

        for (int threadId = 0, startId = 0, endId = eachThreadPUNum; threadId < nJobs;
             threadId++, startId += eachThreadPUNum, endId += eachThreadPUNum)
        {
            if (threadId == nJobs - 1)
            {
                endId = involvedPUVec.size();
            }
            startEndPairs.emplace_back(startId, endId);
        }
        for (int threadId = 0; threadId < nJobs; threadId++)
        {
            std::thread *newThread =
                new std::thread(GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocationsWorker, placementInfo,
                                std::ref(involvedPUs), std::ref(involvedCells), std::ref(involvedPUVec),
                                std::ref(forgetRatio), std::ref(displacementLimit),
                                std::ref(startEndPairs[threadId].first), std::ref(startEndPairs[threadId].second), roundLimit, scope);
            threadsVec.push_back(newThread);
        }
        for (int threadId = 0; threadId < nJobs; threadId++)
        {
            threadsVec[threadId]->join();
            delete threadsVec[threadId];
        }
    }
    else
    {
        for (auto curPU : involvedPUVec)
        {
            if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
            {
                float cellX = curUnpackedCell->X();
                float cellY = curUnpackedCell->Y();
                DesignInfo::DesignCell *curCell = curUnpackedCell->getCell();
                if (curPU->isFixed() || curPU->isLocked())
                {
                    cellLoc[curCell->getCellId()].X = cellX;
                    cellLoc[curCell->getCellId()].Y = cellY;
                }
                else
                {
                    makeCellInLegalArea(placementInfo, cellLoc[curCell->getCellId()].X,
                                        cellLoc[curCell->getCellId()].Y);

                    const bool residual = scope && scope->contains(curCell);
                    const float weight = residual ? residualSuggestionWeight : forgetRatio;
                    if (displacementLimitEnable && !(roundLimit && roundLimit->active(curPU->getId())))
                        curPU->setSpreadLocation_WithLimitDisplacement(cellLoc[curCell->getCellId()].X,
                                                                       cellLoc[curCell->getCellId()].Y, weight,
                                                                       displacementLimit, residual);
                    else
                        curPU->setSpreadLocation(cellLoc[curCell->getCellId()].X, cellLoc[curCell->getCellId()].Y,
                                                 weight, residual);
                    if (roundLimit && roundLimit->active(curPU->getId()))
                        placementInfo->clipPaperRegionLocation(curPU);
                    placementInfo->transferCellBinInfo(curCell->getCellId(), curPU->X(), curPU->Y());
                    cellLoc[curCell->getCellId()].X = curPU->X();
                    cellLoc[curCell->getCellId()].Y = curPU->Y();
                }
            }
            else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
            {
                if (curPU->isFixed() || curPU->isLocked())
                {
                    for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                    {
                        float offsetX_InMacro, offsetY_InMacro;
                        DesignInfo::DesignCellType cellType;
                        curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);

                        float cellX = curMacro->X() + offsetX_InMacro;
                        float cellY = curMacro->Y() + offsetY_InMacro;

                        DesignInfo::DesignCell *curCell = curMacro->getCell(vId);

                        cellLoc[curCell->getCellId()].X = cellX;
                        cellLoc[curCell->getCellId()].Y = cellY;
                    }
                }
                else
                {
                    ResidualMacroSuggestions residualMembers;
                    double tmpTotalX = 0.0;
                    double tmpTotalY = 0.0;

                    int numCellsInvolvedInSpreading = 0;
                    for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                    {
                        DesignInfo::DesignCell *curCell = curMacro->getCell(vId);
                        if (involvedCells.find(curCell) != involvedCells.end())
                        {
                            float offsetX_InMacro, offsetY_InMacro;
                            DesignInfo::DesignCellType cellType;
                            curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);
                            makeCellInLegalArea(placementInfo, cellLoc[curCell->getCellId()].X,
                                                cellLoc[curCell->getCellId()].Y);
                            tmpTotalX += cellLoc[curCell->getCellId()].X - offsetX_InMacro;
                            tmpTotalY += cellLoc[curCell->getCellId()].Y - offsetY_InMacro;
                            if (scope && scope->contains(curCell))
                                residualMembers.add(cellLoc[curCell->getCellId()].X - offsetX_InMacro,
                                                    cellLoc[curCell->getCellId()].Y - offsetY_InMacro);
                            numCellsInvolvedInSpreading++;
                        }
                    }
                    const float weight = residualMembers.combine(tmpTotalX, tmpTotalY, numCellsInvolvedInSpreading, forgetRatio);
                    tmpTotalX /= (double)numCellsInvolvedInSpreading;
                    tmpTotalY /= (double)numCellsInvolvedInSpreading;
                    float curNewPUX = tmpTotalX;
                    float curNewPUY = tmpTotalY;
                    const float studyX0=curPU->X(),studyY0=curPU->Y();
                    placementInfo->legalizeXYInArea(curPU, curNewPUX, curNewPUY);

                    if (displacementLimitEnable && !(roundLimit && roundLimit->active(curPU->getId())))
                        curPU->setSpreadLocation_WithLimitDisplacement(curNewPUX, curNewPUY, weight,
                                                                       displacementLimit, residualMembers.count != 0);
                    else
                        curPU->setSpreadLocation(curNewPUX, curNewPUY, weight, residualMembers.count != 0);

                    if(macro_study::active("carry-spread-y-half") && curPU->checkHasCARRY() && macro_study::carryEligible.count(curPU->getId()))
                        curPU->setAnchorLocationAndForgetTheOriginalOne(curPU->X(),studyY0+0.5f*(curPU->Y()-studyY0));
                    const float studyXB=curPU->X(),studyYB=curPU->Y();
                    placementInfo->enforceLegalizeXYInArea(curPU);
                    macro_study::workerStep(placementInfo,curPU,studyX0,studyY0,studyXB,studyYB,curPU->X(),curPU->Y(),curPU->X(),curPU->Y());
                    for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                    {
                        float offsetX_InMacro, offsetY_InMacro;
                        DesignInfo::DesignCellType cellType;
                        curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);

                        float cellX = curMacro->X() + offsetX_InMacro;
                        float cellY = curMacro->Y() + offsetY_InMacro;

                        DesignInfo::DesignCell *curCell = curMacro->getCell(vId);
                        placementInfo->transferCellBinInfo(curCell->getCellId(), cellX, cellY);
                        cellLoc[curCell->getCellId()].X = cellX;
                        cellLoc[curCell->getCellId()].Y = cellY;
                    }
                }
            }
        }
    }
}

GeneralSpreader::SpreadRegion *GeneralSpreader::expandFromABin(PlacementInfo::PlacementBinInfo *curBin,
                                                               float capacityShrinkRatio, unsigned int numBinThr)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__); // Our Region Expanding (1.4x faster)
    if(dieView) {
        auto range=dieView->rowRange(curBin->Y()); const int die=dieView->rowDie[curBin->Y()];
        return selectDieFirstRegion(curBin,placementInfo,binGrid,capacityShrinkRatio,
            std::min(numBinThr,200u),coveredBinSet,range.first,range.second,die,
            localFailures[{curBin->X(),curBin->Y()}],dieSupply.at(die),useSimpleExpland,directionalResidual);
    }
    GeneralSpreader::SpreadRegion *resRegion =
        new GeneralSpreader::SpreadRegion(curBin, placementInfo, binGrid, capacityShrinkRatio, numBinThr);

    float binUnitSize = std::min(curBin->right() - curBin->left(), curBin->top() - curBin->bottom());
    if (!useSimpleExpland)
    {
        while (resRegion->getOverflowRatio() > capacityShrinkRatio &&
               resRegion->getBinsInRegion().size() < numBinThr && resRegion->smartFindExpandDirection(coveredBinSet))
        {
            resRegion->smartExpand(coveredBinSet);
        }
    }
    else
    {
        while (resRegion->getOverflowRatio() > capacityShrinkRatio &&
               resRegion->getBinsInRegion().size() < numBinThr && resRegion->simpleFindExpandDirection(coveredBinSet))
        {
            resRegion->simpleExpand(coveredBinSet);
        }
    }

    // assert(!resRegion->isOverflow() && "TODO: how to handle the situation that the resource is not enough.");
    return resRegion;
}

// GeneralSpreader::SpreadRegion *GeneralSpreader::expandFromABin(PlacementInfo::PlacementBinInfo *curBin,
//                                                                float capacityShrinkRatio)
// { // RippleFPGA Region Expanding
//     GeneralSpreader::SpreadRegion *resRegion =
//         new GeneralSpreader::SpreadRegion(curBin, placementInfo, binGrid, capacityShrinkRatio);

//     while (resRegion->getOverflowRatio() > capacityShrinkRatio &&
//     resRegion->simpleFindExpandDirection(coveredBinSet))
//     {
//         resRegion->simpleExpand(coveredBinSet);
//     }
//     // assert(!resRegion->isOverflow() && "TODO: how to handle the situation that the resource is not enough.");
//     return resRegion;
// }

void GeneralSpreader::SpreadRegion::addBinRegion(int newRegionTopBinY, int newRegionBottomBinY, int newRegionLeftBinX,
                                                 int newRegionRightBinX,
                                                 std::set<PlacementInfo::PlacementBinInfo *> &coveredBinSet)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    // Keep the invariant in release builds as well; reject before mutating any bins or bounds.
    if (!withinBinLimit(std::max(topBinY, newRegionTopBinY), std::min(bottomBinY, newRegionBottomBinY),
                        std::min(leftBinX, newRegionLeftBinX), std::max(rightBinX, newRegionRightBinX)))
        throw std::logic_error("Spread region expansion exceeds bin limit");
    assert(!isRegionOverlap(newRegionTopBinY, newRegionBottomBinY, newRegionLeftBinX, newRegionRightBinX));
    if (newRegionTopBinY > topBinY)
        topBinY = newRegionTopBinY;
    if (newRegionBottomBinY < bottomBinY)
        bottomBinY = newRegionBottomBinY;
    if (newRegionLeftBinX < leftBinX)
        leftBinX = newRegionLeftBinX;
    if (newRegionRightBinX > rightBinX)
        rightBinX = newRegionRightBinX;

    assert(0 <= newRegionBottomBinY);
    assert(0 <= newRegionTopBinY);
    assert((unsigned int)newRegionBottomBinY < binGrid.size());
    assert((unsigned int)newRegionTopBinY < binGrid.size());
    assert(0 <= newRegionLeftBinX);
    assert(0 <= newRegionRightBinX);
    assert((unsigned int)newRegionLeftBinX < binGrid[0].size());
    assert((unsigned int)newRegionRightBinX < binGrid[0].size());
    for (int i = newRegionBottomBinY; i <= newRegionTopBinY; i++)
        for (int j = newRegionLeftBinX; j <= newRegionRightBinX; j++)
        {
            assert(binSetInRegion.find(binGrid[i][j]) == binSetInRegion.end());
            assert(binGrid[i][j]->X() == j && binGrid[i][j]->Y() == i);
            binsInRegion.push_back(binGrid[i][j]);
            binSetInRegion.insert(binGrid[i][j]);
            assert(coveredBinSet.find(binGrid[i][j]) == coveredBinSet.end());
            coveredBinSet.insert(binGrid[i][j]);
            for (auto curCell : binGrid[i][j]->getCells())
            {
                cellsInRegion.insert(curCell);
                cellsInRegionVec.push_back(curCell);
            }
            totalCapacity += binGrid[i][j]->getSpreadingCapacity();
            totalUtilization += binGrid[i][j]->getUtilization();
        }
    overflowRatio = totalUtilization / totalCapacity;
}

void GeneralSpreader::recordSpreadedCellLocations()
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    for (auto tmpPU : placementInfo->getPlacementUnits())
    {
        tmpPU->recordSpreadLocatin();
    }
}

void GeneralSpreader::SpreadRegion::SubBox::spreadAndPartition()
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    if (level == 0)
        return;
    if (topBinY - bottomBinY < minExpandSize - 1 && rightBinX - leftBinX < minExpandSize - 1)
    {
        return;
    }

    SubBox *boxA = nullptr, *boxB = nullptr;
    if (dirIsH)
    {
        if (rightBinX - leftBinX >= minExpandSize - 1)
        {
            spreadCellsH(&boxA, &boxB);
        }
        if (!(boxA || boxB) && topBinY - bottomBinY >= minExpandSize - 1)
        {
            spreadCellsV(&boxA, &boxB);
        }
    }
    else
    {
        if (topBinY - bottomBinY >= minExpandSize - 1)
        {
            spreadCellsV(&boxA, &boxB);
        }
        if (!(boxA || boxB) && rightBinX - leftBinX >= minExpandSize - 1)
        {
            spreadCellsH(&boxA, &boxB);
        }
    }
    if (boxA)
    {
        boxA->spreadAndPartition();
    }
    if (boxB)
    {
        boxB->spreadAndPartition();
    }
    if (boxA)
        delete boxA;
    if (boxB)
        delete boxB;
}

// Only queried for strips whose existing spreading supply is zero. A physically
// populated strip with a reduced target must keep the legacy allocation rules.
bool GeneralSpreader::SpreadRegion::SubBox::physicallyEmptyStrip(bool horizontal, int strip) const
{
    if (logicMask)
        return horizontal ? logicMask->empty(strip, strip, bottomBinY, topBinY)
                          : logicMask->empty(leftBinX, rightBinX, strip, strip);
    const int first = horizontal ? bottomBinY : leftBinX;
    const int last = horizontal ? topBinY : rightBinX;
    for (int i = first; i <= last; ++i)
    {
        const auto *bin = horizontal ? binGrid[i][strip] : binGrid[strip][i];
        if (bin->getNominalCapacity() > 0)
            return false;
    }
    return true;
}

// There is only one receiving strip, so a normal two-way cut cannot run.
// Project suggestions from adjoining empty strips just inside its near edge.
// Keep the other coordinate, order within the strip, and PU writeback intact.
void GeneralSpreader::SpreadRegion::SubBox::absorbAdjacentEmptyStrips(bool horizontal, int strip)
{
    int first = strip, last = strip;
    while (first > (horizontal ? leftBinX : bottomBinY) && physicallyEmptyStrip(horizontal, first - 1))
        --first;
    while (last < (horizontal ? rightBinX : topBinY) && physicallyEmptyStrip(horizontal, last + 1))
        ++last;
    if (first == last)
        return;

    const auto lower = [&](int i) {
        return horizontal ? binGrid[bottomBinY][i]->left() : binGrid[i][leftBinX]->bottom();
    };
    const auto upper = [&](int i) {
        return horizontal ? binGrid[bottomBinY][i]->right() : binGrid[i][leftBinX]->top();
    };
    const float lo = lower(strip), hi = upper(strip);
    const float from = lower(first), until = upper(last);
    const float inset = std::min(1e-3f, (hi - lo) * .25f);
    auto &locations = placementInfo->getCellId2location();
    for (int id : cellIds)
    {
        float &value = horizontal ? locations[id].X : locations[id].Y;
        if (value >= from && value < lo)
            value = lo + inset;
        else if (value >= hi && value < until)
            value = hi - inset;
    }
}

void GeneralSpreader::SpreadRegion::SubBox::spreadCellsH(SubBox **boxA, SubBox **boxB)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    // refer to paper of POLAR and RippleFPGA

    if (cellIds.size() == 0)
        return;
    if (cellIds.size() > 1)
        quick_sort(cellIds, 0, cellIds.size() - 1, true);

    std::vector<float> colCapacity(rightBinX - leftBinX + 1, 0.0);
    float totalCapacity = 0;

    assert(leftBinX >= 0);
    assert(bottomBinY >= 0);
    assert((unsigned int)topBinY < binGrid.size());
    assert((unsigned int)rightBinX < binGrid[topBinY].size());
    // calculate capacity
    for (int binX = leftBinX; binX <= rightBinX; binX++)
    {
        for (int y = bottomBinY; y <= topBinY; y++)
        {
            float binCapacity = capacityShrinkRatio * binGrid[y][binX]->getSpreadingCapacity();
            colCapacity[binX - leftBinX] += binCapacity;
            totalCapacity += binCapacity;
        }
    }

    // get the boundary of the sub boxes
    //      @        |  |         @
    //      @  boxA  |  |   boxB  @
    //      @        |  |         @
    int boxALeft = leftBinX;
    int boxBRight = rightBinX;
    for (int binX = leftBinX; binX <= rightBinX; binX++, boxALeft++)
    {
        if (colCapacity[binX - leftBinX] > 0)
            break;
    }
    for (int binX = rightBinX; binX >= leftBinX; binX--, boxBRight--)
    {
        if (colCapacity[binX - leftBinX] > 0)
            break;
    }
    if (boxBRight <= boxALeft)
    {
        if (skipEmptyLogicStrips && boxBRight == boxALeft)
            absorbAdjacentEmptyStrips(true, boxALeft);
        return;
    }

    //      |        @  @         |
    //      |  boxA  @  @   boxB  |
    //      |        @  @         |
    int boxARight = boxALeft;
    int boxBLeft = boxBRight;
    float leftCapacity = colCapacity[boxARight - leftBinX];
    float rightCapacity = colCapacity[boxBLeft - leftBinX];
    while (boxARight < boxBLeft - 1)
    {
        if (leftCapacity <= rightCapacity)
        {
            boxARight++;
            leftCapacity += colCapacity[boxARight - leftBinX];
        }
        else
        {
            boxBLeft--;
            rightCapacity += colCapacity[boxBLeft - leftBinX];
        }
    }

    // splits the cells into two part
    float leftUtilRatio = leftCapacity / totalCapacity;
    float totalUtilization = 0;
    for (auto cellId : cellIds)
    {
        totalUtilization += placementInfo->getActualOccupationByCellId(cellId);
    }
    float leftUtil = totalUtilization * leftUtilRatio;

    int cutLineIdX = -1;
    if (leftUtil > eps)
    {
        for (auto cellId : cellIds)
        {
            leftUtil -= placementInfo->getActualOccupationByCellId(cellId);
            cutLineIdX++;
            if (leftUtil <= eps)
                break;
        }
    }

    // assign cells into two subbox A and B
    *boxA = new SubBox(this, topBinY, bottomBinY, boxALeft, boxARight, 0, cutLineIdX);
    *boxB = new SubBox(this, topBinY, bottomBinY, boxBLeft, boxBRight, cutLineIdX + 1, cellIds.size() - 1);
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();

    float overallOverflowRatio = totalUtilization / totalCapacity;
    if (overallOverflowRatio < 1)
        overallOverflowRatio = 1;

    std::vector<int> &boxACellIds = (*boxA)->cellIds;
    if (boxACellIds.size() > 0)
    {
        int cellInBinHead = boxACellIds.size() - 1;
        int cellInBinTail = boxACellIds.size() - 1;

        // spread cells' location in boxA

        for (int binX = boxARight; binX >= boxALeft; binX--)
        {
            if (cellInBinTail < 0)
                break;
            if (skipEmptyLogicStrips && colCapacity[binX - leftBinX] == 0 &&
                physicallyEmptyStrip(true, binX))
                continue;

            float cArea = 0;
            int num = 0;
            float oldLoX = cellLoc[boxACellIds[cellInBinTail]].X;
            float oldHiX = oldLoX;
            for (cellInBinHead = cellInBinTail;
                 cellInBinHead >= 0 &&
                 (binX == boxALeft || (cArea / overallOverflowRatio <= colCapacity[binX - leftBinX] + eps));
                 --cellInBinHead)
            {
                cArea += placementInfo->getActualOccupationByCellId(boxACellIds[cellInBinHead]);
                oldLoX = cellLoc[boxACellIds[cellInBinHead]].X;
                num++;
            }
            cellInBinHead++;
            if (num > 0)
            {
                assert(binX >= 0);
                assert((unsigned int)binX < binGrid[bottomBinY].size());
                float newLoX = binGrid[bottomBinY][binX]->left() + placementInfo->getBinGridW() / (num + 1.0);
                float newHiX = binGrid[bottomBinY][binX]->right() - placementInfo->getBinGridW() / (num + 1.0);
                float rangeold = oldHiX - oldLoX;
                float rangenew = newHiX - newLoX;
                assert(rangeold > -1e-4);
                assert(rangenew > -1e-4);
                if (fabs(rangeold) < 1e-5 || rangeold < 0)
                {
                    float newLoc = (newLoX + newHiX) / 2;
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxACellIds[ci]].X = newLoc;
                    }
                }
                else
                {
                    float scale = rangenew / rangeold;
                    assert(scale > -1e-5);
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxACellIds[ci]].X = newLoX + (cellLoc[boxACellIds[ci]].X - oldLoX) * scale;
                    }
                }
            }
            cellInBinTail = cellInBinHead - 1;
        }
    }
    else
    {
        delete *boxA;
        *boxA = nullptr;
    }

    std::vector<int> &boxBCellIds = (*boxB)->cellIds;

    if (boxBCellIds.size() > 0)
    {
        int cellInBinHead = 0;
        int cellInBinTail = 0;

        // spread cells' location in boxB
        for (int binX = boxBLeft; binX <= boxBRight; binX++)
        {
            if ((unsigned int)cellInBinHead >= boxBCellIds.size())
                break;
            if (skipEmptyLogicStrips && colCapacity[binX - leftBinX] == 0 &&
                physicallyEmptyStrip(true, binX))
                continue;

            float cArea = 0;
            int num = 0;
            float oldLoX = cellLoc[boxBCellIds[cellInBinHead]].X;
            float oldHiX = oldLoX;

            for (cellInBinTail = cellInBinHead;
                 (unsigned int)cellInBinTail < boxBCellIds.size() &&
                 (binX == boxBRight || (cArea / overallOverflowRatio <= colCapacity[binX - leftBinX] + eps));
                 cellInBinTail++)
            {
                cArea += placementInfo->getActualOccupationByCellId(boxBCellIds[cellInBinTail]);
                oldHiX = cellLoc[boxBCellIds[cellInBinTail]].X;
                num++;
            }
            cellInBinTail--;

            assert(cellLoc[boxBCellIds[cellInBinTail]].X <= oldHiX + 1e-5);
            if (num > 0)
            {
                assert(binX >= 0);
                assert((unsigned int)binX < binGrid[bottomBinY].size());
                float newLoX = binGrid[bottomBinY][binX]->left() + placementInfo->getBinGridW() / (num + 1.0);
                float newHiX = binGrid[bottomBinY][binX]->right() - placementInfo->getBinGridW() / (num + 1.0);
                float rangeold = oldHiX - oldLoX;
                float rangenew = newHiX - newLoX;
                assert(rangeold > -1e-4);
                assert(rangenew > -1e-4);
                if (fabs(rangeold) < 1e-5 || rangeold < 0)
                {
                    float newLoc = (newLoX + newHiX) / 2;
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxBCellIds[ci]].X = newLoc;
                    }
                }
                else
                {
                    float scale = rangenew / rangeold;
                    assert(scale > -1e-5);
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxBCellIds[ci]].X = newLoX + (cellLoc[boxBCellIds[ci]].X - oldLoX) * scale;
                    }
                }
            }
            cellInBinHead = cellInBinTail + 1;
        }
    }
    else
    {
        delete *boxB;
        *boxB = nullptr;
    }

    return;
}

void GeneralSpreader::SpreadRegion::SubBox::spreadCellsV(SubBox **boxA, SubBox **boxB)
{
    AMF_PROFILE_FUNCTION("spreading");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    // refer to paper of POLAR and RippleFPGA
    if (cellIds.size() == 0)
        return;

    if (cellIds.size() > 1)
        quick_sort(cellIds, 0, cellIds.size() - 1, false);

    std::vector<float> colCapacity(topBinY - bottomBinY + 1, 0.0);
    float totalCapacity = 0;

    assert(leftBinX >= 0);
    assert(bottomBinY >= 0);
    assert((unsigned int)topBinY < binGrid.size());
    assert((unsigned int)rightBinX < binGrid[topBinY].size());
    // calculate capacity
    for (int binY = bottomBinY; binY <= topBinY; binY++)
    {
        for (int binX = leftBinX; binX <= rightBinX; binX++)
        {
            float binCapacity = capacityShrinkRatio * binGrid[binY][binX]->getSpreadingCapacity();
            colCapacity[binY - bottomBinY] += binCapacity;
            totalCapacity += binCapacity;
        }
    }

    // get the boundary of the sub boxes
    //      @ @ @ @ @ @
    //
    //         boxB
    //
    //      ----------
    //      ----------
    //
    //         boxA
    //
    //      @ @ @ @ @ @
    int boxABottom = bottomBinY;
    int boxBTop = topBinY;
    for (int binY = bottomBinY; binY <= topBinY; binY++, boxABottom++)
    {
        if (colCapacity[binY - bottomBinY] > 0)
            break;
    }
    for (int binY = topBinY; binY >= bottomBinY; binY--, boxBTop--)
    {
        if (colCapacity[binY - bottomBinY] > 0)
            break;
    }
    if (boxBTop <= boxABottom)
    {
        if (skipEmptyLogicStrips && boxBTop == boxABottom)
            absorbAdjacentEmptyStrips(false, boxABottom);
        return;
    }

    //      ----------
    //
    //         boxB
    //
    //      @ @ @ @ @ @
    //      @ @ @ @ @ @
    //
    //
    //         boxA
    //
    //      ----------
    int boxATop = boxABottom;
    int boxBBottom = boxBTop;
    float bottomCapacity = colCapacity[boxATop - bottomBinY];
    float topCapacity = colCapacity[boxBBottom - bottomBinY];
    while (boxATop < boxBBottom - 1)
    {
        if (bottomCapacity <= topCapacity)
        {
            boxATop++;
            bottomCapacity += colCapacity[boxATop - bottomBinY];
        }
        else
        {
            boxBBottom--;
            topCapacity += colCapacity[boxBBottom - bottomBinY];
        }
    }
    // if (leftBinX == 26 && rightBinX == 26 && topBinY == 180 && bottomBinY == 179)
    // {
    //     std::cout << "boxATop=" << boxATop << "\n";
    //     std::cout << "boxABottom=" << boxABottom << "\n";
    //     std::cout << "boxBTop=" << boxBTop << "\n";
    //     std::cout << "boxBBottom=" << boxBBottom << "\n";
    //     std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    //     for (auto cellId : cellIds)
    //     {
    //         std::cout << "cell#" << cellId << " locX:" << cellLoc[cellId].X << " locY:" << cellLoc[cellId].Y << "\n";
    //     }
    // }
    // splits the cells into two part
    float bottomUtilRatio = bottomCapacity / totalCapacity;
    float totalUtilization = 0;
    for (auto cellId : cellIds)
    {
        totalUtilization += placementInfo->getActualOccupationByCellId(cellId);
    }
    float bottomUtil = totalUtilization * bottomUtilRatio;

    int cutLineIdX = -1;
    if (bottomUtil > eps)
    {
        for (auto cellId : cellIds)
        {
            bottomUtil -= placementInfo->getActualOccupationByCellId(cellId);
            cutLineIdX++;
            if (bottomUtil <= eps)
                break;
        }
    }

    // assign cells into two subbox A and B
    *boxA = new SubBox(this, boxATop, boxABottom, leftBinX, rightBinX, 0, cutLineIdX);
    *boxB = new SubBox(this, boxBTop, boxBBottom, leftBinX, rightBinX, cutLineIdX + 1, cellIds.size() - 1);
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();

    float overallOverflowRatio = totalUtilization / totalCapacity;
    if (overallOverflowRatio < 1)
        overallOverflowRatio = 1;
    std::vector<int> &boxACellIds = (*boxA)->cellIds;

    if (boxACellIds.size() > 0)
    {
        int cellInBinHead = boxACellIds.size() - 1;
        int cellInBinTail = boxACellIds.size() - 1;
        // spread cells' location in boxA

        for (int binY = boxATop; binY >= boxABottom; binY--)
        {
            if (cellInBinTail < 0)
                break;
            if (skipEmptyLogicStrips && colCapacity[binY - bottomBinY] == 0 &&
                physicallyEmptyStrip(false, binY))
                continue;
            float cArea = 0;
            int num = 0;
            float oriBottomY = cellLoc[boxACellIds[cellInBinTail]].Y;
            float oriTopY = oriBottomY;
            for (cellInBinHead = cellInBinTail;
                 cellInBinHead >= 0 &&
                 (binY == boxABottom || (cArea / overallOverflowRatio <= colCapacity[binY - bottomBinY] + eps));
                 --cellInBinHead)
            {
                cArea += placementInfo->getActualOccupationByCellId(boxACellIds[cellInBinHead]);
                oriBottomY = cellLoc[boxACellIds[cellInBinHead]].Y;
                num++;
            }
            cellInBinHead++;
            if (num > 0)
            {
                assert(binY >= 0);
                assert((unsigned int)binY < binGrid.size());
                auto bounds = die_spread::verticalBounds(binGrid[binY][leftBinX],placementInfo->getBinGridH(),num,seamAware);
                float newBottomY=bounds.first, newTopY=bounds.second;
                assert(newTopY >= newBottomY);
                float rangeold = oriTopY - oriBottomY;
                float rangenew = newTopY - newBottomY;
                assert(rangeold > -1e-4);
                assert(rangenew > -1e-4);
                if (fabs(rangeold) < 1e-5 || rangeold < 0)
                {
                    float newBottomc = (newBottomY + newTopY) / 2;
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxACellIds[ci]].Y = newBottomc;
                    }
                }
                else
                {
                    float scale = rangenew / rangeold;
                    assert(scale > -1e-5);
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxACellIds[ci]].Y = newBottomY + (cellLoc[boxACellIds[ci]].Y - oriBottomY) * scale;
                    }
                }
            }
            cellInBinTail = cellInBinHead - 1;
        }
    }
    else
    {
        delete *boxA;
        *boxA = nullptr;
    }

    std::vector<int> &boxBCellIds = (*boxB)->cellIds;

    if (boxBCellIds.size() > 0)
    {
        int cellInBinHead = 0;
        int cellInBinTail = 0;

        // spread cells' location in boxB
        for (int binY = boxBBottom; binY <= boxBTop; binY++)
        {
            if ((unsigned int)cellInBinHead >= boxBCellIds.size())
                break;
            if (skipEmptyLogicStrips && colCapacity[binY - bottomBinY] == 0 &&
                physicallyEmptyStrip(false, binY))
                continue;

            float cArea = 0;
            int num = 0;
            float oriBottomY = cellLoc[boxBCellIds[cellInBinHead]].Y;
            float oriTopY = oriBottomY;

            for (cellInBinTail = cellInBinHead;
                 (unsigned int)cellInBinTail < boxBCellIds.size() &&
                 (binY == boxBTop || (cArea / overallOverflowRatio <= colCapacity[binY - bottomBinY] + eps));
                 cellInBinTail++)
            {
                cArea += placementInfo->getActualOccupationByCellId(boxBCellIds[cellInBinTail]);
                oriTopY = cellLoc[boxBCellIds[cellInBinTail]].Y;
                num++;
            }
            cellInBinTail--;
            if (num > 0)
            {
                assert(binY >= 0);
                assert((unsigned int)binY < binGrid.size());
                auto bounds = die_spread::verticalBounds(binGrid[binY][leftBinX],placementInfo->getBinGridH(),num,seamAware);
                float newBottomY=bounds.first, newTopY=bounds.second;
                float rangeold = oriTopY - oriBottomY;
                float rangenew = newTopY - newBottomY;
                assert(rangeold > -1e-4);
                assert(rangenew > -1e-4);
                if (fabs(rangeold) < 1e-5 || rangeold < 0)
                {
                    float newBottomc = (newBottomY + newTopY) / 2;
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxBCellIds[ci]].Y = newBottomc;
                    }
                }
                else
                {
                    float scale = rangenew / rangeold;
                    assert(scale > -1e-5);
                    for (int ci = cellInBinHead; ci <= cellInBinTail; ++ci)
                    {
                        cellLoc[boxBCellIds[ci]].Y = newBottomY + (cellLoc[boxBCellIds[ci]].Y - oriBottomY) * scale;
                    }
                }
            }
            cellInBinHead = cellInBinTail + 1;
        }
    }
    else
    {
        delete *boxB;
        *boxB = nullptr;
    }

    return;
}

const std::string currentDateTime()
{
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

void GeneralSpreader::dumpSiteGridDensity(std::string dumpFileName)
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    dumpFileName =
        dumpFileName + "-" + sharedCellType + "-" + currentDateTime() + "-" + std::to_string(dumpSiteGridDensityCnt);
    print_status("GeneralSpreader: dumping density to: " + dumpFileName);
    std::vector<std::vector<PlacementInfo::PlacementBinInfo *>> &curBinGrid =
        placementInfo->getBinGrid(placementInfo->getSharedBELTypeId(sharedCellType));

    std::ofstream outfile0(dumpFileName.c_str());
    assert(outfile0.is_open() && outfile0.good() &&
           "The path for site density dumping does not exist and please check your path settings");
    for (auto &row : curBinGrid)
    {
        for (auto curBin : row)
        {
            outfile0 << curBin->getRealUtilizationRate() << " ";
        }
        outfile0 << "\n";
    }
    outfile0.close();
    dumpSiteGridDensityCnt++;
}

void GeneralSpreader::dumpLUTFFCoordinate()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    if (JSONCfg.find("DumpLUTFFCoordTrace-GeneralSpreader") != JSONCfg.end())
    {
        std::string dumpFile = JSONCfg["DumpLUTFFCoordTrace-GeneralSpreader"] + "-" + sharedCellType + "-" +
                               std::to_string(LUTFFCoordinateDumpCnt) + ".gz";
        print_status("GeneralSpreader: dumping coordinate archieve to: " + dumpFile);
        LUTFFCoordinateDumpCnt++;
        if (dumpFile != "")
        {
            std::stringstream outfile0;
            for (auto curPU : placementInfo->getPlacementUnits())
            {
                if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
                {
                    float cellX = curUnpackedCell->X();
                    float cellY = curUnpackedCell->Y();
                    DesignInfo::DesignCell *curCell = curUnpackedCell->getCell();
                    if (curCell->isFF() || curCell->isLUT())
                    {
                        outfile0 << cellX << " " << cellY << " " << curCell->getName() << "\n";
                    }
                }
                else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                {
                    for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
                    {
                        float offsetX_InMacro, offsetY_InMacro;
                        DesignInfo::DesignCellType cellType;
                        curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);
                        float cellX = curMacro->X() + offsetX_InMacro;
                        float cellY = curMacro->Y() + offsetY_InMacro;

                        if (DesignInfo::isFF(cellType) || DesignInfo::isLUT(cellType))
                        {
                            if (curMacro->getCell(vId))
                                outfile0 << cellX << " " << cellY << " " << curMacro->getCell(vId)->getName() << "\n";
                            else
                                outfile0 << cellX << " " << cellY << "\n";
                        }
                    }
                }
            }
            writeStrToGZip(dumpFile, outfile0);
            print_status("GeneralSpreader: dumped coordinate archieve to: " + dumpFile);
        }
    }
}

std::ostream &operator<<(std::ostream &os, GeneralSpreader::SpreadRegion::SubBox *curBox)
{
    os << "Box: Top:" << curBox->top() << " Bottom:" << curBox->bottom() << " Left:" << curBox->left()
       << " Right:" << curBox->right();
    return os;
}

void GeneralSpreader::DumpCellsCoordinate(std::string dumpFileName, GeneralSpreader::SpreadRegion *curRegion)
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    if (dumpFileName != "")
    {
        dumpCnt++;
        dumpFileName = dumpFileName + "-" + std::to_string(dumpCnt) + ".gz";
        // print_status("GeneralSpreader: dumping coordinate archieve to: " + dumpFileName);
        std::stringstream outfile0;
        std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
        for (auto tmpCell : curRegion->getCells())
        {
            outfile0 << cellLoc[tmpCell->getCellId()].X << " " << cellLoc[tmpCell->getCellId()].Y << tmpCell << "\n";
        }
        writeStrToGZip(dumpFileName, outfile0);
        // print_status("GeneralSpreader: dumped coordinate archieve to: " + dumpFileName);
    }
}

void GeneralSpreader::DumpPUCoordinate(std::string dumpFileName,
                                       std::vector<PlacementInfo::PlacementUnit *> &involvedPUVec)
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    phase_wait::Scope phaseWaitScope(__PRETTY_FUNCTION__);
    if (dumpFileName != "")
    {
        dumpCnt++;
        dumpFileName = dumpFileName + "-" + std::to_string(dumpCnt) + ".gz";
        // print_status("GeneralSpreader: dumping coordinate archieve to: " + dumpFileName);
        std::stringstream outfile0;
        for (auto curPU : involvedPUVec)
        {
            outfile0 << curPU->X() << " " << curPU->Y() << " " << curPU << "\n";
        }
        writeStrToGZip(dumpFileName, outfile0);
        // print_status("GeneralSpreader: dumped coordinate archieve to: " + dumpFileName);
    }
}


GeneralSpreader::SpreadRegion *GeneralSpreader::selectDieFirstRegion(
    PlacementInfo::PlacementBinInfo *seed, PlacementInfo *placement, die_spread::Grid &grid,
    float ratio, unsigned limit, std::set<PlacementInfo::PlacementBinInfo *> &covered,
    int minRow, int maxRow, int die, const die_spread::Failure &failure,
    const die_spread::GridView::Supply &supply, bool simple, bool firstLocalOnly)
{
    limit=std::min(limit,200u);
    auto search=[&](SpreadRegion &r,std::set<die_spread::Bin*> &claimed,bool exhaust){
        while(exhaust || r.getOverflowRatio()>ratio) {
            if(!(simple?r.simpleFindExpandDirection(claimed):r.smartFindExpandDirection(claimed))) break;
            const auto size=r.getBinsInRegion().size();
            if(simple)r.simpleExpand(claimed);else r.smartExpand(claimed);
            if(r.getBinsInRegion().size()<=size) throw std::logic_error("Non-progressing spread search");
        }
    };
    // Probing a failed local candidate must not reserve its bins against fallback.
    auto localCovered=covered;
    std::unique_ptr<SpreadRegion> local(new SpreadRegion(seed,placement,grid,ratio,limit,minRow,maxRow));
    search(*local,localCovered,!firstLocalOnly && failure.needsWiderLocal);
    auto &d=local->decision;d.local=true;d.seedX=seed->X();d.seedY=seed->Y();d.die=die;
    d.localBins=local->getBinsInRegion().size();d.localCapacity=0;d.localDemand=0;
    for(auto b:local->getBinsInRegion()){d.localCapacity+=b->getSpreadingCapacity();d.localDemand+=b->getUtilization();}
    const bool insufficient=d.localDemand>ratio*d.localCapacity;
    d.boundedLocal=insufficient || failure.needsWiderLocal;
    d.localStop=d.boundedLocal?(local->budgetBlocked?"region_budget_or_strip":"no_local_direction"):"capacity_satisfied";
    d.dieDemand=supply.demand;d.dieNominal=supply.nominal;d.dieEffective=supply.effective;
    d.reason="local_capacity_satisfied";
    if(firstLocalOnly) {
        d.reason=insufficient?"first_local_partial_supply":"first_local_capacity_satisfied";
        covered.swap(localCovered);return local.release();
    }
    if(!insufficient && !failure.exhaustedLocalWritten) {
        if(failure.needsWiderLocal)d.reason="wider_local_writeback_trial";
        covered.swap(localCovered);return local.release();
    }
    if(insufficient) {
        if(supply.demand>supply.nominal+1e-5)d.reason="die_resource_capacity_shortage";
        else if(supply.demand>ratio*supply.effective+1e-5)d.reason="die_target_supply_shortage";
        else d.reason="local_capacity_search_exhausted";
    } else d.reason="local_writeback_overflow_after_bounded_search";
    std::unique_ptr<SpreadRegion> cross(new SpreadRegion(seed,placement,grid,ratio,limit));
    cross->decision=d;cross->decision.local=false;
    // Rebuild from the same seed; never append to a full 200-bin local rectangle.
    search(*cross,covered,false);
    return cross.release();
}

namespace {
using PU=PlacementInfo::PlacementUnit;
using Loc=PlacementInfo::Location;
double spreadOverflow(die_spread::Bin *b,float ratio) {
    return std::max(0.,double(b->getUtilization())-b->spreadingDemandLimit(ratio, true));
}
std::string spreadSafe(std::string s) {for(char &c:s)if(c=='\t'||c=='\n'||c=='\r')c=' ';return s;}
}

void GeneralSpreader::activateCrossDieLimits()
{
    localWritePUs.clear();
    if (!dieView) return;
    crossTargetDie.clear();
    for (auto region : expandedRegions) {
        const auto &d=region->decision;
        for (auto cell : region->getCells()) {
            auto pu=placementInfo->getPlacementUnitByCell(cell);
            const int id=pu->getId();
            if (d.local || (directionalResidual && !d.crossEligible.count(id))) {
                localWritePUs.insert(id);
            } else if (directionalResidual) {
                auto inserted=crossTargetDie.emplace(id,d.targetDie);
                if (!inserted.second && inserted.first->second!=d.targetDie) localWritePUs.insert(id);
            }
        }
    }
    if (!roundLimit || !roundLimit->enabled()) return;
    for (auto region : expandedRegions) if (!region->decision.local)
        for (auto cell : region->getCells()) {
            auto pu = placementInfo->getPlacementUnitByCell(cell);
            if (!localWritePUs.count(pu->getId()) && !pu->isFixed() && !pu->isLocked())
                roundLimit->activate(pu->getId());
        }
    // Activation lasts for the whole round. Later resource/local passes must
    // not reset the reference or let a previously cross-die PU escape its disk.
}

void GeneralSpreader::constrainCrossDieWrites(const std::vector<PU*> &units,
                                             const std::map<int,Loc> &before)
{
    if (!roundLimit || !roundLimit->enabled()) return;
    const auto &local = localWritePUs;
    for (auto pu : units) {
        const int id = pu->getId();
        if (pu->isFixed() || pu->isLocked() || !roundLimit->active(id)) continue;
        CrossDieSpreadLimit::Point target{pu->X(), pu->Y()};
        if (roundLimit->contains(id, target)) continue;
        const auto old = before.at(id);
        const bool localOnly = local.count(id);
        die_spread::Range range;
        if (localOnly)
            spreadMembers(pu, [&](DesignInfo::DesignCell*, float, float y) {
                range.includeMember(dieView->seams, old.Y + y, y);
            });
        auto valid = [&]() {
            bool ok = roundLimit->contains(id, {pu->X(), pu->Y()});
            if (localOnly) {
                ok &= range.contains(pu->Y());
                spreadMembers(pu, [&](DesignInfo::DesignCell*, float, float y) {
                    ok &= die_spread::dieAt(dieView->seams, old.Y + y) ==
                          die_spread::dieAt(dieView->seams, pu->Y() + y);
                });
            }
            return ok;
        };
        auto projectLegal = [&](CrossDieSpreadLimit::Point p) {
            pu->setAnchorLocationAndForgetTheOriginalOne(p.x, p.y);
            placementInfo->enforceLegalizeXYInArea(pu);
            placementInfo->clipPaperRegionLocation(pu);
            if (localOnly) {
                pu->setAnchorLocationAndForgetTheOriginalOne(pu->X(),
                    std::max(range.low, std::min(range.high, pu->Y())));
                placementInfo->enforceLegalizeXYInArea(pu);
                placementInfo->clipPaperRegionLocation(pu);
            }
        };
        projectLegal(roundLimit->project(id, target));
        ++roundLimit->limitedWrites;
        if (!valid()) {
            // Non-convex hard regions or preference projections can undo the
            // radial clip. Retain a feasible preceding anchor, then try the
            // round origin; never silently enlarge the configured radius.
            ++roundLimit->rejectedWrites;
            projectLegal({old.X, old.Y});
            if (!valid()) projectLegal(roundLimit->origin(id));
            // A soft paper preference can point outside both the start die and
            // this round's disk. Retain a preceding hard-legal anchor rather
            // than reapplying that soft projection until it throws.
            if(!valid() && directionalResidual && roundLimit->contains(id,{old.X,old.Y})) {
                pu->setAnchorLocationAndForgetTheOriginalOne(old.X,old.Y);
                placementInfo->enforceLegalizeXYInArea(pu);
            }
            if (!valid()) {
                pu->setAnchorLocationAndForgetTheOriginalOne(old.X, old.Y);
                throw std::runtime_error("No legal cross-die spread placement within round budget for PU " +
                                         pu->getName());
            }
        }
        spreadMembers(pu, [&](DesignInfo::DesignCell *cell, float x, float y) {
            const float cellX = pu->X() + x, cellY = pu->Y() + y;
            placementInfo->transferCellBinInfo(cell->getCellId(), cellX, cellY);
            placementInfo->getCellId2location()[cell->getCellId()] = {cellX, cellY};
        });
    }
}

void GeneralSpreader::constrainLocalWrites(const std::vector<PU*> &units,const std::map<int,Loc> &before)
{
    const auto &local = localWritePUs;
    for (auto pu:units) {
        auto target=crossTargetDie.find(pu->getId());
        if(target==crossTargetDie.end() || local.count(pu->getId()) || pu->isFixed() || pu->isLocked()) continue;
        const auto old=before.at(pu->getId());
        bool allowed=true;
        spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float offset){
            const int from=die_spread::dieAt(dieView->seams,old.Y+offset);
            const int to=die_spread::dieAt(dieView->seams,pu->Y()+offset);
            if(to!=from && to!=target->second) allowed=false;
        });
        if(!allowed) {
            pu->setAnchorLocationAndForgetTheOriginalOne(old.X,old.Y);
            spreadMembers(pu,[&](DesignInfo::DesignCell *cell,float x,float y){
                placementInfo->transferCellBinInfo(cell->getCellId(),old.X+x,old.Y+y);
                placementInfo->getCellId2location()[cell->getCellId()]={old.X+x,old.Y+y};
            });
        }
    }
    for(auto pu:units)if(local.count(pu->getId())&&!pu->isFixed()&&!pu->isLocked()) {
        const auto old=before.at(pu->getId());die_spread::Range range;
        spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float y){range.includeMember(dieView->seams,old.Y+y,y);});
        bool sameDie=true;
        spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float offset){
            sameDie=sameDie && die_spread::dieAt(dieView->seams,old.Y+offset)==die_spread::dieAt(dieView->seams,pu->Y()+offset);
        });
        if(!range.contains(pu->Y()) || !sameDie) {
            float y=std::max(range.low,std::min(range.high,pu->Y()));
            pu->setAnchorLocationAndForgetTheOriginalOne(pu->X(),y);
            placementInfo->enforceLegalizeXYInArea(pu);
            placementInfo->clipPaperRegionLocation(pu);
            bool valid=range.contains(pu->Y());
            spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float offset){
                valid=valid && die_spread::dieAt(dieView->seams,old.Y+offset)==die_spread::dieAt(dieView->seams,pu->Y()+offset);
            });
            // Prefer a retained legal original placement to violating a region,
            // macro footprint, or original die membership due to rounding.
            if(!valid)pu->setAnchorLocationAndForgetTheOriginalOne(old.X,old.Y);
            spreadMembers(pu,[&](DesignInfo::DesignCell *cell,float x,float y){
                placementInfo->transferCellBinInfo(cell->getCellId(),pu->X()+x,pu->Y()+y);
                placementInfo->getCellId2location()[cell->getCellId()]={pu->X()+x,pu->Y()+y};
            });
        }
    }
}

void GeneralSpreader::finishDieFirstIteration(int call,int inner,const std::vector<PU*> &units,
                                             const std::map<int,Loc> &before)
{
    std::map<int,int> crossings;std::set<int> local;
    for(auto r:expandedRegions)if(r->decision.local)for(auto cell:r->getCells())
        local.insert(placementInfo->getPlacementUnitByCell(cell)->getId());
    for(auto pu:units) {
        const auto old=before.at(pu->getId());int cross=0;
        spreadMembers(pu,[&](DesignInfo::DesignCell *cell,float x,float y){
            dieView->update(cell,pu->X()+x,pu->Y()+y);
            if(!cell->isVirtualCell() && die_spread::dieAt(dieView->seams,old.Y+y)!=die_spread::dieAt(dieView->seams,pu->Y()+y))++cross;
        });
        crossings[pu->getId()]=cross;
        if(cross)crossingLog<<sharedCellType<<'\t'<<call<<'\t'<<currentIteration<<'\t'<<inner<<'\t'<<pu->getId()<<'\t'
            <<spreadSafe(pu->getName())<<'\t'<<old.X<<'\t'<<old.Y<<'\t'<<pu->X()<<'\t'<<pu->Y()<<'\t'
            <<pu->X()-old.X<<'\t'<<pu->Y()-old.Y<<'\t'<<cross<<'\t'<<local.count(pu->getId())<<'\n';
        if(cross && local.count(pu->getId()))throw std::logic_error("Die-local spreading crossed a physical seam");
    }
    if(sharedResidual) finishShared(before,call,inner);
    for (auto r:expandedRegions) {
        const auto &d=r->decision;
        if (d.residualBins.empty()) continue;
        double after=0;
        for(auto bin:d.residualBins) after+=spreadOverflow(bin,capacityShrinkRatio);
        const auto key=std::make_pair(d.seedX,d.seedY);
        if (after>=d.initialResidual-1e-6)
            failedPlans[key]={{r->left(),r->right(),r->bottom(),r->top()},after,inner};
        else failedPlans.erase(key);
        if (residualLog.is_open())
            residualLog<<sharedCellType<<'\t'<<call<<'\t'<<inner<<'\t'<<d.seedX<<'\t'<<d.seedY<<'\t'
                <<d.residualBins.size()<<'\t'<<d.initialResidual<<'\t'<<after<<'\t'
                <<r->left()<<'\t'<<r->right()<<'\t'<<r->bottom()<<'\t'<<r->top()<<'\t'<<d.reason<<'\n';
    }
    if(!amf_config::feature(JSONCfg,"DetailedDiagnosticsEnable",true)) {
        for(auto r:expandedRegions)for(auto b:r->getBinsInRegion()) {
            const auto key=std::make_pair(b->X(),b->Y());
            if(r->decision.local && b->isSpreadingOverflow(capacityShrinkRatio)) localFailures[key]={true,r->decision.boundedLocal};
            else localFailures.erase(key);
        }
        return;
    }
    size_t crossRegions=0;
    for(auto r:expandedRegions) {
        const auto &d=r->decision;double residual=0,maxMove=0,sumMove=0,maxDy=0;int maxId=-1,moved=0,cross=0;
        std::set<PU*> members;for(auto cell:r->getCells())members.insert(placementInfo->getPlacementUnitByCell(cell));
        for(auto pu:members) {
            const auto old=before.at(pu->getId());double dx=pu->X()-old.X,dy=pu->Y()-old.Y,norm=std::hypot(dx,dy);
            if(norm>1e-6)++moved;if(norm>maxMove){maxMove=norm;maxId=pu->getId();}
            maxDy=std::max(maxDy,std::abs(dy));sumMove+=norm;cross+=crossings.at(pu->getId());
        }
        for(auto b:r->getBinsInRegion()) {
            residual+=spreadOverflow(b,capacityShrinkRatio);
            const auto key=std::make_pair(b->X(),b->Y());
            if(d.local && b->isSpreadingOverflow(capacityShrinkRatio))localFailures[key]={true,d.boundedLocal};
            else localFailures.erase(key);
        }
        crossRegions+=!d.local;
        regionLog<<sharedCellType<<'\t'<<call<<'\t'<<currentIteration<<'\t'<<inner<<'\t'<<d.seedX<<'\t'<<d.seedY<<'\t'<<d.die<<'\t'
            <<(d.local?"die_local":"cross_die_search")<<'\t'<<d.reason<<'\t'<<d.localStop<<'\t'<<d.localBins<<'\t'
            <<d.localDemand<<'\t'<<d.localCapacity<<'\t'<<d.dieDemand<<'\t'<<d.dieNominal<<'\t'<<d.dieEffective<<'\t'
            <<r->getBinsInRegion().size()<<'\t'<<r->left()<<'\t'<<r->right()<<'\t'<<r->bottom()<<'\t'<<r->top()<<'\t'
            <<residual<<'\t'<<spreadOverflow(binGrid[d.seedY][d.seedX],capacityShrinkRatio)<<'\t'
            <<members.size()<<'\t'<<moved<<'\t'<<maxMove<<'\t'<<(members.empty()?0:sumMove/members.size())<<'\t'<<maxDy<<'\t'<<maxId<<'\t'<<cross<<'\n';
    }
    size_t remaining=0;double residual=0;
    for(auto &row:binGrid)for(auto b:row){remaining+=b->isSpreadingOverflow(capacityShrinkRatio);residual+=spreadOverflow(b,capacityShrinkRatio);}
    passLog<<sharedCellType<<'\t'<<call<<'\t'<<currentIteration<<'\t'<<inner<<'\t'<<remaining<<'\t'<<residual<<'\t'<<expandedRegions.size()<<'\t'<<crossRegions<<'\n';
    regionLog.flush();passLog.flush();crossingLog.flush();
    if(amf_config::feature(JSONCfg,"DetailedDiagnosticsEnable",true) && (!regionLog||!passLog||!crossingLog))throw std::runtime_error("Failed to write spreading diagnostics");
}


void GeneralSpreader::planResidualRegions(int inner,int budget,std::set<die_spread::Bin*> &handled)
{
    using namespace residual_spread;
    using Bin=die_spread::Bin;
    const Rect device{0,int(binGrid.front().size())-1,0,int(binGrid.size())-1};
    auto read=[&](int x,int y) {
        Bin *b=binGrid[y][x];
        const double used=b->getUtilization();
        const double nominal=b->spreadingDemandLimit(capacityShrinkRatio,true);
        const double effective=b->spreadingDemandLimit(capacityShrinkRatio);
        const double room=b->isSpreadingOverflow(capacityShrinkRatio)?nominal-used:std::min(nominal,effective)-used;
        const double upper=reserveFirst?b->getNominalCapacity():nominal;
        return Account{room,upper>used,coveredBinSet.count(b)!=0};
    };
    if (sharedResidual) planSharedRegions(budget,handled);
    std::set<Bin*> waiting;
    for(auto b:overflowBins) {
        const auto it=localFailures.find({b->X(),b->Y()});
        if(it==localFailures.end() || !it->second.needsWiderLocal || handled.count(b)) continue;
        if(sharedResidual) {
            int i=b->Y()*device.width()+b->X();
            if(!residualHistory.at(i).hard) continue;  // another ordinary local write
            // The quota search can stop at a blocked relay. Try the existing
            // ordinary local redistribution in that case. Cross-die fallback
            // is eligible only when there is no potentially usable local bin.
            auto rows=dieView->rowRange(b->Y());
            Rect local{device.l,device.r,rows.first,rows.second};
            if(potentialNearby({b->X(),b->Y()},local,read)) continue;
        }
        waiting.insert(b);
    }
    for(auto seed:overflowBins) {
        if(!waiting.count(seed) || coveredBinSet.count(seed)) continue;
        const Point origin{seed->X(),seed->Y()};
        const auto rows=dieView->rowRange(origin.second);
        const int die=dieView->rowDie[origin.second];
        const Rect local{device.l,device.r,rows.first,rows.second};
        Rect core=pointBox(origin);
        std::vector<Bin*> group{seed};
        std::set<Bin*> members{seed};
        // Four-connected residuals in the same die. Bound component chunks by
        // the ordinary stage budget, including holes in the enclosing rectangle.
        for(size_t i=0;i<group.size();++i) {
            const auto cur=group[i];
            for(Point step:std::array<Point,4>{{{-1,0},{1,0},{0,-1},{0,1}}}) {
                Point xy{cur->X()+step.first,cur->Y()+step.second};
                if(!local.contains(pointBox(xy))) continue;
                auto b=binGrid[xy.second][xy.first];
                if(!waiting.count(b) || members.count(b) || coveredBinSet.count(b)) continue;
                auto enlarged=core.include(xy);
                if(enlarged.area()>budget || (!(enlarged==core) && !available(enlarged,read))) continue;
                core=enlarged;
                group.push_back(b);
                members.insert(b);
            }
        }
        // Find one receiving bin if a straight boundary ray missed a diagonal
        // pocket. Only its minimum enclosing box is relevant. No PU data is read.
        auto receiverBox=[&](Rect start,int wantedDie,const Rect *avoid,Rect &result) {
            bool found=false;
            std::tuple<int,int,double,int,int> best;
            for(int y=std::max(device.b,start.t-budget+1);y<=std::min(device.t,start.b+budget-1);++y) {
                if(dieView->rowDie[y]!=wantedDie) continue;
                const int maxWidth=budget/(std::max(y,start.t)-std::min(y,start.b)+1);
                for(int x=std::max(device.l,start.r-maxWidth+1);x<=std::min(device.r,start.l+maxWidth-1);++x) {
                    const auto b=read(x,y);
                    if(b.room<=0 || b.occupied) continue;
                    auto box=start.include({x,y});
                    if(box.area()>budget || (avoid && box==*avoid)) continue;
                    auto rank=std::make_tuple(box.area(),std::max(box.width(),box.height()),-b.room,y,x);
                    if((!found || rank<best) && available(box,read)) {
                        found=true; best=rank; result=box;
                    }
                }
            }
            return found;
        };
        auto deficit=[&](){
            double total=0;
            for(auto b:group) total+=spreadOverflow(b,capacityShrinkRatio);
            return total;
        };
        double required=deficit();
        const auto it=failedPlans.find(origin);
        const Rect *avoid=nullptr;
        if(it!=failedPlans.end() && it->second.inner==inner-1 && std::abs(it->second.residual-required)<1e-6)
            avoid=&it->second.box;
        auto planLocal=[&](Rect start, Rect &box) {
            auto plan=grow(start,local,budget,read,avoid);
            box=plan.box;
            if(plan.receiver && !(avoid && box==*avoid)) return true;
            return receiverBox(start,die,avoid,box);
        };
        Rect box=core;
        bool selected=planLocal(core,box);
        // A large hot component may consume all of the stage's area. Let a
        // boundary seed use its own nearby room; leave the other seeds eligible.
        if(!selected && group.size()>1) {
            group.resize(1); core=pointBox(origin); required=deficit();
            selected=planLocal(core,box);
        }
        for(auto b:group){waiting.erase(b);handled.insert(b);}
        int targetDie=die;
        if(!selected) {
            // Check the union of all same-die <=100-bin rectangles around the
            // source, even if the current inner pass owns/blocks that room.
            if(potentialNearby(origin,local,read)) continue;
            Rect other=core;
            for(int neighbor:{die-1,die+1}) {
                if(neighbor<0 || neighbor>=int(dieSupply.size())) continue;
                if(receiverBox(core,neighbor,nullptr,other) && (!selected || other.area()<box.area())) {
                    selected=true; box=other; targetDie=neighbor;
                }
            }
        }
        if(!selected) continue;
        auto reg=std::make_unique<SpreadRegion>(seed,placementInfo,binGrid,capacityShrinkRatio,budget,box.b,box.t);
        auto &d=reg->decision;
        d.local=targetDie==die;
        if(!d.local) {
            // Only PUs in the exhausted seed, with all members in its original
            // die, may use the adjacent die. Other PUs keep their local guard.
            for(auto c:seed->getCells()) {
                auto pu=placementInfo->getPlacementUnitByCell(c);
                if(pu->isFixed() || pu->isLocked()) continue;
                bool inDie=true;
                spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float y){
                    inDie &= die_spread::dieAt(dieView->seams,pu->Y()+y)==die;
                });
                if(inDie) d.crossEligible.insert(pu->getId());
            }
            if(d.crossEligible.empty()) continue;
        }
        coveredBinSet.insert(seed);
        const int sx=origin.first,sy=origin.second;
        if(box.l<sx) reg->addBinRegion(sy,sy,box.l,sx-1,coveredBinSet);
        if(box.r>sx) reg->addBinRegion(sy,sy,sx+1,box.r,coveredBinSet);
        if(box.b<sy) reg->addBinRegion(sy-1,box.b,box.l,box.r,coveredBinSet);
        if(box.t>sy) reg->addBinRegion(box.t,sy+1,box.l,box.r,coveredBinSet);
        d.residualBins=std::move(group); d.initialResidual=required;
        d.seedX=sx; d.seedY=sy; d.die=die; d.targetDie=targetDie;
        d.reason=d.local?"grouped_local_residual":"cross_after_no_local_receiver";
        d.localStop=d.local?"residual_and_bin_margin":"no_receiver_within_100_bin_area";
        d.localBins=d.local?box.area():0;
        for(auto b:reg->getBinsInRegion()) if(d.local) {
            d.localCapacity+=b->getSpreadingCapacity(); d.localDemand+=b->getUtilization();
        }
        const auto &supply=dieSupply.at(die);
        d.dieDemand=supply.demand; d.dieEffective=supply.effective; d.dieNominal=supply.nominal;
        if(triggerStats) ++triggerCounts[sy][sx].seeds;
        expandedRegions.push_back(reg.release());
    }
}

void GeneralSpreader::planSharedRegions(int budget, std::set<die_spread::Bin*>& handled)
{
    using namespace residual_spread;
    const int width = int(binGrid.front().size()), count = int(residualHistory.size());
    auto index = [width](die_spread::Bin *b) { return b->Y()*width+b->X(); };
    auto point = [width](int i) { return Point{i%width,i/width}; };
    if (std::none_of(overflowBins.begin(),overflowBins.end(),[&](auto b){
        return residualHistory[index(b)].hard;
    })) return;

    // Locked and fixed member charges stay in their original bins throughout
    // this resource pass. Use the same integer charges as GridView::update.
    auto &charges=placementInfo->getCellId2CellBinInfo();
    if (fixedDemand.empty()) {
        fixedDemand.assign(count,0);
        for (auto pu:placementInfo->getPlacementUnits()) if (pu->isFixed() || pu->isLocked())
            spreadMembers(pu,[&](DesignInfo::DesignCell *cell,float,float) {
                const int id=cell->getCellId();
                if(auto b=dieView->memberBin(id)) fixedDemand[index(b)]+=int(charges.at(id).occupation);
            });
    }
    std::vector<shared_residual::Bin> grid(count);
    for (const auto &row:binGrid) for (auto b:row) {
        int i=index(b);
        double demand=b->getUtilization(), nominal=b->spreadingDemandLimit(capacityShrinkRatio,true);
        double ceiling=std::min(nominal,double(b->spreadingDemandLimit(capacityShrinkRatio)));
        double overflow=std::max(0.,demand-nominal);
        grid[i]={overflow>0 ? -overflow : ceiling-demand, demand-fixedDemand[i],
                 !coveredBinSet.count(b),dieView->rowDie[b->Y()],overflow};
    }
    const auto paths=shared_residual::receivers(grid,width);
    std::vector<std::vector<int>> sources(count);
    for(auto b:overflowBins) {
        int i=index(b), sink=paths.receiver[i];
        if(sink>=0) sources[sink].push_back(i);
    }
    std::vector<bool> visited(count);
    auto read=[&](int x,int y) {
        auto &b=grid[y*width+x];
        return Account{b.margin,b.margin>0,coveredBinSet.count(binGrid[y][x])!=0};
    };
    for(auto seed:overflowBins) {
        const int first=index(seed), sink=paths.receiver[first];
        if(sink<0 || visited[sink] || !residualHistory[first].hard || coveredBinSet.count(seed)) continue;
        visited[sink]=true;
        Rect core=pointBox(point(sink));
        // Include the complete path so that a relay and its receiver are
        // claimed together. Try the hardest seed first, then other claimants.
        auto include=[&](int source) {
            Rect candidate=core;
            for(int i=source;i!=sink;i=paths.parent[i]) {
                candidate=candidate.include(point(i));
                if(candidate.area()>budget) return false;
            }
            if(!available(candidate,read)) return false;
            core=candidate;
            return true;
        };
        if(!include(first)) continue;
        for(int source:sources[sink]) if(source!=first) include(source);
        auto range=dieView->rowRange(seed->Y());
        Rect domain{0,width-1,range.first,range.second};
        Rect box=grow(core,domain,budget,read).box;
        std::vector<shared_residual::Bin> local;
        std::vector<die_spread::Bin*> hot;
        each(box,[&](int x,int y) {
            auto b=binGrid[y][x];
            local.push_back(grid[index(b)]);
            if(grid[index(b)].overflow>0) hot.push_back(b);
        });
        auto allocation=shared_residual::allocate(local,box.width());
        if(allocation.relieved<=shared_residual::tolerance) continue;
        int sx=seed->X(), sy=seed->Y();
        auto region=std::make_unique<SpreadRegion>(seed,placementInfo,binGrid,capacityShrinkRatio,budget,box.b,box.t);
        region->claimResidualBox(box,coveredBinSet);
        auto &d=region->decision;
        d.local=true; d.seedX=sx; d.seedY=sy;
        d.die=d.targetDie=dieView->rowDie[sy]; d.localBins=box.area();
        d.localStop="shared_receiver_quota"; d.reason="shared_local_residual";
        d.movableTarget=std::move(allocation.target); d.plannedRelief=allocation.relieved;
        d.residualBins=std::move(hot);
        for(auto b:d.residualBins) { handled.insert(b); d.initialResidual+=grid[index(b)].overflow; }
        for(auto b:region->getBinsInRegion()) {
            d.localDemand+=b->getUtilization(); d.localCapacity+=b->getSpreadingCapacity();
        }
        const auto &supply=dieSupply.at(d.die);
        d.dieDemand=supply.demand; d.dieEffective=supply.effective; d.dieNominal=supply.nominal;
        if(triggerStats) ++triggerCounts[sy][sx].seeds;
        expandedRegions.push_back(region.release());
    }
}

// Quota-guided bisection. Work only on the finalized rectangle, and preserve
// the original order on each cut axis. Perform one normal whole-PU writeback.
void GeneralSpreader::partitionShared(SpreadRegion &region)
{
    using namespace residual_spread;
    Rect box{region.left(),region.right(),region.bottom(),region.top()};
    auto &targets=region.decision.movableTarget;
    auto &loc=placementInfo->getCellId2location();
    const auto &charges=placementInfo->getCellId2CellBinInfo();
    std::vector<int> ids;
    ids.reserve(region.getCells().size());
    for(auto cell:region.getCells()) {
        auto pu=placementInfo->getPlacementUnitByCell(cell);
        if(!pu->isFixed() && !pu->isLocked()) ids.push_back(cell->getCellId());
    }
    if (region.decision.batchWindow) {
        // A partial plan can leave demand in a physical hole. Keep only
        // existing residents there, never use that remainder as a destination.
        // All selected members still take part in the complete-PU writeback.
        bool holes=false;
        each(box,[&](int x,int y){
            if(binGrid[y][x]->getNominalCapacity()==0 && targets[(y-box.b)*box.width()+x-box.l]>0)
                holes=true;
        });
        if(holes) {
            std::vector<double> retained(targets.size());
            auto keep=[&](int id) {
                auto b=dieView->memberBin(id);
                if(b->getNominalCapacity()!=0) return false;
                size_t i=(b->Y()-box.b)*box.width()+b->X()-box.l;
                double charge=int(charges[id].occupation);
                if(std::abs(retained[i]+charge-targets[i])>=std::abs(retained[i]-targets[i])) return false;
                retained[i]+=charge;return true;
            };
            ids.erase(std::remove_if(ids.begin(),ids.end(),keep),ids.end());
            double delta=0,liveTarget=0;
            each(box,[&](int x,int y){
                size_t i=(y-box.b)*box.width()+x-box.l;
                if(binGrid[y][x]->getNominalCapacity()==0) {
                    delta+=targets[i]-retained[i];targets[i]=0;
                } else liveTarget+=targets[i];
            });
            if(liveTarget<=0 && !ids.empty()) throw std::logic_error("Batch has no physical destination");
            if(liveTarget>0) for(double &target:targets) target*=std::max(0.,1+delta/liveTarget);
        }
    }
    auto total=[&](Rect r) {
        double result=0;
        each(r,[&](int x,int y){result+=targets[(y-box.b)*box.width()+x-box.l];});
        return result;
    };
    auto fitAxis=[&](size_t begin,size_t end,bool horizontal,float low,float high) {
        auto coord=[&](int id)->float& {return horizontal?loc[id].X:loc[id].Y;};
        if (region.decision.batchWindow && region.decision.local) {
            // Scale the complete assigned group, including current bin residents.
            // Use this physical leaf's extent (a seam may split a device bin).
            const auto bounds = std::minmax_element(ids.begin() + begin, ids.begin() + end,
                [&](int a, int b) { return coord(a) < coord(b); });
            const double first = coord(*bounds.first), last = coord(*bounds.second);
            const double width = double(high) - low;
            const double margin = width / (end - begin + 1);
            const float upper = std::nextafter(high, low);
            if (last - first < 1e-5) {
                const float middle = std::clamp(float(low + width / 2), low, upper);
                for (size_t k = begin; k < end; ++k) coord(ids[k]) = middle;
            } else {
                const double scale = (width - 2 * margin) / (last - first);
                for (size_t k = begin; k < end; ++k) {
                    float &value = coord(ids[k]);
                    value = std::clamp(float(low + margin + (value - first) * scale), low, upper);
                }
            }
            return;
        }
        if(std::all_of(ids.begin()+begin,ids.begin()+end,[&](int id){
            return coord(id)>=low && coord(id)<high;
        })) return;
        std::sort(ids.begin()+begin,ids.begin()+end,[&](int a,int b){
            return coord(a)!=coord(b)?coord(a)<coord(b):a<b;
        });
        size_t left=begin,right=end;
        while(left<right && coord(ids[left])<low) ++left;
        while(right>left && coord(ids[right-1])>=high) --right;
        auto place=[&](size_t a,size_t b,float lo,float hi) {
            for(size_t k=a;k<b;++k) coord(ids[k])=std::clamp(
                float(lo+(hi-lo)*double(k-a+1)/(b-a+1)),low,std::nextafter(high,low));
        };
        if(left==right) place(begin,end,low,high);
        else { place(begin,left,low,coord(ids[left])); place(right,end,coord(ids[right-1]),high); }
    };
    auto split=[&](auto &self,Rect r,size_t begin,size_t end,bool horizontal)->void {
        if(begin==end) return;
        if(r.area()==1) {
            auto b=binGrid[r.b][r.l];
            fitAxis(begin,end,true,b->left(),b->right());
            fitAxis(begin,end,false,b->bottom(),b->top());
            return;
        }
        if(r.width()==1) horizontal=false;
        if(r.height()==1) horizontal=true;
        Rect a=r,b=r;
        if(horizontal) {a.r=(r.l+r.r)/2; b.l=a.r+1;}
        else {a.t=(r.b+r.t)/2; b.b=a.t+1;}
        double left=total(a),right=total(b);
        if(left<=0) { self(self,b,begin,end,!horizontal); return; }
        if(right<=0) { self(self,a,begin,end,!horizontal); return; }
        std::sort(ids.begin()+begin,ids.begin()+end,[&](int i,int j) {
            float x=horizontal?loc[i].X:loc[i].Y, y=horizontal?loc[j].X:loc[j].Y;
            return x!=y?x<y:i<j;
        });
        size_t cut=begin;
        double mass=0;
        while(cut<end) {
            // The grid books each member's integer charge before computing the quotas.
            double next=mass+static_cast<int>(charges[ids[cut]].occupation);
            if(next>left && std::abs(next-left)>=std::abs(mass-left)) break;
            ++cut; mass=next;
        }
        self(self,a,begin,cut,!horizontal);
        self(self,b,cut,end,!horizontal);
    };
    split(split,box,0,ids.size(),true);
}

void GeneralSpreader::finishShared(const std::map<int,PlacementInfo::Location>& before,
                                  int call, int inner)
{
    int width=int(binGrid.front().size()), count=int(preWriteOverflow.size());
    auto index=[width](die_spread::Bin *b){return b->Y()*width+b->X();};
    std::vector<int> owner(count,-1);
    std::vector<std::vector<int>> extra(expandedRegions.size());
    for(size_t k=0;k<expandedRegions.size();++k)
        for(auto b:expandedRegions[k]->getBinsInRegion()) owner[index(b)]=int(k);
    // A macro can touch more than one region, or end up outside its rectangle.
    // Record the union of its actual old/new resource bins for each affected
    // region. Overlapping footprints are audited per region; pass totals use
    // the entire grid once.
    std::vector<int> affectedRegions,affectedBins;
    for(const auto &entry:before) {
        auto pu=placementInfo->getPlacementUnits()[entry.first];
        if(pu->X()==entry.second.X && pu->Y()==entry.second.Y) continue;
        affectedBins.clear(); affectedRegions.clear();
        spreadMembers(pu,[&](DesignInfo::DesignCell *cell,float x,float y) {
            auto next=dieView->memberBin(cell->getCellId());
            if(!next) return;
            float oldX=entry.second.X+x, oldY=entry.second.Y+y;
            auto old=(oldX>=next->left() && oldX<next->right() && oldY>=next->bottom() && oldY<next->top())
                ? next : dieView->at(oldX,oldY);
            int oldId=index(old), nextId=index(next);
            if(owner[oldId]>=0) affectedRegions.push_back(owner[oldId]);
            if(oldId!=nextId) { affectedBins.push_back(oldId); affectedBins.push_back(nextId); }
        });
        if(affectedBins.empty()) continue;
        std::sort(affectedRegions.begin(),affectedRegions.end());
        affectedRegions.erase(std::unique(affectedRegions.begin(),affectedRegions.end()),affectedRegions.end());
        for(int k:affectedRegions) for(int i:affectedBins) if(owner[i]!=k) extra[k].push_back(i);
    }
    // Use a snapshot for every bin in the pass. Outcomes from one box may
    // neither promote nor erase the history of a neighbor in the same pass.
    auto nextHistory=residualHistory;
    auto binAt=[&](int id){return binGrid[id/width][id%width];};
    for(size_t k=0;k<expandedRegions.size();++k) {
        auto region=expandedRegions[k];
        auto &d=region->decision;
        if(!d.local) continue;
        auto &outside=extra[k];
        std::sort(outside.begin(),outside.end());
        outside.erase(std::unique(outside.begin(),outside.end()),outside.end());
        double initial=0,remaining=0;
        auto measure=[&](int i) { initial+=preWriteOverflow[i]; remaining+=spreadOverflow(binAt(i),capacityShrinkRatio); };
        for(auto b:region->getBinsInRegion()) measure(index(b));
        for(int i:outside) measure(i);
        auto record=[&](int i) {
            if(!binAt(i)->isSpreadingOverflow(capacityShrinkRatio)) { nextHistory[i]={}; return; }
            unsigned writes=0;
            auto inherit=[&](int j) {
                if(j>=0 && j<count && dieView->rowDie[j/width]==d.die && preWriteOverflow[j]>shared_residual::tolerance &&
                   (owner[j]==int(k) || std::binary_search(outside.begin(),outside.end(),j)))
                    writes=std::max(writes,residualHistory[j].writes+1);
            };
            // A surviving hotspot keeps its own history. Only new overflow
            // inherits a consecutive write from an adjacent prior hotspot.
            inherit(i);
            if(!writes) { if(i%width) inherit(i-1); if(i%width+1<width) inherit(i+1); inherit(i-width); inherit(i+width); }
            auto updated=shared_residual::afterWrite(writes,true,initial,remaining);
            if(updated.writes>=nextHistory[i].writes) {
                updated.hard |= nextHistory[i].hard;
                nextHistory[i]=updated;
            }
        };
        for(auto b:region->getBinsInRegion()) record(index(b));
        for(int i:outside) record(i);
        if(quotaLog.is_open() && !d.movableTarget.empty())
            quotaLog<<sharedCellType<<'\t'<<call<<'\t'<<inner<<'\t'<<d.seedX<<'\t'<<d.seedY<<'\t'
                    <<d.residualBins.size()<<'\t'<<d.plannedRelief<<'\t'<<initial<<'\t'<<remaining<<'\t'
                    <<region->getBinsInRegion().size()<<'\t'<<region->getBinsInRegion().size()+outside.size()<<'\n';
    }
    for(int i=0;i<count;++i)
        if(!binAt(i)->isSpreadingOverflow(capacityShrinkRatio)) nextHistory[i]={};
    residualHistory.swap(nextHistory);
}

void GeneralSpreader::spreadInBatches(int call, int limit, float forgetRatio, bool clockAware, float stepLimit)
{
    AMF_PROFILE_SCOPE("spreading", "batched_die_spread");
    using namespace batched_spread;
    using Unit = PlacementInfo::PlacementUnit;
    const int w=int(binGrid.front().size()), h=int(binGrid.size()), count=w*h;
    const auto &charges=placementInfo->getCellId2CellBinInfo();
    auto bin=[&](int i){return binGrid[i/w][i%w];};
    auto index=[&](die_spread::Bin *b){return b->Y()*w+b->X();};
    const auto &dies=dieView->rowDie;
    std::vector<Rect> domains;
    for(int y=0;y<h;) {
        int end=y;
        while(end+1<h && dies[end+1]==dies[y]) ++end;
        domains.push_back({0,w-1,y,end}); y=end+1;
    }
    Grid g{w,std::vector<batched_spread::Bin>(count)};
    struct Residents {
        int singles=0, fixed=0;
        std::vector<int> macros;
    };
    std::vector<Residents> residents(count);
    std::vector<int> revision(count), age(count), priorWrites(count);
    std::vector<char> dirty(count,true);
    using Key=std::tuple<int,int,int,int,int,int>;
    auto key=[](const Window &p){auto r=p.box;return Key{r.l,r.r,r.b,r.t,p.target,p.target<0?-1:p.origin};};
    std::map<Key,int> failed;
    auto stamp=[&](Rect r) {
        int value=0;
        each(r,[&](int x,int y){value=std::max(value,revision[g.index(x,y)]);});
        return value;
    };
    auto tried=[&](const Window &p){
        auto it=failed.find(key(p));return it!=failed.end() && it->second==stamp(p.box);
    };
    auto overflow=[&](die_spread::Bin *b){
        return std::max(0.,double(b->getUtilization())-b->spreadingDemandLimit(capacityShrinkRatio,true));
    };
    auto refresh=[&](int id) {
        auto &r=residents[id];r.singles=0;r.fixed=0;r.macros.clear();
        for(auto c:bin(id)->getCells()) {
            auto pu=placementInfo->getPlacementUnitByCell(c);
            if(pu->isFixed() || pu->isLocked()) r.fixed+=int(charges[c->getCellId()].occupation);
            else if(dynamic_cast<PlacementInfo::PlacementMacro*>(pu)) r.macros.push_back(pu->getId());
            else ++r.singles;
        }
        std::sort(r.macros.begin(),r.macros.end());
        r.macros.erase(std::unique(r.macros.begin(),r.macros.end()),r.macros.end());
    };
    const int maxBatches=10; // same budget as spreadPlacementUnits' original inner loop
    int executed=0;
    double remaining=0;
    std::string stop="iteration_budget";
    for(int inner=1;inner<=maxBatches;++inner) {
        if((inner-1)%5==0) for(auto &row:binGrid) for(auto b:row) {
            b->resetBinShrinkRatio(); b->resetNoOverflowCounter(); b->resetOverflowCounter();
        }
        findOverflowBins(capacityShrinkRatio);
        std::vector<int> hot;
        std::set<int> hotMacros;
        remaining=0;
        for(int i=0;i<count;++i) {
            auto b=bin(i);
            if(dirty[i]) {refresh(i);dirty[i]=false;}
            double demand=b->getUtilization(), cap=b->spreadingDemandLimit(capacityShrinkRatio,true);
            // Keep the established nominal trigger and effective receiver ceiling.
            double room=b->getNominalCapacity()>0 ? std::max(0.,std::min(cap,double(b->spreadingDemandLimit(capacityShrinkRatio)))-demand) : 0.;
            auto next=batched_spread::Bin{overflow(b),room,demand-residents[i].fixed,cap};
            auto &prev=g.bins[i];
            if(next.excess!=prev.excess || next.room!=prev.room || next.capacity!=prev.capacity) revision[i]=inner;
            prev=next;remaining+=next.excess;
            if(next.excess>eps) {
                hotMacros.insert(residents[i].macros.begin(),residents[i].macros.end());
                if(next.donor()>eps) hot.push_back(i);
            } else age[i]=0;
        }
        if(hot.empty()) {stop=remaining<=eps?"hotspots_cleared":"only_immovable_demand";break;}
        auto cold=[&](Rect r){
            int singles=0;std::set<int> macros;
            each(r,[&](int x,int y){
                int i=g.index(x,y);
                if(g.bins[i].excess>eps) return;
                singles+=residents[i].singles;
                for(int id:residents[i].macros) if(!hotMacros.count(id)) macros.insert(id);
            });
            return singles+int(macros.size());
        };
        auto regrow=[&](Rect r,int i){return grow(g,r,domains[dies[i/w]],limit,g.point(i),cold);};
        std::vector<Window> proposals(hot.size());
{ phase_wait::Parallel phaseParallel("GeneralSpreader.cc:1:schedule(dynamic,8) num_threads(nJobs) if(hot.size()>8)");
#pragma omp parallel num_threads(nJobs) if(hot.size()>8)
{auto &phaseWorker=phaseParallel.workers.at(omp_get_thread_num());phaseWorker.begin();
#pragma omp for schedule(dynamic,8) nowait
        for(int k=0;k<int(hot.size());++k) {
            int i=hot[k];proposals[k]={regrow(pointBox(g.point(i)),i),i};
        }
phaseWorker.wait();
#pragma omp barrier
phaseWorker.end();}
}

        auto windows=merge(std::move(proposals),w,h,limit,regrow);
        // Rank by the peak among all actionable hot bins in the final bounds.
        std::vector<Ranked> ranked;
        auto rank=[&](Window p) {
            if(tried(p)) return;
            double room=0,donor=0;
            int peak=-1;
            each(p.box,[&](int x,int y){
                int i=g.index(x,y);auto &b=g.bins[i];
                if(p.target<0 || dies[y]==p.target) room+=b.room;
                if(p.target>=0 && i!=p.origin) return;
                donor+=b.donor();
                if(b.donor()<=eps) return;
                auto priority=[&](int id){auto &a=g.bins[id];return std::make_tuple(a.urgency(),a.capacity<=0?a.donor():0.,age[id],-id);};
                if(peak<0 || priority(i)>priority(peak)) peak=i;
            });
            if(room<=eps || peak<0) return;
            auto &a=g.bins[peak];
            ranked.push_back({p,peak,age[p.origin],cold(p.box),a.urgency(),a.capacity<=0?a.donor():0.,
                              a.donor()*std::min(1.,room/donor)});
        };
        for(auto p:windows) rank(p);
        // An overlap never establishes local infeasibility. If ordinary planning
        // stalls, consider receivers hidden beyond two neutral strips or on a diagonal.
        bool recheck=ranked.empty();
        if(recheck) {
            std::vector<Window> escapes;
            for(int i:hot) {
                Point seed=g.point(i);Rect one=pointBox(seed),domain=domains[dies[seed.second]];
                auto nearest=[&](Rect dest,int target,Window &answer){
                    bool found=false;std::tuple<int,double,int,int> best;
                    nearby(seed,dest,limit,[&](int x,int y){
                        auto &b=g.at(x,y); if(b.room<=eps) return;
                        Window p{unite(one,pointBox({x,y})),i,target};
                        if(tried(p)) return;
                        auto cost=std::make_tuple(p.box.area(),-std::min(g.bins[i].donor(),b.room),y,x);
                        if(!found || cost<best){answer=p;best=cost;found=true;}
                    });
                    return found;
                };
                Window candidate{one,i};
                if(nearest(domain,-1,candidate)) {rank(candidate);continue;}
                bool localRoom=false;
                nearby(seed,domain,limit,[&](int x,int y){localRoom|=g.at(x,y).room>eps;});
                if(localRoom) {
                    // Minimum enclosing windows were all tried with these inputs.
                    // Larger local arrangements can still place an indivisible PU.
                    bool found=false;
                    for(int area=2;area<=limit && !found;++area) for(int ny=1;ny<=area && !found;++ny) {
                        if(area%ny) continue;
                        int nx=area/ny;
                        for(int y=std::max(domain.b,seed.second-ny+1);y<=std::min(seed.second,domain.t-ny+1) && !found;++y)
                            for(int x=std::max(0,seed.first-nx+1);x<=std::min(seed.first,w-nx) && !found;++x) {
                                Window p{{x,x+nx-1,y,y+ny-1},i};
                                auto size=ranked.size();rank(p);found=ranked.size()>size;
                            }
                    }
                    if(found) continue;
                }
                bool eligible=false;
                for(auto c:bin(i)->getCells()) {
                    auto pu=placementInfo->getPlacementUnitByCell(c);
                    if(pu->isFixed() || pu->isLocked()) continue;
                    bool local=true;
                    spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float dy){
                        local &= die_spread::dieAt(dieView->seams,pu->Y()+dy)==dies[i/w];
                    });
                    if(local){eligible=true;break;}
                }
                if(!eligible) continue;
                bool found=false;
                for(int d:{dies[i/w]-1,dies[i/w]+1}) if(d>=0 && d<int(domains.size())) {
                    Window other{one,i};
                    if(nearest(domains[d],d,other) && (!found || other.box.area()<candidate.box.area())) {
                        candidate=other;found=true;
                    }
                }
                if(found) escapes.push_back(candidate);
            }
            if(ranked.empty()) for(auto p:escapes) rank(p);
        }
        if(ranked.empty()) {stop="no_local_or_adjacent_action";break;}
        std::map<int,int> peakAge;
        for(const auto &r:ranked) peakAge[r.peak]=std::max(peakAge[r.peak],r.age);
        std::sort(ranked.begin(),ranked.end(),[&](const Ranked &a,const Ranked &b){
            if(a.urgency==b.urgency && a.zeroDebt==b.zeroDebt && a.peak!=b.peak && peakAge.at(a.peak)!=peakAge.at(b.peak))
                return peakAge.at(a.peak)>peakAge.at(b.peak);
            return earlier(a,b);
        });
        std::vector<char> occupied(count);
        std::set<int> claimedMacros;
        std::vector<Ranked> batch;
        for(auto &r:ranked) {
            bool conflict=false;
            each(r.window.box,[&](int x,int y){
                int i=g.index(x,y);if(occupied[i]) conflict=true;
                if(!conflict) for(int id:residents[i].macros) if(claimedMacros.count(id)){conflict=true;break;}
            });
            if(conflict) continue;
            std::set<int> outside, macros;
            each(r.window.box,[&](int x,int y){
                for(int id:residents[g.index(x,y)].macros) macros.insert(id);
            });
            for(int id:macros) {
                auto pu=placementInfo->getPlacementUnits()[id];
                spreadMembers(pu,[&](DesignInfo::DesignCell* c,float,float) {
                    auto member=dieView->memberBin(c->getCellId());
                    if(member) {
                        int i=index(member);
                        if(occupied[i]) conflict=true;
                        outside.insert(i);
                    }
                });
                if(conflict) break;
            }
            if(conflict) continue;
            for(int i:outside) occupied[i]=true;
            batch.push_back(r);
            each(r.window.box,[&](int x,int y){int i=g.index(x,y);occupied[i]=true;
                claimedMacros.insert(residents[i].macros.begin(),residents[i].macros.end());});
        }
        for(int i:hot) if(!occupied[i]) ++age[i]; else age[i]=0;
        coveredBinSet.clear();expandedRegions.clear();
        std::vector<std::unique_ptr<SpreadRegion>> regions;
        std::vector<int> inputRevision;
        std::set<Unit*> allUnits;
        std::vector<Unit*> units;
        std::set<DesignInfo::DesignCell*> cells;
        std::vector<int> unitOwner(placementInfo->getPlacementUnits().size(),-1);
        for(size_t k=0;k<batch.size();++k) {
            auto p=batch[k].window;Rect box=p.box;
            int first=p.target<0?batch[k].peak:p.origin;
            auto seed=bin(first);
            auto region=std::make_unique<SpreadRegion>(seed,placementInfo,binGrid,capacityShrinkRatio,limit,box.b,box.t);
            region->claimResidualBox(box,coveredBinSet);
            auto &d=region->decision;
            d.batchWindow=true;d.local=p.target<0;d.die=dies[first/w];d.targetDie=d.local?d.die:p.target;
            d.seedX=first%w;d.seedY=first/w;
            bool residual=false;
            std::vector<shared_residual::Bin> local;
            each(box,[&](int x,int y){
                int i=g.index(x,y);auto &b=g.bins[i];residual|=priorWrites[i] && b.donor()>eps;
                local.push_back({d.local || dies[y]==p.target?b.room:0., b.movable, true,0,
                                 d.local || i==first?b.excess:0.});
            });
            auto allocation=shared_residual::allocate(local,box.width());
            d.movableTarget=std::move(allocation.target);
            if(residual) d.residualBins.push_back(seed);
            if(!d.local) for(auto c:seed->getCells()) {
                auto pu=placementInfo->getPlacementUnitByCell(c);
                bool ok=!pu->isFixed() && !pu->isLocked();
                spreadMembers(pu,[&](DesignInfo::DesignCell*,float,float dy){
                    ok &= die_spread::dieAt(dieView->seams,pu->Y()+dy)==d.die;
                });
                if(ok) d.crossEligible.insert(pu->getId());
            }
            for(auto c:region->getCells()) {
                auto pu=placementInfo->getPlacementUnitByCell(c);
                if(pu->isFixed() || pu->isLocked()) continue;
                cells.insert(c);
                auto &owner=unitOwner.at(pu->getId());
                if(owner>=0 && owner!=int(k)) throw std::logic_error("Two batch windows own the same PU");
                if(owner<0) {owner=int(k);allUnits.insert(pu);units.push_back(pu);}
            }
            inputRevision.push_back(stamp(box));
            expandedRegions.push_back(region.get());regions.push_back(std::move(region));
            if(triggerStats) ++triggerCounts[seed->Y()][seed->X()].seeds;
        }
        std::map<int,PlacementInfo::Location> before;
        for(auto pu:units) before.emplace(pu->getId(),PlacementInfo::Location{pu->X(),pu->Y()});
{ phase_wait::Parallel phaseParallel("GeneralSpreader.cc:1:schedule(dynamic,1) num_threads(nJobs) if(regions.size()>1)");
#pragma omp parallel num_threads(nJobs) if(regions.size()>1)
{auto &phaseWorker=phaseParallel.workers.at(omp_get_thread_num());phaseWorker.begin();
#pragma omp for schedule(dynamic,1) nowait
        for(int k=0;k<int(regions.size());++k) partitionShared(*regions[k]);

phaseWorker.wait();
#pragma omp barrier
phaseWorker.end();}
}
        activateCrossDieLimits();
        // An escape batch is scheduled only after exhausting the local windows.
        // Apply every local proposal directly, subject to the existing motion guards.
        const float writeWeight = regions.front()->decision.local ? 1.0f : forgetRatio;
        updatePlacementUnitsWithSpreadedCellLocations(allUnits,cells,units,writeWeight,clockAware,stepLimit);
        constrainLocalWrites(units,before);
        constrainCrossDieWrites(units,before);
        if(!crossTargetDie.empty()) constrainLocalWrites(units,before);
        std::vector<std::set<int>> affected(batch.size());
        int moved=0,coldMoved=0,crossed=0;
        for(auto pu:units) {
            const auto old=before.at(pu->getId());
            if(old.X==pu->X() && old.Y==pu->Y()) continue;
            ++moved;bool wasHot=false;
            auto k=unitOwner[pu->getId()];
            spreadMembers(pu,[&](DesignInfo::DesignCell *c,float dx,float dy){
                int from=die_spread::dieAt(dieView->seams,old.Y+dy),to=die_spread::dieAt(dieView->seams,pu->Y()+dy);
                if(from!=to) {
                    if(regions[k]->decision.local || to!=regions[k]->decision.targetDie ||
                       !regions[k]->decision.crossEligible.count(pu->getId()))
                        throw std::logic_error("Unapproved die crossing in batch writeback");
                    if(!c->isVirtualCell()) ++crossed;
                }
                auto prev=dieView->memberBin(c->getCellId());
                if(!prev) return;
                int source=index(prev);wasHot |= g.bins[source].excess>eps;
                dieView->update(c,pu->X()+dx,pu->Y()+dy);
                int dest=index(dieView->memberBin(c->getCellId()));
                for(int i:{source,dest}) {affected[k].insert(i);revision[i]=inner+1;}
                if(dest!=source) dirty[dest]=dirty[source]=true;
            });
            if(!wasHot) ++coldMoved;
        }
        std::set<int> touched;
        for(size_t k=0;k<batch.size();++k) {
            auto &p=batch[k].window;
            double relief=0;
            for(int i:affected[k]) relief+=g.bins[i].excess-overflow(bin(i));
            if(relief<=eps) failed[key(p)]=inputRevision[k];
            each(p.box,[&](int x,int y){int i=g.index(x,y);if(g.bins[i].excess>eps) ++priorWrites[i];});
            touched.insert(affected[k].begin(),affected[k].end());
        }
        int longest=0, narrow=0;
        for(const auto &r:batch) {
            int wide=std::max(r.window.box.width(),r.window.box.height());
            int thin=std::min(r.window.box.width(),r.window.box.height());
            longest=std::max(longest,wide);narrow+=wide>=4*thin;
        }
        double actual=0;
        for(int i:touched) actual+=g.bins[i].excess-overflow(bin(i));
        std::cout<<"SPREAD_BATCH resource="<<sharedCellType<<" call="<<call<<" gp="<<currentIteration
                 <<" batch="<<inner<<" area_limit="<<limit<<" boxes="<<batch.size()
                 <<" longest_edge="<<longest<<" aspect_ge_4="<<narrow<<" recheck="<<recheck<<" examined_bins="<<touched.size()<<" moved_pus="<<moved
                 <<" previously_nonhot_pus="<<coldMoved<<" crossing_cells="<<crossed
                 <<" excess_before="<<remaining<<" net_relief="<<actual<<" excess_after="<<remaining-actual<<std::endl;
        remaining-=actual;++executed;expandedRegions.clear();dumpLUTFFCoordinate();
        if(remaining<=eps) {stop="hotspots_cleared";break;}
    }
    std::cout<<"SPREAD_BATCH_STOP resource="<<sharedCellType<<" call="<<call<<" gp="<<currentIteration
             <<" batches="<<executed<<" residual="<<remaining<<" reason="<<stop<<std::endl;
}
