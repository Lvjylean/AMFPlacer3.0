#include "../../../utils/RuntimeProfiler.h"
/**
 * @file MacroLegalizer.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief This implementation file contains APIs' implementation of the MacroLegalizer which maps DSP/BRAM/CARRY macros
 * to legal location
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "MacroLegalizer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <chrono>
#include "HardResourceUtils.h"
#include "ColumnOverflow.h"
#include "RegionColumnDP.h"

MacroLegalizer::MacroLegalizer(std::string legalizerName, PlacementInfo *placementInfo, DeviceInfo *deviceInfo,
                               std::vector<DesignInfo::DesignCellType> &macroTypesToLegalize,
                               std::map<std::string, std::string> &JSONCfg)
    : legalizerName(legalizerName), placementInfo(placementInfo), deviceInfo(deviceInfo),
      compatiblePlacementTable(placementInfo->getCompatiblePlacementTable()),
      macroTypesToLegalize(macroTypesToLegalize), cellLoc(placementInfo->getCellId2location()), JSONCfg(JSONCfg)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    macroCellsToLegalize.clear();
    PU2X.clear();
    PU2Y.clear();
    PU2LegalSites.clear();

    if (JSONCfg.find("y2xRatio") != JSONCfg.end())
    {
        y2xRatio = std::stof(JSONCfg["y2xRatio"]);
    }

    if (JSONCfg.find("MacroLegalizationVerbose") != JSONCfg.end())
    {
        verbose = JSONCfg["MacroLegalizationVerbose"] == "true";
    }

    if (JSONCfg.find("jobs") != JSONCfg.end())
    {
        nJobs = std::stoi(JSONCfg["jobs"]);
    }

    clockRegionAware = false;
    if (JSONCfg.count("MacroCandidateSelection"))
    {
        const auto &selection = JSONCfg.at("MacroCandidateSelection");
        if (selection != "legacy" && selection != "cached_topk")
            throw std::invalid_argument("MacroCandidateSelection must be legacy or cached_topk");
        cachedCandidateSelection = selection == "cached_topk";
    }
    verifyCandidateCosts = JSONCfg.count("VerifyMacroCandidateCosts") && JSONCfg.at("VerifyMacroCandidateCosts") == "true";
}

void MacroLegalizer::legalize(bool exactLegalization, bool directLegalization, bool _timingDrivenLegalize)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    if (verbose)
        print_status("MacroLegalizer[" + legalizerName + "] Started Legalization.");

    timingDrivenLegalize = _timingDrivenLegalize;
    resetSettings();
    findMacroType2AvailableSites();
    getMacrosToLegalize();
    noTarget = initialMacrosToLegalize.empty();

    if (initialMacrosToLegalize.size() == 0)
    {
        print_warning("MacroLegalizer[" + legalizerName + "] find no target elements.");
        noTarget = true;
        return;
    }

    verifyAvailableCapacity();

    if (!directLegalization)
    {
        roughlyLegalize();
        updatePUMatchingLocation(true, !exactLegalization);
    }

    // dumpMatching();

    if (exactLegalization)
    {
        if (verbose)
            print_status("MacroLegalizer[" + legalizerName + "] Started Fixed-Column Legalization.");
        resetSettings();
        fixedColumnLegalize(directLegalization);
        updatePUMatchingLocation(false, true);
        finalLegalizeBasedOnDP();
        dumpMatching(exactLegalization);
    }

    setSitesMapped();
    if (verbose)
        print_status("MacroLegalizer[" + legalizerName + "] Finished Legalization.");
}

void MacroLegalizer::roughlyLegalize()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    while (macroCellsToLegalize.size())
    {
        findMacroCell2SitesInDistance(clockRegionAware);
        findPossibleLegalLocation(false);
        resetMacroCell2SitesInDistance();

        createBipartiteGraph();
        const auto replayGraphStart=std::chrono::steady_clock::now();
        std::string replayGraph;
        if(const char *dir=std::getenv("AMF_HARD_REPLAY_DIR")) {
            static int sequence=0; const int id=++sequence;
            if(legalizerName.find("BRAM")!=std::string::npos) {
                replayGraph=std::string(dir)+"/hard-rough-"+std::to_string(id);
                AMF_PROFILE_SCOPE("diagnostics","hard_matching_snapshot");
                std::ofstream out(replayGraph+".input"),nodes(replayGraph+".nodes.tsv");
                if(!out || !nodes)throw std::runtime_error("Cannot create hard matching snapshot");
                out<<std::setprecision(std::numeric_limits<float>::max_digits10);
                out<<"AMF_MATCHING_V1 "<<adjList.size()<<' '<<rightSiteIds.size()<<' '<<macro2Sites.size()<<'\n';
                for(size_t left=0;left<adjList.size();++left) {
                    auto cell=macroCellsToLegalize[left];
                    nodes<<"L\t"<<left<<'\t'<<(cell->isDSP()?"DSP":cell->isBRAM()?"BRAM":cell->isURAM()?"URAM":"OTHER")<<'\t'<<cell->getName()<<'\n';
                    for(const auto &edge:adjList[left])out<<left<<' '<<edge.first<<' '<<edge.second<<'\n';
                }
                for(const auto &node:rightSiteIds)nodes<<"R\t"<<node.second<<'\t'<<node.first->getName()<<'\n';
            }
        }
        const double replayDumpSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-replayGraphStart).count();
        const auto replayMatchStart=std::chrono::steady_clock::now();
        minCostBipartiteMatcher = new MinCostBipartiteMatcher(macro2Sites.size(), rightSiteIds.size(),
                                                              macro2Sites.size(), adjList, nJobs, verbose,
                JSONCfg.count("BipartiteMatchingBackend") ? JSONCfg.at("BipartiteMatchingBackend") : "legacy",
                JSONCfg.count("BipartiteMatchingForwardBias") ? std::stod(JSONCfg.at("BipartiteMatchingForwardBias")) : 0.0);

        minCostBipartiteMatcher->solve();
        const double replayMatchSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-replayMatchStart).count();
        if(!replayGraph.empty()) {
            AMF_PROFILE_SCOPE("diagnostics","hard_matching_expected");
            std::ofstream out(replayGraph+".expected");
            for(size_t left=0;left<adjList.size();++left)out<<left<<'\t'<<minCostBipartiteMatcher->getMatchedRightNode(left)<<'\n';
            std::ofstream meta(replayGraph+".meta");
            meta<<std::setprecision(12)<<"jobs "<<nJobs<<"\nmatch_seconds "<<replayMatchSeconds<<"\ndump_seconds "<<replayDumpSeconds<<"\nlegalizer "<<legalizerName<<'\n';
        }
        updateMatchingAndUnmatchedMacroCells();

        displacementThreshold *= 2;
        if ((int)(maxNumCandidate * 2) > maxNumCandidate + 1)
            maxNumCandidate *= 2;
        else
            maxNumCandidate++;
        delete minCostBipartiteMatcher;
        minCostBipartiteMatcher = nullptr;
    }
}

void MacroLegalizer::fixedColumnLegalize(bool directLegalization)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    mapMacrosToColumns(directLegalization);
    resolveOverflowColumns();

    macroCellsToLegalize = initialMacrosToLegalize;

    while (macroCellsToLegalize.size())
    {
        findMacroCell2SitesInDistance(clockRegionAware);
        findPossibleLegalLocation(true);
        resetMacroCell2SitesInDistance();
        createBipartiteGraph();
        const auto replayGraphStart=std::chrono::steady_clock::now();
        std::string replayGraph;
        if(const char *dir=std::getenv("AMF_HARD_REPLAY_DIR")) {
            static int sequence=0; const int id=++sequence;
            if(legalizerName.find("BRAM")!=std::string::npos) {
                replayGraph=std::string(dir)+"/hard-column-"+std::to_string(id);
                AMF_PROFILE_SCOPE("diagnostics","hard_matching_snapshot");
                std::ofstream out(replayGraph+".input"),nodes(replayGraph+".nodes.tsv");
                if(!out || !nodes)throw std::runtime_error("Cannot create hard matching snapshot");
                out<<std::setprecision(std::numeric_limits<float>::max_digits10);
                out<<"AMF_MATCHING_V1 "<<adjList.size()<<' '<<rightSiteIds.size()<<' '<<macro2Sites.size()<<'\n';
                for(size_t left=0;left<adjList.size();++left) {
                    auto cell=macroCellsToLegalize[left];
                    nodes<<"L\t"<<left<<'\t'<<(cell->isDSP()?"DSP":cell->isBRAM()?"BRAM":cell->isURAM()?"URAM":"OTHER")<<'\t'<<cell->getName()<<'\n';
                    for(const auto &edge:adjList[left])out<<left<<' '<<edge.first<<' '<<edge.second<<'\n';
                }
                for(const auto &node:rightSiteIds)nodes<<"R\t"<<node.second<<'\t'<<node.first->getName()<<'\n';
            }
        }
        const double replayDumpSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-replayGraphStart).count();
        const auto replayMatchStart=std::chrono::steady_clock::now();
        minCostBipartiteMatcher = new MinCostBipartiteMatcher(macro2Sites.size(), rightSiteIds.size(),
                                                              macro2Sites.size(), adjList, nJobs, verbose,
                JSONCfg.count("BipartiteMatchingBackend") ? JSONCfg.at("BipartiteMatchingBackend") : "legacy",
                JSONCfg.count("BipartiteMatchingForwardBias") ? std::stod(JSONCfg.at("BipartiteMatchingForwardBias")) : 0.0);

        minCostBipartiteMatcher->solve();
        const double replayMatchSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-replayMatchStart).count();
        if(!replayGraph.empty()) {
            AMF_PROFILE_SCOPE("diagnostics","hard_matching_expected");
            std::ofstream out(replayGraph+".expected");
            for(size_t left=0;left<adjList.size();++left)out<<left<<'\t'<<minCostBipartiteMatcher->getMatchedRightNode(left)<<'\n';
            std::ofstream meta(replayGraph+".meta");
            meta<<std::setprecision(12)<<"jobs "<<nJobs<<"\nmatch_seconds "<<replayMatchSeconds<<"\ndump_seconds "<<replayDumpSeconds<<"\nlegalizer "<<legalizerName<<'\n';
        }
        updateMatchingAndUnmatchedMacroCells();

        displacementThreshold *= 2;
        maxNumCandidate *= 2;

        if ((int)(maxNumCandidate * 2) > maxNumCandidate + 1)
            maxNumCandidate *= 2;
        else
            maxNumCandidate++;

        delete minCostBipartiteMatcher;
        minCostBipartiteMatcher = nullptr;
    }
}

void MacroLegalizer::finalLegalizeBasedOnDP()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    PU2X.clear();
    PU2Y.clear();
    PU2LegalSites.clear();
    resetSettings();
    float tmpAverageDisplacement = 0.0;
    if (verbose)
        print_status("MacroLegalizer[" + legalizerName + "] Start finalLegalizeBasedOnDP");
    tmpAverageDisplacement += DPForMinHPWL(BRAMColumnNum, BRAMColumn2Sites, BRAMColumn2PUs);
    tmpAverageDisplacement += DPForMinHPWL(DSPColumnNum, DSPColumn2Sites, DSPColumn2PUs);
    tmpAverageDisplacement += DPForMinHPWL(URAMColumnNum, URAMColumn2Sites, URAMColumn2PUs);
    tmpAverageDisplacement += DPForMinHPWL(CARRYColumnNum, CARRYColumn2Sites, CARRYColumn2PUs);
    finalAverageDisplacement = tmpAverageDisplacement / PU2X.size();
    if (verbose)
        print_status("MacroLegalizer[" + legalizerName +
                     "] Done finalLegalizeBasedOnDP: Macro Cell Average Final Legalization Displacement = " +
                     std::to_string(finalAverageDisplacement));

    placementInfo->setPULegalSite(PU2LegalSites);
    placementInfo->setPULegalXY(PU2X, PU2Y);
}

float MacroLegalizer::DPForMinHPWL(int colNum, std::vector<std::vector<DeviceInfo::DeviceSite *>> &Column2Sites,
                                   std::vector<std::deque<PlacementInfo::PlacementUnit *>> &Column2PUs)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    // final Legalization DP
    // i th macro (start from 0), j th row (start from 0)
    // f[i][j] = min(f[i-1][j-row[i]]+HPWLChange[i][j-row[i]+1],f[i][j-1])

    // std::string dumpFile =
    //     JSONCfg["DumpMacroLegalization"] + "-Exact-" + std::to_string(DumpMacroLegalizationCnt) + ".gz";

    // print_status("MacroLegalizer: dumping MacroLegalization archieve to: " + dumpFile);
    // DumpMacroLegalizationCnt++;
    // std::stringstream outfile0;

    float tmpTotalDisplacement = 0.0;
    std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> PU2Sites;
    PU2Sites.clear();
    for (auto &PUDeque : Column2PUs)
    {
        for (auto tmpPU : PUDeque)
        {
            assert(PU2Sites.find(tmpPU) == PU2Sites.end());
            PU2Sites[tmpPU] = std::vector<DeviceInfo::DeviceSite *>();
        }
    }

#pragma omp parallel for
    for (int c = 0; c < colNum; c++)
    {
        int numPUs = Column2PUs[c].size();
        if (!numPUs)
            continue;

        auto &curColSites = Column2Sites[c];
        auto &curColPU = Column2PUs[c];

        sortSitesBySiteY(curColSites);

        if (placementInfo->hasHardRegions())
        {
            std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> regionSites;
            if (!solveRegionColumn(curColSites, curColPU, true, &regionSites))
            {
                print_error("Hard-region column cannot be legalized after column assignment: " + std::to_string(c));
                std::abort();
            }
            // PU2Sites keys were preallocated before the parallel loop. Each
            // column owns a disjoint set of PUs; do not insert into the shared map.
            for (auto pu : curColPU) PU2Sites.at(pu) = std::move(regionSites.at(pu));
            continue;
        }

        // initialize

        std::vector<std::vector<float>> f(numPUs, std::vector<float>(curColSites.size(), 1000000000.0));
        std::vector<std::vector<bool>> fChoice(
            numPUs, std::vector<bool>(curColSites.size(), 0)); // used for tracing back macro-site mapping

        int minLastPUTop = -1;

        int totalMacroCellNum = 0;
        int heightPURow = getMarcroCellNum(curColPU[0]);
        totalMacroCellNum += heightPURow;
        float minHPWLChange = 1100000000.0;
        for (unsigned int j = totalMacroCellNum - 1; j < curColSites.size(); j++)
        {
            float curHPWLChange = getHPWLChange(curColPU[0], curColSites[j - heightPURow + 1]);
            if (!legalSiteRange(curColPU[0], curColSites, j - heightPURow + 1, heightPURow))
            {
                // we need to ensure that there is no occpupied sites in this range
                curHPWLChange = 1100000000.0;
            }
            if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curColPU[0]))
            {
                if (curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                    curColSites[j]->getClockRegionY() != curColSites[j - heightPURow + 1]->getClockRegionY())
                {
                    // we need to ensure that there is no occpupied sites in this range
                    curHPWLChange = 1100000000.0;
                }
                if (curMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
                {
                    // we need to ensure BRAM macros starts from site with even siteY
                    if (curMacro->getCells().size() > 1 && curColSites[j - heightPURow + 1]->getSiteY() % 2 != 0)
                    {
                        curHPWLChange = 1100000000.0;
                    }
                }
            }
            if (curHPWLChange < minHPWLChange)
            {
                minHPWLChange = curHPWLChange;
                fChoice[0][j] = 1;
                if (numPUs == 1)
                {
                    minLastPUTop = j;
                }
            }
            f[0][j] = minHPWLChange;
        }

        // DP to minimize HPWL
        for (int i = 1; i < numPUs; i++)
        {
            float minVal = 100000000.0;
            int heightPURow = getMarcroCellNum(curColPU[i]);
            totalMacroCellNum += heightPURow;
            for (unsigned int j = totalMacroCellNum - 1; j < curColSites.size();
                 j++) // j start from heightPURow because PU0 must occupy 1+ site(s)
            {
                float curHPWLChange = getHPWLChange(curColPU[i], curColSites[j - heightPURow + 1]);
                if (!legalSiteRange(curColPU[i], curColSites, j - heightPURow + 1, heightPURow))
                {
                    // we need to ensure that there is no occpupied sites in this range
                    curHPWLChange = 1100000000.0;
                }
                if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curColPU[i]))
                {
                    if (curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                        curColSites[j]->getClockRegionY() != curColSites[j - heightPURow + 1]->getClockRegionY())
                    {
                        // we need to ensure that there is no occpupied sites in this range
                        curHPWLChange = 1100000000.0;
                    }
                    if (curMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
                    {
                        // we need to ensure BRAM macros starts from site with even siteY
                        if (curMacro->getCells().size() > 1 && curColSites[j - heightPURow + 1]->getSiteY() % 2 != 0)
                        {
                            curHPWLChange = 1100000000.0;
                        }
                    }
                }
                if (f[i][j - 1] > f[i - 1][j - heightPURow] + curHPWLChange)
                {
                    fChoice[i][j] = 1;
                    if (i == numPUs - 1)
                    {
                        if (f[i - 1][j - heightPURow] + curHPWLChange < minVal)
                        {
                            minVal = f[i - 1][j - heightPURow] + curHPWLChange;
                            minLastPUTop = j;
                        }
                    }
                }

                f[i][j] = std::min(f[i][j - 1], f[i - 1][j - heightPURow] + curHPWLChange);
            }
        }

        if (minLastPUTop < 0)
        {
            int PUCnt = 0;
            for (int i = 0; i < numPUs; i++)
            {
                std::cout << curColPU[i] << "\n";
                PUCnt += getMarcroCellNum(curColPU[i]);
            }
            std::cout << "total PU Cnt=" << PUCnt << "\n";
            for (int i = 0; i < numPUs; i++)
                for (int j = 0; j < curColSites.size(); j++)
                {
                    if (fChoice[i][j])
                    {
                        std::cout << "PU#" << i << " (" << getMarcroCellNum(curColPU[i]) << "): " << j << "\n";
                        break;
                    }
                }
            std::cout.flush();
        }

        assert(minLastPUTop >= 0);

        std::vector<DeviceInfo::DeviceSite *> PUBottom2Site_reverse(0);

        for (int i = numPUs - 1; i >= 0; i--)
        {
            assert(minLastPUTop >= 0);
            int heightPURow = getMarcroCellNum(curColPU[i]);
            while (!fChoice[i][minLastPUTop])
            {
                minLastPUTop--;
                assert(minLastPUTop >= 0);
            }
            auto curSite = curColSites[minLastPUTop - heightPURow + 1];
            assert(PU2Sites[curColPU[i]].size() == 0);
            assert(curSite);
            for (int curColSiteId = minLastPUTop - heightPURow + 1; curColSiteId <= minLastPUTop; curColSiteId++)
            {
                PU2Sites[curColPU[i]].push_back(curColSites[curColSiteId]);
            }
            minLastPUTop -= heightPURow;
            // outfile0 << "col#" << c << ": " << curColPU[i]->getName() << " PUY:" << PU2Y[curColPU[i]]
            //          << " numPUs:" << getMarcroCellNum(curColPU[i])
            //          << " netNum:" << curColPU[i]->getNetsSetPtr()->size() << "\n";
            // outfile0 << "    macthed with: " << curSite->getName() << " locX:" << curSite->X()
            //          << " locY:" << curSite->Y() << " HPWLIncrease:" << getHPWLChange(curColPU[i], curSite) << "\n";
        }
    }
    // writeStrToGZip(dumpFile, outfile0);
    // print_status("dumped MacroLegalization archieve to: " + dumpFile);
    for (auto &PUDeque : Column2PUs)
    {
        for (auto tmpPU : PUDeque)
        {
            assert(PU2Sites.find(tmpPU) != PU2Sites.end());
            auto &curSites = PU2Sites[tmpPU];
            assert(curSites.size());
            assert(PU2X.find(tmpPU) == PU2X.end());
            auto curSite = curSites[0];
            PU2LegalSites[tmpPU] = curSites;
            PU2X[tmpPU] = curSite->X();
            PU2Y[tmpPU] = curSite->Y();
            PULevelMatching.emplace_back(tmpPU, curSite);
            tmpTotalDisplacement += std::fabs(tmpPU->X() - curSite->X()) + std::fabs(tmpPU->Y() - curSite->Y());
        }
    }

    return tmpTotalDisplacement;
}

bool MacroLegalizer::solveRegionColumn(
    std::vector<DeviceInfo::DeviceSite *> sites,
    std::deque<PlacementInfo::PlacementUnit *> pus, bool optimize,
    std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> *result)
{
    if (pus.empty()) return true;
    sortSitesBySiteY(sites);
    // Fit probes sort a local copy. Final DP receives the already sorted column
    // from updatePUMatchingLocation; finalLegalizeBasedOnDP has cleared PU2Y by
    // then, so never read/insert into that map in the parallel final-DP phase.
    if (!optimize) sortPUsByPU2Y(pus);
    std::vector<int> itemRegions, siteRegions(sites.size(), -1);
    std::map<int, PlacementInfo::PlacementUnit *> representatives;
    for (auto pu : pus)
    {
        const int region = placementInfo->hardRegionForPU(pu);
        itemRegions.push_back(region);
        representatives.emplace(region, pu);
    }
    for (int j = 0; j < static_cast<int>(sites.size()); ++j)
        for (const auto &entry : representatives)
            if (placementInfo->hardRegionAllows(entry.second, sites[j]))
            {
                if (siteRegions[j] != -1) throw std::logic_error("Overlapping hard-region site masks");
                siteRegions[j] = entry.first;
            }
    std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> candidate;
    for (const auto &entry : AMFRegionColumnDP::partition(itemRegions, siteRegions))
    {
        const auto &group = entry.second;
        std::vector<DeviceInfo::DeviceSite *> regionSites;
        std::vector<int> heights, bottoms;
        for (int j : group.sites) regionSites.push_back(sites[j]);
        for (int i : group.items) heights.push_back(getMarcroCellNum(pus[i]));
        auto legal = [&](int i, int first, int height) {
            auto pu = pus[group.items[i]];
            if (!legalSiteRange(pu, regionSites, first, height)) return false;
            // Fixed/locked PUs are never displacement candidates in this path.
            if ((pu->isFixed() || pu->isLocked()) &&
                (std::fabs(regionSites[first]->X() - pu->X()) > 0.001f ||
                 std::fabs(regionSites[first]->Y() - pu->Y()) > 0.001f)) return false;
            return true;
        };
        auto cost = [&](int i, int first) -> double {
            return optimize ? getHPWLChange(pus[group.items[i]], regionSites[first]) : 0.0;
        };
        if (!AMFRegionColumnDP::solveOrdered(heights, regionSites.size(), legal, cost, bottoms)) return false;
        if (result)
            for (int i = 0; i < static_cast<int>(group.items.size()); ++i)
            {
                const int first = bottoms[i];
                candidate.emplace(pus[group.items[i]], std::vector<DeviceInfo::DeviceSite *>(
                    regionSites.begin() + first, regionSites.begin() + first + heights[i]));
            }
    }
    if (result) *result = std::move(candidate);
    return true;
}

bool MacroLegalizer::macroCanBeFitIn(int colId, std::vector<std::vector<DeviceInfo::DeviceSite *>> &Column2Sites,
                                     std::deque<PlacementInfo::PlacementUnit *> Column2PUs)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    int c = colId;

    int numPUs = Column2PUs.size();
    if (!numPUs)
        return true;

    if (placementInfo->hasHardRegions())
        return solveRegionColumn(Column2Sites[c], Column2PUs, false, nullptr);

    auto &curColSites = Column2Sites[c];
    auto &curColPU = Column2PUs;

    sortSitesBySiteY(curColSites);
    sortPUsByPU2Y(curColPU);

    // initialize

    std::vector<std::vector<float>> f(numPUs, std::vector<float>(curColSites.size(), 1000000000.0));
    std::vector<std::vector<bool>> fChoice(
        numPUs, std::vector<bool>(curColSites.size(), 0)); // used for tracing back macro-site mapping

    int minLastPUTop = -1;

    int totalMacroCellNum = 0;
    int heightPURow = getMarcroCellNum(curColPU[0]);
    totalMacroCellNum += heightPURow;
    float minHPWLChange = 1100000000.0;
    for (unsigned int j = totalMacroCellNum - 1; j < curColSites.size(); j++)
    {
        float curHPWLChange = 1;
        if (!legalSiteRange(curColPU[0], curColSites, j - heightPURow + 1, heightPURow))
        {
            // we need to ensure that there is no occpupied sites in this range
            curHPWLChange = 1100000000.0;
        }
        if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curColPU[0]))
        {
            if (curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                curColSites[j]->getClockRegionY() != curColSites[j - heightPURow + 1]->getClockRegionY())
            {
                // we need to ensure that there is no occpupied sites in this range
                curHPWLChange = 1100000000.0;
            }
            if (curMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
            {
                // we need to ensure BRAM macros starts from site with even siteY
                if (curMacro->getCells().size() > 1 && curColSites[j - heightPURow + 1]->getSiteY() % 2 != 0)
                {
                    curHPWLChange = 1100000000.0;
                }
            }
        }
        if (curHPWLChange < minHPWLChange)
        {
            minHPWLChange = curHPWLChange;
            fChoice[0][j] = 1;
            if (numPUs == 1)
            {
                minLastPUTop = j;
            }
        }
        f[0][j] = minHPWLChange;
    }

    // DP to minimize HPWL
    for (int i = 1; i < numPUs; i++)
    {
        float minVal = 100000000.0;
        int heightPURow = getMarcroCellNum(curColPU[i]);
        totalMacroCellNum += heightPURow;
        for (unsigned int j = totalMacroCellNum - 1; j < curColSites.size();
             j++) // j start from heightPURow because PU0 must occupy 1+ site(s)
        {
            float curHPWLChange = 1;
            if (!legalSiteRange(curColPU[i], curColSites, j - heightPURow + 1, heightPURow))
            {
                // we need to ensure that there is no occpupied sites in this range
                curHPWLChange = 1100000000.0;
            }
            if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curColPU[i]))
            {
                if (curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                    curColSites[j]->getClockRegionY() != curColSites[j - heightPURow + 1]->getClockRegionY())
                {
                    // we need to ensure that there is no occpupied sites in this range
                    curHPWLChange = 1100000000.0;
                }
                if (curMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
                {
                    // we need to ensure BRAM macros starts from site with even siteY
                    if (curMacro->getCells().size() > 1 && curColSites[j - heightPURow + 1]->getSiteY() % 2 != 0)
                    {
                        curHPWLChange = 1100000000.0;
                    }
                }
            }
            if (f[i][j - 1] > f[i - 1][j - heightPURow] + curHPWLChange)
            {
                fChoice[i][j] = 1;
                if (i == numPUs - 1)
                {
                    if (f[i - 1][j - heightPURow] + curHPWLChange < minVal)
                    {
                        minVal = f[i - 1][j - heightPURow] + curHPWLChange;
                        minLastPUTop = j;
                    }
                }
            }

            f[i][j] = std::min(f[i][j - 1], f[i - 1][j - heightPURow] + curHPWLChange);
        }
    }

    if (minLastPUTop < 0)
    {
        int PUCnt = 0;
        for (int i = 0; i < numPUs; i++)
        {
            std::cout << curColPU[i] << "\n";
            PUCnt += getMarcroCellNum(curColPU[i]);
        }
        std::cout << "total PU Cnt=" << PUCnt << "\n";
        for (int i = 0; i < numPUs; i++)
            for (int j = 0; j < curColSites.size(); j++)
            {
                if (fChoice[i][j])
                {
                    std::cout << "PU#" << i << " (" << getMarcroCellNum(curColPU[i]) << "): " << j << "\n";
                    break;
                }
            }
        std::cout.flush();
    }

    return (minLastPUTop >= 0);
}

void MacroLegalizer::getMacrosToLegalize()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    macroCellsToLegalize.clear();
    macroUnitsToLegalizeSet.clear();
    BRAMPUs.clear();
    DSPPUs.clear();
    URAMPUs.clear();
    CARRYPUs.clear();
    for (auto curCell : placementInfo->getCells())
    {
        for (auto curType : macroTypesToLegalize)
        {
            if (curCell->getCellType() == curType)
            {
                auto curPU = placementInfo->getPlacementUnitByCell(curCell);
                if (!curPU->isLocked())
                {
                    macroCellsToLegalize.push_back(curCell);
                    macroUnitsToLegalizeSet.insert(curPU);
                    if (curCell->isBRAM())
                    {
                        BRAMPUs.insert(curPU);
                    }
                    if (curCell->isDSP())
                    {
                        DSPPUs.insert(curPU);
                    }
                    if (curCell->isURAM())
                    {
                        URAMPUs.insert(curPU);
                    }
                    if (curCell->isCarry())
                    {
                        CARRYPUs.insert(curPU);
                    }
                }
            }
        }
    }
    initialMacrosToLegalize = macroCellsToLegalize;
}

void MacroLegalizer::findMacroType2AvailableSites()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    macroType2Sites.clear();
    for (auto curCellType : macroTypesToLegalize)
    {
        macroType2Sites[curCellType] = std::vector<DeviceInfo::DeviceSite *>(0);
        for (int SharedBELID : placementInfo->getPotentialBELTypeIDs(curCellType))
        {
            std::string targetSiteType =
                compatiblePlacementTable
                    ->sharedCellType2SiteType[compatiblePlacementTable->sharedCellBELTypes[SharedBELID]];
            std::vector<DeviceInfo::DeviceSite *> &sitesInType = deviceInfo->getSitesInType(targetSiteType);
            for (auto curSite : sitesInType)
            {
                if (!curSite->isOccupied() && !curSite->isMapped())
                {
                    // if (matchedSites.find(curSite) == matchedSites.end())
                    // {
                    macroType2Sites[curCellType].push_back(curSite);
                    // }
                }
            }
        }
    }

    for (auto curCellType : macroTypesToLegalize)
    {
        if (DesignInfo::isBRAM(curCellType))
        {
            enableBRAMLegalization = true;
            for (auto curSite : macroType2Sites[curCellType])
            {
                if (curSite->getSiteY() + 1 > BRAMRowNum)
                    BRAMRowNum = curSite->getSiteY() + 1;
                if (curSite->getSiteX() + 1 > BRAMColumnNum)
                    BRAMColumnNum = curSite->getSiteX() + 1;
            }
        }
        if (DesignInfo::isDSP(curCellType))
        {
            enableDSPLegalization = true;
            for (auto curSite : macroType2Sites[curCellType])
            {
                if (curSite->getSiteY() + 1 > DSPRowNum)
                    DSPRowNum = curSite->getSiteY() + 1;
                if (curSite->getSiteX() + 1 > DSPColumnNum)
                    DSPColumnNum = curSite->getSiteX() + 1;
            }
        }
        if (DesignInfo::isURAM(curCellType))
        {
            enableURAMLegalization = true;
            for (auto curSite : macroType2Sites[curCellType])
            {
                if (curSite->getSiteY() + 1 > URAMRowNum)
                    URAMRowNum = curSite->getSiteY() + 1;
                if (curSite->getSiteX() + 1 > URAMColumnNum)
                    URAMColumnNum = curSite->getSiteX() + 1;
            }
        }
        if (DesignInfo::isCarry(curCellType))
        {
            enableCARRYLegalization = true;
            for (auto curSite : macroType2Sites[curCellType])
            {
                if (curSite->getSiteY() + 1 > CARRYRowNum)
                    CARRYRowNum = curSite->getSiteY() + 1;
                if (curSite->getSiteX() + 1 > CARRYColumnNum)
                    CARRYColumnNum = curSite->getSiteX() + 1;
            }
        }
    }

    BRAMColumnXs.clear();
    if (enableBRAMLegalization)
    {
        BRAMColumnXs.resize(BRAMColumnNum, -1.0);
        BRAMColumn2Sites.clear();
        BRAMColumn2Sites.resize(BRAMColumnNum, std::vector<DeviceInfo::DeviceSite *>(0));
        for (auto curCellType : macroTypesToLegalize)
        {
            if (DesignInfo::isBRAM(curCellType))
            {
                for (auto curSite : macroType2Sites[curCellType])
                {
                    if (curSite->getSiteY() + 1 > BRAMRowNum)
                        BRAMRowNum = curSite->getSiteY() + 1;
                    if (curSite->getSiteX() + 1 > BRAMColumnNum)
                        BRAMColumnNum = curSite->getSiteX() + 1;
                    BRAMColumnXs[curSite->getSiteX()] = curSite->X();
                    BRAMColumn2Sites[curSite->getSiteX()].push_back(curSite);
                }
                break;
            }
        }
    }

    DSPColumnXs.clear();
    if (enableDSPLegalization)
    {
        DSPColumnXs.resize(DSPColumnNum, -1.0);
        DSPColumn2Sites.clear();
        DSPColumn2Sites.resize(DSPColumnNum, std::vector<DeviceInfo::DeviceSite *>(0));
        for (auto curCellType : macroTypesToLegalize)
        {
            if (DesignInfo::isDSP(curCellType))
            {
                for (auto curSite : macroType2Sites[curCellType])
                {
                    if (curSite->getSiteY() + 1 > DSPRowNum)
                        DSPRowNum = curSite->getSiteY() + 1;
                    if (curSite->getSiteX() + 1 > DSPColumnNum)
                        DSPColumnNum = curSite->getSiteX() + 1;
                    DSPColumnXs[curSite->getSiteX()] = curSite->X();
                    DSPColumn2Sites[curSite->getSiteX()].push_back(curSite);
                }
                break;
            }
        }
    }
    URAMColumnXs.clear();
    if (enableURAMLegalization)
    {
        URAMColumnXs.resize(URAMColumnNum, -1.0);
        URAMColumn2Sites.clear();
        URAMColumn2Sites.resize(URAMColumnNum, std::vector<DeviceInfo::DeviceSite *>(0));
        for (auto curCellType : macroTypesToLegalize)
        {
            if (DesignInfo::isURAM(curCellType))
            {
                for (auto curSite : macroType2Sites[curCellType])
                {
                    if (curSite->getSiteY() + 1 > URAMRowNum)
                        URAMRowNum = curSite->getSiteY() + 1;
                    if (curSite->getSiteX() + 1 > URAMColumnNum)
                        URAMColumnNum = curSite->getSiteX() + 1;
                    URAMColumnXs[curSite->getSiteX()] = curSite->X();
                    URAMColumn2Sites[curSite->getSiteX()].push_back(curSite);
                }
                break;
            }
        }
    }

    CARRYColumnXs.clear();
    if (enableCARRYLegalization)
    {
        CARRYColumnXs.resize(CARRYColumnNum, -1.0);
        CARRYColumn2Sites.clear();
        CARRYColumn2Sites.resize(CARRYColumnNum, std::vector<DeviceInfo::DeviceSite *>(0));
        for (auto curCellType : macroTypesToLegalize)
        {
            if (DesignInfo::isCarry(curCellType))
            {
                for (auto curSite : macroType2Sites[curCellType])
                {
                    if (curSite->getSiteY() + 1 > CARRYRowNum)
                        CARRYRowNum = curSite->getSiteY() + 1;
                    if (curSite->getSiteX() + 1 > CARRYColumnNum)
                        CARRYColumnNum = curSite->getSiteX() + 1;
                    CARRYColumnXs[curSite->getSiteX()] = curSite->X();
                    CARRYColumn2Sites[curSite->getSiteX()].push_back(curSite);
                }
                break;
            }
        }
    }
}

void MacroLegalizer::findPossibleLegalLocation(bool fixedColumn)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    macro2Sites.clear();

    for (auto curCell : macroCellsToLegalize)
    {
        macro2Sites[curCell] = std::vector<DeviceInfo::DeviceSite *>(0);
    }

    int numMacroCells = macroCellsToLegalize.size();
    selectedCandidateCosts.clear();
    selectedCandidateCosts.resize(numMacroCells);

    if (verbose)
    {
        print_status("searching sites for " + std::to_string(numMacroCells) + " macroCells with displacement<" +
                     std::to_string(displacementThreshold));
    }
#pragma omp parallel for
    for (int i = 0; i < numMacroCells; i++)
    {
        auto curCell = macroCellsToLegalize[i];
        auto curCellType = curCell->getCellType();
        int macroLength = -1;
        int cellOffset = -1;

        if (DesignInfo::isDSP(curCellType) || DesignInfo::isBRAM(curCellType))
        {
            auto tmpPU = placementInfo->getPlacementUnitByCellId(curCell->getCellId());
            if (dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
            {
                macroLength = cellOffset = 1;
            }
            else if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
            {

                for (cellOffset = 0; cellOffset < (int)(tmpMacro->getCells().size()); cellOffset++)
                {
                    if (tmpMacro->getCell(cellOffset) == curCell)
                    {
                        macroLength = tmpMacro->getCells().size();
                        break;
                    }
                }
            }
        }

        assert(macroType2Sites.find(curCellType) != macroType2Sites.end());

        std::vector<DeviceInfo::DeviceSite *> *candidateSite = nullptr;
        if (fixedColumn)
        {
            int targetSiteX = -1;
            if (curCell->isDSP())
            {
                assert(DSPCell2Column.find(curCell) != DSPCell2Column.end());
                targetSiteX = DSPCell2Column[curCell];
            }
            if (curCell->isURAM())
            {
                assert(URAMCell2Column.find(curCell) != URAMCell2Column.end());
                targetSiteX = URAMCell2Column[curCell];
            }
            if (curCell->isCarry())
            {
                assert(CARRYCell2Column.find(curCell) != CARRYCell2Column.end());
                targetSiteX = CARRYCell2Column[curCell];
            }
            if (curCell->isBRAM())
            {
                assert(BRAMCell2Column.find(curCell) != BRAMCell2Column.end());
                targetSiteX = BRAMCell2Column[curCell];
            }
            assert(targetSiteX >= 0);

            std::vector<std::vector<DeviceInfo::DeviceSite *>> *column2Sites;
            if (DesignInfo::isCarry(curCellType))
            {
                column2Sites = &CARRYColumn2Sites;
            }
            else if (DesignInfo::isDSP(curCellType))
            {
                column2Sites = &DSPColumn2Sites;
            }
            else if (DesignInfo::isURAM(curCellType))
            {
                column2Sites = &URAMColumn2Sites;
            }
            else if (DesignInfo::isBRAM(curCellType))
            {
                column2Sites = &BRAMColumn2Sites;
            }
            else
            {
                assert(false && "undefine type");
            }
            for (auto &tmpCol : *column2Sites)
            {
                if (tmpCol.size() == 0)
                    continue;
                if (tmpCol[0]->getSiteX() == targetSiteX)
                {
                    candidateSite = &tmpCol;
                }
            }
        }
        else
        {
            candidateSite = macro2SitesInDisplacementThreshold[curCell];
        }

        assert(candidateSite);

        for (auto curSite : *candidateSite)
        {
            if(!placementInfo->hardRegionAllows(curCell,curSite))continue;
            float curDisp = getDisplacement(cellLoc[curCell->getCellId()], curSite);
            if ((!fixedColumn && curDisp < displacementThreshold) ||
                (fixedColumn && curDisp < 2 * displacementThreshold))
            {
                // since the cell in a macro, we need to ensure the other parts of the macro can be legalized if the
                // cell is placed to the site
                if (placementInfo->isLegalLocation(curCell, curSite->X(), curSite->Y()) ||
                    displacementThreshold > 2 * initialDisplacementThreshold)
                {
                    if (matchedSites.find(curSite) != matchedSites.end())
                        continue;

                    if ((curCellType == DesignInfo::CellType_RAMB36E2 ||
                         curCellType == DesignInfo::CellType_FIFO36E2) &&
                        curSite->getSiteY() % 2 != 0)
                        continue;
                    if ((curCellType == DesignInfo::CellType_RAMB18E2 ||
                         curCellType == DesignInfo::CellType_FIFO18E2) &&
                        curCell->isVirtualCell() && curSite->getSiteY() % 2 != 1)
                        continue;

                    if (macroLength > 1 && cellOffset >= 0 && clockRegionCasLegalization && fixedColumn)
                    {
                        int headLocation = curSite->getSiteY() - cellOffset;
                        int tailLocation = headLocation + macroLength - 1;
                        if (headLocation / clockRegionHeightOfDSE_BRAM != tailLocation / clockRegionHeightOfDSE_BRAM)
                        {
                            continue;
                        }
                    }

                    macro2Sites[curCell].push_back(curSite);
                }
            }
        }
        auto &sites = macro2Sites[curCell];
        if (cachedCandidateSelection)
        {
            std::vector<std::pair<float, std::size_t>> ranked;
            ranked.reserve(sites.size());
            for (std::size_t candidate = 0; candidate < sites.size(); ++candidate)
                ranked.emplace_back(getHPWLChange(curCell, sites[candidate]), candidate);
            const std::size_t keep = std::min(sites.size(), static_cast<std::size_t>(maxNumCandidate));
            // Equal costs retain candidate-search order. The old quicksort had
            // a different tie order, so this mode is validated as a QoR change.
            std::partial_sort(ranked.begin(), ranked.begin() + keep, ranked.end());
            std::vector<DeviceInfo::DeviceSite *> selected;
            selected.reserve(keep);
            auto &costs = selectedCandidateCosts[i];
            costs.reserve(keep);
            for (std::size_t candidate = 0; candidate < keep; ++candidate)
            {
                selected.push_back(sites[ranked[candidate].second]);
                costs.push_back(ranked[candidate].first);
            }
            sites.swap(selected);
        }
        else
        {
            if (sites.size() > 1)
                quick_sort_WLChange(curCell, sites, 0, sites.size() - 1, cellLoc[curCell->getCellId()]);
            if (sites.size() > static_cast<unsigned int>(maxNumCandidate)) sites.resize(maxNumCandidate);
        }
    }

    // print_info("#total macro cell = " + std::to_string(macro2Sites.size()));
    // print_info("#total macro candidate site (might be duplicated) = " + std::to_string(totalSiteNum));
}

void MacroLegalizer::createBipartiteGraph()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    rightSiteIds.clear();
    adjList.resize(macroCellsToLegalize.size());
    siteList.clear();

    float minCost = 10000000;

    for (unsigned int leftCellId = 0; leftCellId < macroCellsToLegalize.size(); leftCellId++)
    {
        auto curCell = macroCellsToLegalize[leftCellId];
        adjList[leftCellId].clear();
        const auto &candidates = macro2Sites[curCell];
        for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate)
        {
            auto curSite = candidates[candidate];
            if (rightSiteIds.find(curSite) == rightSiteIds.end())
            {
                int curSiteCnt = rightSiteIds.size();
                rightSiteIds[curSite] = curSiteCnt;
                siteList.push_back(curSite);
            }
            float tmpCost = cachedCandidateSelection ? selectedCandidateCosts.at(leftCellId).at(candidate)
                                                     : getHPWLChange(curCell, curSite);
            if (cachedCandidateSelection && verifyCandidateCosts && tmpCost != getHPWLChange(curCell, curSite))
                throw std::logic_error("Macro candidate cost changed between selection and graph construction");
            adjList[leftCellId].emplace_back(rightSiteIds[curSite], tmpCost);
            if (tmpCost < minCost)
            {
                minCost = tmpCost;
            }
            // adjList[leftCellId].emplace_back(rightSiteIds[curSite],
            //                                  getDisplacement(cellLoc[curCell->getCellId()], curSite));
        }
    }

    if (minCost < 0.0001)
    {
        float compensation = 10 - minCost;
        for (unsigned int leftCellId = 0; leftCellId < macroCellsToLegalize.size(); leftCellId++)
        {
            for (unsigned int tmpId = 0; tmpId < adjList[leftCellId].size(); tmpId++)
            {
                adjList[leftCellId][tmpId].second += compensation;
            }
        }
    }
}

void MacroLegalizer::updateMatchingAndUnmatchedMacroCells()
{
    AMF_PROFILE_FUNCTION("macro_legalization");

    for (unsigned int leftCellId = 0; leftCellId < macroCellsToLegalize.size(); leftCellId++)
    {
        int rightNode = minCostBipartiteMatcher->getMatchedRightNode(leftCellId);
        auto curCell = macroCellsToLegalize[leftCellId];
        if (rightNode >= 0)
        {
            assert(matchedMacroCells.find(curCell) == matchedMacroCells.end());
            assert(matchedSites.find(siteList[rightNode]) == matchedSites.end());
            matchedMacroCells.insert(curCell);
            matchedSites.insert(siteList[rightNode]);
            cellLevelMatching.emplace_back(curCell, siteList[rightNode]);
        }
    }
    std::vector<DesignInfo::DesignCell *> newMacrosToLegalize;
    newMacrosToLegalize.clear();
    for (unsigned int leftCellId = 0; leftCellId < macroCellsToLegalize.size(); leftCellId++)
    {
        auto curCell = macroCellsToLegalize[leftCellId];
        if (matchedMacroCells.find(curCell) == matchedMacroCells.end())
        {
            newMacrosToLegalize.push_back(curCell);
        }
    }
    macroCellsToLegalize = newMacrosToLegalize;
}

void MacroLegalizer::dumpMatching(bool fixedColumn, bool enforce)
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    if (noTarget)
        return;
    if (JSONCfg.find("DumpMacroLegalization") != JSONCfg.end() || enforce)
    {
        std::string dumpFile = "";
        if (enforce)
            dumpFile = JSONCfg["dumpDirectory"] + legalizerName + "DumpMacroLegalization-" +
                       std::to_string(DumpMacroLegalizationCnt) + ".gz";
        else
            dumpFile = JSONCfg["DumpMacroLegalization"] + "-" + legalizerName +
                       std::to_string(DumpMacroLegalizationCnt) + ".gz";

        print_status("MacroLegalizer: dumping MacroLegalization archieve to: " + dumpFile);
        DumpMacroLegalizationCnt++;
        if (dumpFile != "")
        {
            std::stringstream outfile0;
            if (cellLevelMatching.size() != 0)
            {
                for (auto matchedPair : cellLevelMatching)
                {
                    auto matchedSite = matchedPair.second;
                    auto curCell = matchedPair.first;
                    auto curPU = placementInfo->getPlacementUnitByCell(curCell);
                    outfile0 << "CellLevelMatching name: " << curCell->getName() << " CellId: " << curCell->getCellId()
                             << " PUID: " << curPU->getId() << "\n        locX:" << cellLoc[curCell->getCellId()].X
                             << "\n        locY:" << cellLoc[curCell->getCellId()].Y << "\n";
                    outfile0 << "    macthed with: " << matchedSite->getName() << "\n        locX:" << matchedSite->X()
                             << "\n        locY:" << matchedSite->Y()
                             << "\n        dis:" << getDisplacement(cellLoc[curCell->getCellId()], matchedSite)
                             << "\n        HPWLIncrease:" << getHPWLChange(curCell, matchedSite) << "\n";
                }
            }

            if (PULevelMatching.size() != 0)
            {
                for (auto matchedPair : PULevelMatching)
                {
                    auto matchedSite = matchedPair.second;
                    auto curPU = matchedPair.first;
                    outfile0 << "PULevelMatching name: " << curPU->getName() << " PUID: " << curPU->getId()
                             << "\n        locX:" << curPU->X() << "\n        locY:" << curPU->Y()
                             << "\n        PU2X:" << PU2X[curPU] << "\n        PU2Y:" << PU2Y[curPU] << "\n";
                    assert(std::fabs(matchedSite->X() - PU2X[curPU]) + std::fabs(matchedSite->Y() - PU2Y[curPU]) < 0.1);
                    assert(std::fabs(matchedSite->X() - placementInfo->getPULegalXY().first[curPU]) +
                               std::fabs(matchedSite->Y() - placementInfo->getPULegalXY().second[curPU]) <
                           0.1);
                    outfile0 << "    macthed with: " << matchedSite->getName() << "\n        locX:" << matchedSite->X()
                             << "\n        locY:" << matchedSite->Y()
                             << "\n        dis:" << getDisplacement(curPU, matchedSite)
                             << "\n        HPWLIncrease:" << getHPWLChange(curPU, matchedSite) << "\n";
                }
            }

            if (fixedColumn)
            {
                for (int i = 0; i < BRAMColumnNum; i++)
                {
                    int numPUs = BRAMColumn2PUs[i].size();
                    for (int j = 0; j < numPUs; j++)
                    {
                        auto PU = BRAMColumn2PUs[i][j];
                        outfile0 << "BRAM col#" << i << ": " << PU->getName() << " PUY:" << PU2Y[PU]
                                 << " numPUs:" << getMarcroCellNum(PU) << " netNum:" << PU->getNetsSetPtr()->size()
                                 << "\n";
                    }
                }
                for (int i = 0; i < DSPColumnNum; i++)
                {
                    int numPUs = DSPColumn2PUs[i].size();
                    for (int j = 0; j < numPUs; j++)
                    {
                        auto PU = DSPColumn2PUs[i][j];
                        outfile0 << "DSP col#" << i << ": " << PU->getName() << " PUY:" << PU2Y[PU]
                                 << " numPUs:" << getMarcroCellNum(PU) << " netNum:" << PU->getNetsSetPtr()->size()
                                 << "\n";
                    }
                }
                for (int i = 0; i < URAMColumnNum; i++)
                {
                    int numPUs = URAMColumn2PUs[i].size();
                    for (int j = 0; j < numPUs; j++)
                    {
                        auto PU = URAMColumn2PUs[i][j];
                        outfile0 << "URAM col#" << i << ": " << PU->getName() << " PUY:" << PU2Y[PU]
                                 << " numPUs:" << getMarcroCellNum(PU) << " netNum:" << PU->getNetsSetPtr()->size()
                                 << "\n";
                    }
                }
                for (int i = 0; i < CARRYColumnNum; i++)
                {
                    int numPUs = CARRYColumn2PUs[i].size();
                    for (int j = 0; j < numPUs; j++)
                    {
                        auto PU = CARRYColumn2PUs[i][j];
                        outfile0 << "CARRY col#" << i << ": " << PU->getName() << " PUY:" << PU2Y[PU]
                                 << " numPUs:" << getMarcroCellNum(PU) << " netNum:" << PU->getNetsSetPtr()->size()
                                 << "\n";
                    }
                }
            }

            writeStrToGZip(dumpFile, outfile0);
            print_status("MacroLegalizer: dumped MacroLegalization archieve to: " + dumpFile);
        }
    }
}

int MacroLegalizer::getMarcroCellNum(PlacementInfo::PlacementUnit *tmpMacroUnit)
{
    if (dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpMacroUnit))
    {
        return 1;
    }
    else if (auto macroPU = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpMacroUnit))
    {
        if (macroPU->checkHasBRAM())
            return macroPU->getBRAMNum();
        else if (macroPU->checkHasDSP())
            return macroPU->getDSPNum();
        else if (macroPU->checkHasURAM())
            return macroPU->getURAMNum();
        else if (macroPU->checkHasCARRY())
            return macroPU->getCARRYNum();
        else
        {
            assert(false && "undefined legaliation macro");
            return 1;
        }
    }
    assert(false);
    return 1;
}

void MacroLegalizer::sortPUsByPU2Y(std::deque<PlacementInfo::PlacementUnit *> &PUs)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    int numPUs = PUs.size();
    for (int i = 0; i < numPUs; i++)
        for (int j = i + 1; j < numPUs; j++)
            if (PU2Y[PUs[i]] > PU2Y[PUs[j]])
                swapPU(&PUs[i], &PUs[j]);
}

void MacroLegalizer::sortSitesBySiteY(std::vector<DeviceInfo::DeviceSite *> &sites)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    int numSites = sites.size();
    bool ordered = true;
    for (int i = 1; i < numSites; i++)
    {
        if (sites[i - 1]->getSiteY() > sites[i]->getSiteY())
        {
            ordered = false;
            break;
        }
    }
    if (ordered)
        return;
    for (int i = 0; i < numSites; i++)
        for (int j = i + 1; j < numSites; j++)
            if (sites[i]->getSiteY() > sites[j]->getSiteY())
                swapSitePtr(&sites[i], &sites[j]);
}

void MacroLegalizer::updatePUMatchingLocation(bool isRoughLegalization, bool updateDisplacement)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    PU2X.clear();
    PU2Y.clear();
    PU2Columns.clear();
    PU2SiteX.clear();
    PU2LegalSites.clear();

    BRAMColumn2PUs.clear();
    if (BRAMColumnNum > 0)
    {
        BRAMColumn2PUs.resize(BRAMColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
    }
    DSPColumn2PUs.clear();
    if (DSPColumnNum > 0)
    {
        DSPColumn2PUs.resize(DSPColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
    }
    URAMColumn2PUs.clear();
    if (URAMColumnNum > 0)
    {
        URAMColumn2PUs.resize(URAMColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
    }
    CARRYColumn2PUs.clear();
    if (CARRYColumnNum > 0)
    {
        CARRYColumn2PUs.resize(CARRYColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
    }

    float tmpAverageDisplacement = 0.0;

    for (auto matchedPair : cellLevelMatching)
    {
        auto matchedSite = matchedPair.second;
        auto curCell = matchedPair.first;

        auto curPU = placementInfo->getPlacementUnitByCell(curCell);

        if (PU2Y.find(curPU) == PU2Y.end())
        {
            PU2SiteX[curPU] = -1;
            PU2Y[curPU] = 0.0;
            if (curCell->isBRAM())
            {
                BRAMColumn2PUs[matchedSite->getSiteX()].push_back(curPU);
            }
            if (curCell->isDSP())
            {
                DSPColumn2PUs[matchedSite->getSiteX()].push_back(curPU);
            }
            if (curCell->isURAM())
            {
                URAMColumn2PUs[matchedSite->getSiteX()].push_back(curPU);
            }
            if (curCell->isCarry())
            {
                CARRYColumn2PUs[matchedSite->getSiteX()].push_back(curPU);
            }
        }

        float actualPUX = 0;
        float actualPUY = 0;
        // since the matched site is for a single DSP/BRAM/CARRY cell, we need to find its corresponding placement macro
        // (with multiple DSPs/BRAMs/CARRYs)
        placementInfo->getPULocationByCellLocation(curCell, matchedSite->X(), matchedSite->Y(), actualPUX, actualPUY);

        tmpAverageDisplacement += std::fabs(cellLoc[curCell->getCellId()].X - matchedSite->X()) +
                                  std::fabs(cellLoc[curCell->getCellId()].Y - matchedSite->Y());
        assert(isRoughLegalization || PU2SiteX[curPU] == -1 || PU2SiteX[curPU] == matchedSite->getSiteX());
        PU2SiteX[curPU] = matchedSite->getSiteX();
        PU2X[curPU] += actualPUX;
        PU2Y[curPU] += actualPUY;
        if (PU2Columns.find(curPU) == PU2Columns.end())
        {
            PU2Columns[curPU] = std::vector<int>(0);
        }
        PU2Columns[curPU].push_back(matchedSite->getSiteX());
    }

    tmpAverageDisplacement /= cellLevelMatching.size();

    if (verbose)
        print_info("MacroLegalizer[" + legalizerName +
                   "] Macro Cell Average Displacement = " + std::to_string(tmpAverageDisplacement));

    if (updateDisplacement)
    {
        if (isRoughLegalization)
            roughAverageDisplacement = tmpAverageDisplacement;
        else
            fixedColumnAverageDisplacement = tmpAverageDisplacement;
    }

    for (auto pairPU2X : PU2X)
    {
        int numCells = getMarcroCellNum(pairPU2X.first);
        PU2X[pairPU2X.first] /= numCells;
        PU2Y[pairPU2X.first] /= numCells;
    }

#pragma omp parallel for
    for (int i = 0; i < BRAMColumnNum; i++)
        sortPUsByPU2Y(BRAMColumn2PUs[i]);

#pragma omp parallel for
    for (int i = 0; i < DSPColumnNum; i++)
        sortPUsByPU2Y(DSPColumn2PUs[i]);

#pragma omp parallel for
    for (int i = 0; i < URAMColumnNum; i++)
        sortPUsByPU2Y(URAMColumn2PUs[i]);

#pragma omp parallel for
    for (int i = 0; i < CARRYColumnNum; i++)
        sortPUsByPU2Y(CARRYColumn2PUs[i]);

    placementInfo->setPULegalXY(PU2X, PU2Y);
}

bool MacroLegalizer::canReceiveWholeMacro(
    int target, PlacementInfo::PlacementUnit *pu, const std::vector<int> &utilization,
    const std::vector<float> &budgets, std::vector<std::vector<DeviceInfo::DeviceSite *>> &sites,
    const std::vector<std::deque<PlacementInfo::PlacementUnit *>> &columns)
{
    if (target < 0 || target >= static_cast<int>(columns.size()) || pu->isFixed() || pu->isLocked()) return false;
    if (!AMFColumnOverflow::wholeMacroFitsBudget(utilization[target], getMarcroCellNum(pu),
                                               sites[target].size(), budgets[target])) return false;
    auto proposed = columns[target];
    proposed.push_back(pu);
    return solveRegionColumn(sites[target], proposed, false, nullptr);
}

void MacroLegalizer::spreadMacros(int columnNum, std::vector<int> &columnUntilization,
                                  std::vector<std::vector<DeviceInfo::DeviceSite *>> &column2Sites,
                                  std::vector<std::deque<PlacementInfo::PlacementUnit *>> &column2PUs,
                                  std::map<DesignInfo::DesignCell *, int> &cell2Column, float globalBudgeRatio)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    if (columnNum == 0) return;
    std::vector<float> budgetRatios(columnNum, globalBudgeRatio);
    while (true)
    {
        int overflowColId = -1;
        std::vector<int> accumulationUtil(columnNum, 0), accumulationCapacity(columnNum, 0);
        accumulationUtil[0] = columnUntilization[0];
        accumulationCapacity[0] = column2Sites[0].size() * budgetRatios[0];
        for (int colId = 1; colId < columnNum; colId++)
        {
            accumulationUtil[colId] = accumulationUtil[colId - 1] + columnUntilization[colId];
            accumulationCapacity[colId] =
                accumulationCapacity[colId - 1] + column2Sites[colId].size() * budgetRatios[colId];
        }

        for (int colId = 0; colId < columnNum; colId++)
        {
            if (budgetRatios[colId] == 1)
            {
                if (column2Sites[colId].size() < (unsigned int)columnUntilization[colId])
                {
                    overflowColId = colId;
                    break;
                }
            }
            else
            {
                if (column2Sites[colId].size() * budgetRatios[colId] < (unsigned int)columnUntilization[colId])
                {
                    overflowColId = colId;
                    break;
                }
            }

            if (!macroCanBeFitIn(colId, column2Sites, column2PUs[colId]))
            {
                assert(budgetRatios[colId] - 0.05 > 0);
                budgetRatios[colId] -= 0.05;
                overflowColId = colId;
                break;
            }
        }
        if (overflowColId < 0)
        {
            break;
        }
        else
        {
            print_warning("column overflow resolving col:" + std::to_string(overflowColId));
            for (int colId = 0; colId < columnNum; colId++)
            {
                std::cout << " colId#" << colId << " columnUntilization:" << columnUntilization[colId] << " "
                          << " siteCap:" << column2Sites[colId].size()
                          << " actualSiteCap:" << column2Sites[colId].size() * budgetRatios[colId] << "\n";
            }
            int leftAvaliableCapacity = 0;
            int rightAvaliableCapacity = 0;
            int leftUtil = 0;
            int rightUtil = 0;

            if (overflowColId > 0)
            {
                leftAvaliableCapacity += accumulationCapacity[overflowColId - 1];
                leftUtil += accumulationUtil[overflowColId - 1];
            }
            if (overflowColId < columnNum - 1)
            {
                rightAvaliableCapacity += accumulationCapacity[columnNum - 1] - accumulationCapacity[overflowColId];
                rightUtil += accumulationUtil[columnNum - 1] - accumulationUtil[overflowColId];
            }

            int overflowNum = AMFColumnOverflow::requiredRelief(columnUntilization[overflowColId],
                column2Sites[overflowColId].size(), budgetRatios[overflowColId]);
            int toLeft = 0;

            std::cout << " overflowNum=" << overflowNum << "\n";

            int totalAvailableCapacity = rightAvaliableCapacity + leftAvaliableCapacity - leftUtil - rightUtil;
            assert(totalAvailableCapacity > 0);
            float toLeftRatio = std::max(0.0f, std::min(1.0f,
                (float)(leftAvaliableCapacity - leftUtil) / totalAvailableCapacity));
            if (placementInfo->hasHardRegions())
            {
                int movedRows = 0;
                const int leftGoal = AMFColumnOverflow::directionalReliefCeil(overflowNum, toLeftRatio);
                auto &source = column2PUs[overflowColId];
                while (movedRows < overflowNum && !source.empty())
                {
                    const bool preferLeft = toLeft < leftGoal;
                    bool moved = false;
                    // A whole macro is transferred directly, not relayed through
                    // intermediate columns. Distance has priority; the aggregate
                    // side share breaks left/right ties only.
                    for (int distance = 1; distance < columnNum && !moved; ++distance)
                    {
                        const int directions[2] = {preferLeft ? -1 : 1, preferLeft ? 1 : -1};
                        for (int direction : directions)
                        {
                            const int target = overflowColId + direction * distance;
                            if (target < 0 || target >= columnNum) continue;
                            for (int k = 0; k < static_cast<int>(source.size()); ++k)
                            {
                                const int index = direction < 0 ? k : static_cast<int>(source.size()) - 1 - k;
                                auto pu = source[index];
                                if (!canReceiveWholeMacro(target, pu, columnUntilization, budgetRatios,
                                                          column2Sites, column2PUs)) continue;
                                const int height = getMarcroCellNum(pu);
                                // Commit only after the full destination probe succeeds.
                                column2PUs[target].push_back(pu);
                                source.erase(source.begin() + index);
                                columnUntilization[target] += height;
                                columnUntilization[overflowColId] -= height;
                                movedRows += height;
                                if (direction < 0) toLeft += height;
                                std::cout << "AMF_REGION_MACRO_MOVE pu=" << pu->getId()
                                          << " rows=" << height << " from=" << overflowColId
                                          << " to=" << target << " destination_fit=1\n";
                                moved = true;
                                break;
                            }
                            if (moved) break;
                        }
                    }
                    if (!moved)
                        throw std::runtime_error("No column can legally receive an unfixed whole macro from column " +
                                                 std::to_string(overflowColId));
                }
                if (movedRows == 0)
                    throw std::runtime_error("Hard-region column overflow made no progress: " +
                                             std::to_string(overflowColId));
                continue;
            }
            int toLeftNum = int((overflowNum * toLeftRatio) + 0.4999);

            if (leftAvaliableCapacity - leftUtil > 0)
            {
                while (toLeftNum > 0 && column2PUs[overflowColId].size() > 0)
                {
                    column2PUs[overflowColId - 1].push_back(column2PUs[overflowColId][0]);
                    int macroSize = getMarcroCellNum(column2PUs[overflowColId][0]);
                    columnUntilization[overflowColId - 1] += macroSize;
                    columnUntilization[overflowColId] -= macroSize;
                    toLeftNum -= macroSize;
                    toLeft += macroSize;
                    column2PUs[overflowColId].pop_front();
                }
            }
            // A whole macro can exceed the requested left relief. Count what
            // actually moved before deciding whether the right side still needs any.
            int toRightNum = std::max(0, overflowNum - toLeft);
            if (rightAvaliableCapacity - rightUtil > 0)
            {
                while (toRightNum > 0 && column2PUs[overflowColId].size() > 0)
                {
                    column2PUs[overflowColId + 1].push_front(
                        column2PUs[overflowColId][column2PUs[overflowColId].size() - 1]);
                    int macroSize = getMarcroCellNum(column2PUs[overflowColId][column2PUs[overflowColId].size() - 1]);
                    columnUntilization[overflowColId + 1] += macroSize;
                    columnUntilization[overflowColId] -= macroSize;
                    toRightNum -= macroSize;
                    column2PUs[overflowColId].pop_back();
                }
            }

        }
    }
    cell2Column.clear();
    for (unsigned int colId = 0; colId < column2PUs.size(); colId++)
    {
        auto PUs = column2PUs[colId];
        for (auto curPU : PUs)
        {
            if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
            {
                auto curCell = unpackedCell->getCell();
                cell2Column[curCell] = colId;
            }
            else if (auto macroPU = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
            {
                for (auto curCell : macroPU->getCells())
                {
                    cell2Column[curCell] = colId;
                }
            }
        }
    }
}

void MacroLegalizer::resolveOverflowColumns()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    if (enableBRAMLegalization)
    {
        spreadMacros(BRAMColumnNum, BRAMColumnUntilization, BRAMColumn2Sites, BRAMColumn2PUs, BRAMCell2Column, 0.9);
    }
    if (enableDSPLegalization)
    {
        spreadMacros(DSPColumnNum, DSPColumnUntilization, DSPColumn2Sites, DSPColumn2PUs, DSPCell2Column);
    }
    if (enableURAMLegalization)
    {
        spreadMacros(URAMColumnNum, URAMColumnUntilization, URAMColumn2Sites, URAMColumn2PUs, URAMCell2Column);
    }
    if (enableCARRYLegalization)
    {
        spreadMacros(CARRYColumnNum, CARRYColumnUntilization, CARRYColumn2Sites, CARRYColumn2PUs, CARRYCell2Column,
                     0.9);
    }
}

int MacroLegalizer::findIdMaxWithRecurence(int minId, int maxId, std::vector<int> &ids)
{
    int resId = -1;
    int maxRecurence = 0;
    assert(ids.size() > 0);
    for (int i = minId; i <= maxId; i++)
    {
        int cnt = 0;
        for (auto id : ids)
        {
            if (id == i)
                cnt++;
        }
        if (cnt > maxRecurence)
        {
            resId = i;
            maxRecurence = cnt;
        }
    }
    assert(resId >= 0);
    return resId;
}

int MacroLegalizer::findCorrespondingColumn(float curX, std::vector<float> &Xs)
{
    int resId = -1;
    float closeDiff = 100000000;

    for (unsigned int i = 0; i < Xs.size(); i++)
    {
        if (std::fabs(curX - Xs[i]) < closeDiff)
        {
            closeDiff = std::fabs(curX - Xs[i]);
            resId = i;
        }
    }

    assert(resId >= 0);
    return resId;
}

void MacroLegalizer::mapMacrosToColumns(bool directLegalization)
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    BRAMColumn2PUs.clear();
    BRAMColumnUntilization.clear();
    if (BRAMColumnNum > 0)
    {
        BRAMColumn2PUs.resize(BRAMColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
        BRAMColumnUntilization.resize(BRAMColumnNum, 0);

        if (directLegalization)
        {
            for (auto &tmpMacroUnit : BRAMPUs)
            {
                if (BRAMPUs.find(tmpMacroUnit) != BRAMPUs.end())
                {
                    int colId = findCorrespondingColumn(tmpMacroUnit->X(), BRAMColumnXs);
                    BRAMColumn2PUs[colId].push_back(tmpMacroUnit);
                    BRAMColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
        else
        {
            for (auto &PUCol_pair : PU2Columns)
            {
                auto tmpMacroUnit = PUCol_pair.first;
                if (BRAMPUs.find(tmpMacroUnit) != BRAMPUs.end())
                {
                    int colId = findIdMaxWithRecurence(0, BRAMColumnNum - 1, PUCol_pair.second);

                    BRAMColumn2PUs[colId].push_back(tmpMacroUnit);
                    BRAMColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
    }

    DSPColumn2PUs.clear();
    DSPColumnUntilization.clear();
    if (DSPColumnNum > 0)
    {
        DSPColumn2PUs.resize(DSPColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
        DSPColumnUntilization.resize(DSPColumnNum, 0);

        if (directLegalization)
        {
            for (auto tmpMacroUnit : DSPPUs)
            {
                if (DSPPUs.find(tmpMacroUnit) != DSPPUs.end())
                {
                    int colId = findCorrespondingColumn(tmpMacroUnit->X(), DSPColumnXs);

                    DSPColumn2PUs[colId].push_back(tmpMacroUnit);
                    DSPColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
        else
        {
            for (auto &PUCol_pair : PU2Columns)
            {
                auto tmpMacroUnit = PUCol_pair.first;
                if (DSPPUs.find(tmpMacroUnit) != DSPPUs.end())
                {
                    int colId = findIdMaxWithRecurence(0, DSPColumnNum - 1, PUCol_pair.second);

                    DSPColumn2PUs[colId].push_back(tmpMacroUnit);
                    DSPColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
    }
    URAMColumn2PUs.clear();
    URAMColumnUntilization.clear();
    if (URAMColumnNum > 0)
    {
        URAMColumn2PUs.resize(URAMColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
        URAMColumnUntilization.resize(URAMColumnNum, 0);

        if (directLegalization)
        {
            for (auto tmpMacroUnit : URAMPUs)
            {
                if (URAMPUs.find(tmpMacroUnit) != URAMPUs.end())
                {
                    int colId = findCorrespondingColumn(tmpMacroUnit->X(), URAMColumnXs);

                    URAMColumn2PUs[colId].push_back(tmpMacroUnit);
                    URAMColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
        else
        {
            for (auto &PUCol_pair : PU2Columns)
            {
                auto tmpMacroUnit = PUCol_pair.first;
                if (URAMPUs.find(tmpMacroUnit) != URAMPUs.end())
                {
                    int colId = findIdMaxWithRecurence(0, URAMColumnNum - 1, PUCol_pair.second);

                    URAMColumn2PUs[colId].push_back(tmpMacroUnit);
                    URAMColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
    }

    CARRYColumn2PUs.clear();
    CARRYColumnUntilization.clear();
    if (CARRYColumnNum > 0)
    {
        CARRYColumn2PUs.resize(CARRYColumnNum, std::deque<PlacementInfo::PlacementUnit *>(0));
        CARRYColumnUntilization.resize(CARRYColumnNum, 0);
        if (directLegalization)
        {
            for (auto tmpMacroUnit : CARRYPUs)
            {
                if (CARRYPUs.find(tmpMacroUnit) != CARRYPUs.end())
                {
                    int colId = findCorrespondingColumn(tmpMacroUnit->X(), CARRYColumnXs);

                    CARRYColumn2PUs[colId].push_back(tmpMacroUnit);
                    CARRYColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
        else
        {
            for (auto &PUCol_pair : PU2Columns)
            {
                auto tmpMacroUnit = PUCol_pair.first;
                if (CARRYPUs.find(tmpMacroUnit) != CARRYPUs.end())
                {
                    int colId = findIdMaxWithRecurence(0, CARRYColumnNum - 1, PUCol_pair.second);

                    CARRYColumn2PUs[colId].push_back(tmpMacroUnit);
                    CARRYColumnUntilization[colId] += getMarcroCellNum(tmpMacroUnit);
                }
            }
        }
    }
}

void MacroLegalizer::setSitesMapped()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    if (PULevelMatching.size() == 0)
    {
        for (auto matchedPair : cellLevelMatching)
        {
            auto matchedSite = matchedPair.second;
            assert(!matchedSite->isMapped());
            matchedSite->setMapped();
        }
    }
    else
    {
        for (auto matchedPair : PULevelMatching)
        {
            auto targetPU = matchedPair.first;
            for (auto curSite : PU2LegalSites[targetPU])
            {
                assert(!curSite->isMapped());
                curSite->setMapped();
            }
        }
    }
}

void MacroLegalizer::resetSitesMapped()
{
    AMF_PROFILE_FUNCTION("macro_legalization");
    if (PULevelMatching.size() == 0)
    {
        for (auto matchedPair : cellLevelMatching)
        {
            auto matchedSite = matchedPair.second;
            assert(matchedSite->isMapped());
            matchedSite->resetMapped();
        }
    }
    else
    {
        for (auto matchedPair : PULevelMatching)
        {
            auto targetPU = matchedPair.first;
            for (auto curSite : PU2LegalSites[targetPU])
            {
                assert(curSite->isMapped());
                curSite->resetMapped();
            }
        }
        PULevelMatching.clear();
    }
}

bool MacroLegalizer::legalSiteRange(PlacementInfo::PlacementUnit *pu,
    const std::vector<DeviceInfo::DeviceSite *> &sites, int first, int count) const
{
    if (!HardResourceUtils::contiguousWithinSLR(sites, first, count)) return false;
    for(int i=first;i<first+count;++i)if(!placementInfo->hardRegionAllows(pu,sites[i]))return false;
    if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
    {
        if (macro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
            sites[first]->getClockRegionY() != sites[first+count-1]->getClockRegionY()) return false;
        if (macro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM &&
            macro->getCells().size() > 1 && sites[first]->getSiteY() % 2) return false;
    }
    return true;
}

void MacroLegalizer::verifyAvailableCapacity()
{
    AMF_PROFILE_FUNCTION("validation");
    auto verify = [this](const std::set<PlacementInfo::PlacementUnit *> &units,
                         std::vector<std::vector<DeviceInfo::DeviceSite *>> &columns) {
        size_t capacity = 0, demand = 0;
        for (auto &column : columns) { sortSitesBySiteY(column); capacity += column.size(); }
        std::set<std::tuple<int, int, int>> checked;
        for (auto pu : units)
        {
            int count = getMarcroCellNum(pu);
            demand += count;
            auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu);
            int kind = macro ? static_cast<int>(macro->getMacroType()) : -1;
            if (!checked.insert({kind, count, placementInfo->hardRegionForPU(pu)}).second) continue;
            bool fits = false;
            for (const auto &column : columns)
            {
                for (int i = 0; i + count <= static_cast<int>(column.size()); ++i)
                    if (legalSiteRange(pu, column, i, count)) { fits = true; break; }
                if (fits) break;
            }
            if (!fits) throw std::runtime_error("No contiguous same-SLR resource range for: " + pu->getName());
        }
        if (demand > capacity) throw std::runtime_error("Hard-resource demand exceeds available site capacity");
    };
    verify(URAMPUs, URAMColumn2Sites);
    verify(DSPPUs, DSPColumn2Sites);
    verify(BRAMPUs, BRAMColumn2Sites);
    verify(CARRYPUs, CARRYColumn2Sites);
}

void MacroLegalizer::moveAMacroFromAToB(PlacementInfo::PlacementMacro *curMacro, int X1, int Y1, int X2, int Y2,
                                        std::vector<std::vector<CLBMacroLegalizattionInfo>> &CLBArray)
{

    int offset = 0;

    for (auto curSite : PU2LegalSites[curMacro])
    {
        assert(curSite->isMapped());
        CLBArray[curSite->getSiteY()][curSite->getSiteX()].macro = curMacro;
        CLBArray[curSite->getSiteY()][curSite->getSiteX()].offsetInMacro = offset;
        offset++;
    }
}

void MacroLegalizer::macroDetailedPlacement(PlacementTimingOptimizer *timingOptimizer)
{
    placementInfo->updateElementBinGrid();
    timingOptimizer->conductStaticTimingAnalysis();

    auto &deviceCLBArray = deviceInfo->getCLBSliceArray();
    if (deviceCLBArray.empty()) return;
    int colNum = deviceCLBArray[0].size();
    int rowNum = deviceCLBArray.size();

    std::vector<std::vector<CLBMacroLegalizattionInfo>> CLBArray(rowNum,
                                                                 std::vector<CLBMacroLegalizattionInfo>(colNum));

    for (auto matchedPair : PULevelMatching)
    {
        auto targetPU = matchedPair.first;
        int offset = 0;
        for (auto curSite : PU2LegalSites[targetPU])
        {
            assert(curSite->isMapped());
            auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(targetPU);
            CLBArray[curSite->getSiteY()][curSite->getSiteX()].macro = curMacro;
            CLBArray[curSite->getSiteY()][curSite->getSiteX()].offsetInMacro = offset;
            offset++;
        }
    }

    auto oriCellIdsInCriticalPaths = timingOptimizer->findCriticalPaths(0.9, true, 1000);
    std::set<PlacementInfo::PlacementMacro *> macrosTouched;

    int fineTunedCnt = 0;

    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        PlacementInfo::PlacementUnit *lastPU = nullptr;
        std::set<PlacementInfo::PlacementUnit *> PUsInCriticalPathSet;
        std::set<PlacementInfo::PlacementMacro *> carrayMacros;

        for (auto cellId : oriCellIdsInCriticalPath)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            if (PUsInCriticalPathSet.find(curPU) == PUsInCriticalPathSet.end())
            {
                PUsInCriticalPathSet.insert(curPU);
                if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                {
                    if (curMacro->checkHasCARRY())
                    {
                        carrayMacros.insert(curMacro);
                    }
                }
            }
        }

        if (!carrayMacros.size())
            continue;

        for (auto curMacro : carrayMacros)
        {
            if (curMacro->isFixed() || curMacro->isLocked() || !PU2LegalSites.count(curMacro) ||
                PU2LegalSites.at(curMacro).empty() || macrosTouched.find(curMacro) != macrosTouched.end())
                continue;
            macrosTouched.insert(curMacro);
            // if (curMacro->hasRegister())
            //     continue;

            // std::cout << " processing " << curMacro << "\n";

            float oriPUX = curMacro->X();
            float oriPUY = curMacro->Y();

            curMacro->setAnchorLocationAndForgetTheOriginalOne(PU2LegalSites[curMacro][0]->X(),
                                                               PU2LegalSites[curMacro][0]->Y());
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

            std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> changedPUs;
            changedPUs.insert(curMacro);
            timingOptimizer->incrementalStaticTimingAnalysis(changedPUs, true);
            float oriMaxFanoutDelay = timingOptimizer->getMaxIncrementalDelay();
            timingOptimizer->recoverArrivalDelayFromLastIncrementalSTA();
            changedPUs.clear();

            int curSiteX = PU2LegalSites[curMacro][0]->getSiteX();
            int curSiteY = PU2LegalSites[curMacro][0]->getSiteY();
            int siteNum = PU2LegalSites[curMacro].size();
            // std::cout << " curSiteX " << curSiteX << " curSiteY " << curSiteY << " siteNum " << siteNum
            //           << " oriMaxFanoutDelay " << oriMaxFanoutDelay << "\n";

            int bestOffsetSiteX = -100, bestOffsetSiteY = -100;
            float bestDelay = oriMaxFanoutDelay - 0.01;

            for (int offsetSiteX = -1; offsetSiteX <= 1; offsetSiteX++)
            {
                for (int offsetSiteY = -1; offsetSiteY <= 1; offsetSiteY++)
                {
                    if (offsetSiteX == 0 && offsetSiteY == 0)
                        continue;
                    if (curSiteY + offsetSiteY < 0 || curSiteY + offsetSiteY + siteNum > int(CLBArray.size()))
                        continue;
                    if (curSiteX + offsetSiteX < 0 || curSiteX + offsetSiteX >= CLBArray[0].size())
                        continue;

                    std::vector<DeviceInfo::DeviceSite *> targetSites;
                    for (int i = 0; i < siteNum; ++i)
                        targetSites.push_back(deviceCLBArray[curSiteY + offsetSiteY + i][curSiteX + offsetSiteX]);
                    if (std::any_of(targetSites.begin(), targetSites.end(), [](DeviceInfo::DeviceSite *site) {
                            return site == nullptr;
                        }) || !legalSiteRange(curMacro, targetSites, 0, siteNum)) continue;
                    bool ok = true;
                    for (int i = 0; i < siteNum; i++)
                    {
                        if (CLBArray[curSiteY + offsetSiteY + i][curSiteX + offsetSiteX].macro)
                        {
                            if (CLBArray[curSiteY + offsetSiteY + i][curSiteX + offsetSiteX].macro != curMacro)
                            {
                                ok = false;
                                break;
                            }
                        }
                        else if (deviceCLBArray[curSiteY + offsetSiteY + i][curSiteX + offsetSiteX]->isMapped())
                        {
                            ok = false;
                            break;
                        }
                    }

                    if (ok)
                    {
                        int newY = curSiteY + offsetSiteY;
                        int newX = curSiteX + offsetSiteX;

                        curMacro->setAnchorLocationAndForgetTheOriginalOne(deviceCLBArray[newY][newX]->X(),
                                                                           deviceCLBArray[newY][newX]->Y());

                        assert(curMacro->getCARRYNum() + curMacro->getDSPNum() + curMacro->getBRAMNum() > 0);

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

                        changedPUs.insert(curMacro);
                        timingOptimizer->incrementalStaticTimingAnalysis(changedPUs, true);
                        float newFanoutDelay = timingOptimizer->getMaxIncrementalDelay();
                        // std::cout << " newX " << newX << " newY " << newY << " newFanoutDelay " << newFanoutDelay
                        //           << "\n";
                        timingOptimizer->recoverArrivalDelayFromLastIncrementalSTA();
                        changedPUs.clear();

                        if (bestDelay > newFanoutDelay)
                        {
                            bestDelay = newFanoutDelay;
                            bestOffsetSiteX = offsetSiteX;
                            bestOffsetSiteY = offsetSiteY;
                        }
                    }
                }
            }

            curMacro->setAnchorLocationAndForgetTheOriginalOne(oriPUX, oriPUY);
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
            changedPUs.insert(curMacro);
            timingOptimizer->incrementalStaticTimingAnalysis(changedPUs, true);

            if (bestOffsetSiteX > -10)
            {
                int newY = curSiteY + bestOffsetSiteY;
                int newX = curSiteX + bestOffsetSiteX;

                for (auto curSite : PU2LegalSites[curMacro])
                {
                    curSite->resetMapped();
                    CLBArray[curSite->getSiteY()][curSite->getSiteX()].macro = nullptr;
                    CLBArray[curSite->getSiteY()][curSite->getSiteX()].offsetInMacro = 0;
                }

                PU2LegalSites[curMacro].clear();
                for (int i = 0; i < siteNum; i++)
                {
                    assert(!CLBArray[newY + i][newX].macro);
                    deviceCLBArray[newY + i][newX]->setMapped();
                    assert(
                        CLBArray[deviceCLBArray[newY + i][newX]->getSiteY()][deviceCLBArray[newY + i][newX]->getSiteX()]
                            .macro == nullptr);
                    CLBArray[deviceCLBArray[newY + i][newX]->getSiteY()][deviceCLBArray[newY + i][newX]->getSiteX()]
                        .macro = curMacro;
                    CLBArray[deviceCLBArray[newY + i][newX]->getSiteY()][deviceCLBArray[newY + i][newX]->getSiteX()]
                        .offsetInMacro = i;
                    PU2LegalSites[curMacro].push_back(deviceCLBArray[newY + i][newX]);
                }

                PU2X[curMacro] = PU2LegalSites[curMacro][0]->X();
                PU2Y[curMacro] = PU2LegalSites[curMacro][0]->Y();
                fineTunedCnt++;
            }
        }
    }

    for (int i = 0; i < PULevelMatching.size(); i++)
    {
        auto targetPU = PULevelMatching[i].first;
        assert(PU2LegalSites[targetPU].size());
        PULevelMatching[i].second = PU2LegalSites[targetPU][0];
    }

    for (auto matchedPair : PULevelMatching)
    {
        auto targetPU = matchedPair.first;
        for (auto curSite : PU2LegalSites[targetPU])
        {
            assert(curSite->isMapped());
        }
    }

    placementInfo->setPULegalSite(PU2LegalSites);
    placementInfo->setPULegalXY(PU2X, PU2Y);

    print_info("MacroLegalizer: fine tune locations of " + std::to_string(fineTunedCnt) + " macros");
}
