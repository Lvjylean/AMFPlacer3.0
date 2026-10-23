#include "../../../utils/RuntimeProfiler.h"
/**
 * @file PlacementTimingOptimizer.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "PlacementTimingOptimizer.h"
#include "BoundaryAwareClusterer.h"

#include <cmath>
#include <chrono>
#include <codecvt>
#include <stdexcept>

PlacementTimingOptimizer::PlacementTimingOptimizer(PlacementInfo *placementInfo,
                                                   std::map<std::string, std::string> &JSONCfg)
    // U250 coefficients were fitted with tile-columns-subsites-v3 coordinates.
    // Keep the original coefficients on VCU108/095 and all other devices.
    : timingC1(placementInfo->getDeviceInfo()->getDeviceName() == "U250"
                   ? std::array<float, 10>{160.1553292055571, -64.94911625084538, 22.807442252015385,
                                           120.36632967195294, 27.120924804902547}
                   : std::array<float, 10>{123.05017047, -169.25614191, -117.28028144,
                                           208.53573639, 174.2573465}),
      timingC2(placementInfo->getDeviceInfo()->getDeviceName() == "U250"
                   ? std::array<float, 10>{202.12777470189823, -146.96155453685554, -131.59702340956898,
                                           163.46881911227194, 136.17523346378107}
                   : std::array<float, 10>{234.7694101, -433.99467294, -64.96319998,
                                           373.78606257, 139.45226658}),
      placementInfo(placementInfo), timingInfo(placementInfo->getTimingInfo()), JSONCfg(JSONCfg)
{
    AMF_PROFILE_FUNCTION("timing");
    if (JSONCfg.find("PlacementTimingOptimizerVerbose") != JSONCfg.end())
        verbose = JSONCfg["PlacementTimingOptimizerVerbose"] == "true";
    if (JSONCfg.find("y2xRatio") != JSONCfg.end())
        y2xRatio = std::stof(JSONCfg["y2xRatio"]);
    if (JSONCfg.find("DSPCritical") != JSONCfg.end())
        DSPCritical = JSONCfg["DSPCritical"] == "true";

    if (JSONCfg.count("SLRBoundaryDelayNs"))
    {
        const auto &value = JSONCfg.at("SLRBoundaryDelayNs");
        size_t used = 0;
        try
        {
            slrBoundaryDelayNs = std::stof(value, &used);
        }
        catch (const std::exception &)
        {
            throw std::invalid_argument("SLRBoundaryDelayNs must be a finite non-negative number in ns");
        }
        if (used != value.size() || !std::isfinite(slrBoundaryDelayNs) || slrBoundaryDelayNs < 0)
            throw std::invalid_argument("SLRBoundaryDelayNs must be a finite non-negative number in ns");
    }
    print_info("Timing model SLRBoundaryDelayNs=" + std::to_string(slrBoundaryDelayNs));
    designInfo = placementInfo->getDesignInfo();
    deviceInfo = placementInfo->getDeviceInfo();
    proPrimitiveTiming = JSONCfg.count("ProPrimitiveTiming") && JSONCfg.at("ProPrimitiveTiming") == "true";
    proLegacyIOBoundary = JSONCfg.count("ProLegacyIOBoundary") && JSONCfg.at("ProLegacyIOBoundary") == "true";
    if (proLegacyIOBoundary && (deviceInfo->getDeviceName() != "VCU108" || deviceInfo->getPhysicalBoundaryModel()))
        throw std::invalid_argument("ProLegacyIOBoundary requires the legacy VCU108/095 coordinate model");
    // An explicit opt-in may evaluate upstream primitive heuristics on U250.
    // Preserve their original DSPCritical, packing-stage and CARRY-stage gates.
    print_info(std::string("ProPrimitiveTiming=") + (proPrimitiveTiming ? "true" : "false") +
               " DSPCritical=" + (DSPCritical ? "true" : "false") +
               " ProLegacyIOBoundary=" + (proLegacyIOBoundary ? "true" : "false"));

    if (JSONCfg.find("noBlockAware") != JSONCfg.end())
    {
        noBlockAware = (JSONCfg["noBlockAware"] == "true");
    }
    print_info(std::string("Timing distance coefficients=") +
               (deviceInfo->getDeviceName() == "U250" ? "u250-tile-columns-20260929 (near legacy, middle/far fitted)"
                                                      : "legacy-095"));
    initPois();
}

void PlacementTimingOptimizer::setPinsLocation()
{
    AMF_PROFILE_FUNCTION("timing");
    using PinClock = std::chrono::steady_clock;
    const auto started = PinClock::now();
    auto secondsSince = [](PinClock::time_point t) {
        return std::chrono::duration<double>(PinClock::now()-t).count();
    };
    auto &cellLoc = placementInfo->getCellId2location();
    auto &pinLoc = placementInfo->getPinId2location();
    if (grid_update::mode() == grid_update::Mode::legacy)
        pinLoc = std::vector<PlacementInfo::Location>(designInfo->getPins().size());
    else
        // Keep -10 sentinels for pins without a cell, while reusing capacity.
        pinLoc.assign(designInfo->getPins().size(), PlacementInfo::Location{});
    const auto &cells = placementInfo->getCells();
#pragma omp parallel for if(grid_update::parallel()) schedule(static)
    for (size_t ci = 0; ci < cells.size(); ++ci)
    {
        auto tmpCell = cells[ci];
        int cellId = tmpCell->getCellId();
        auto tmpCellLoc = cellLoc[cellId];
        for (auto tmpPin : tmpCell->getPins())
        {
            float pinX = tmpCellLoc.X + tmpPin->getOffsetXInCell();
            float pinY = tmpCellLoc.Y + tmpPin->getOffsetYInCell();
            assert((unsigned int)tmpPin->getElementIdInType() < pinLoc.size());
            pinLoc[tmpPin->getElementIdInType()].X = pinX;
            pinLoc[tmpPin->getElementIdInType()].Y = pinY;
        }
    }
    const double updateSeconds = secondsSince(started);
    double referenceSeconds = 0, verifySeconds = 0;
    const auto verifyStarted = PinClock::now();
    if (grid_update::verify()) {
        std::vector<PlacementInfo::Location> reference(designInfo->getPins().size());
        for (auto cell : cells) {
            const auto loc = cellLoc[cell->getCellId()];
            for (auto pin : cell->getPins()) {
                auto &p = reference.at(pin->getElementIdInType());
                p.X = loc.X + pin->getOffsetXInCell();
                p.Y = loc.Y + pin->getOffsetYInCell();
            }
        }
        referenceSeconds = secondsSince(verifyStarted);
        for (size_t i=0;i<reference.size();++i)
            if (reference[i].X!=pinLoc[i].X || reference[i].Y!=pinLoc[i].Y)
                throw std::runtime_error("Pin-coordinate oracle mismatch at pin " + std::to_string(i));
        verifySeconds = secondsSince(verifyStarted);
    }
    incrementalUpdatedNodes.clear();
    ++pinUpdateCall;
    if (const char *path = std::getenv("AMF_GRID_UPDATE_TRACE")) {
        if (!pinUpdateTrace.is_open()) {
            pinUpdateTrace.open(std::string(path)+".pins.tsv");
            if (!pinUpdateTrace) throw std::runtime_error("Cannot open pin coordinate trace");
            pinUpdateTrace << "call\tmode\tpins\tupdate_s\tverify_s\tlegacy_reference_s\n";
        }
        pinUpdateTrace << pinUpdateCall << '\t' << static_cast<int>(grid_update::mode()) << '\t'
            << pinLoc.size() << '\t' << updateSeconds << '\t' << verifySeconds << '\t' << referenceSeconds << '\n';
        pinUpdateTrace.flush();
    }
}

float PlacementTimingOptimizer::getWorstSlackOfCell(DesignInfo::DesignCell *srcCell)
{
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    auto &cellLoc = placementInfo->getCellId2location();

    unsigned int srcCellId = srcCell->getCellId();
    auto srcNode = timingNodes[srcCellId];
    auto srcLoc = cellLoc[srcCellId];

    float worstSlack = 1000;
    for (auto outputNet : srcCell->getOutputNets())
    {
        for (auto pinBeDriven : outputNet->getPinsBeDriven())
        {
            // get the sinkPin information
            auto sinkCell = pinBeDriven->getCell();
            unsigned int sinkCellId = sinkCell->getCellId();
            auto sinkNode = timingNodes[sinkCellId];
            auto sinkLoc = cellLoc[sinkCellId];
            int succPathLen = sinkNode->getLongestPathLength();
            float netDelay = getDelayByModel(sinkNode, srcNode, sinkLoc.X, sinkLoc.Y, srcLoc.X, srcLoc.Y);
            float slack = sinkNode->getRequiredArrivalTime() - srcNode->getLatestOutputArrival() - netDelay;

            if (slack < worstSlack)
                worstSlack = slack;
        }
    }

    return worstSlack;
}

std::vector<int> PlacementTimingOptimizer::findCriticalPath()
{
    AMF_PROFILE_FUNCTION("timing");

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    float maxDelay = 0;
    int maxDelayId = -1;
    for (unsigned int i = 0; i < timingGraph->getNodes().size(); i++)
    {
        if (timingGraph->getNodes()[i]->getLatestInputArrival() > maxDelay)
        {
            maxDelay = timingGraph->getNodes()[i]->getLatestInputArrival();
            maxDelayId = i;
        }
    }

    std::vector<std::vector<int>> resPaths;
    auto resPath = maxDelayId >= 0 ? timingGraph->backTraceDelayLongestPathFromNode(maxDelayId) : std::vector<int>{};

    return resPath;
}

std::vector<std::vector<int>> PlacementTimingOptimizer::findCriticalPaths(float criticalRatio, bool checkOverlap,
                                                                          int pathNumThr, int converThr,
                                                                          bool backwardSearch)
{
    AMF_PROFILE_FUNCTION("timing");

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    timingGraph->sortedEndpointByDelay();
    std::vector<int> isCovered(timingGraph->getNodes().size(), 0);
    std::vector<std::vector<int>> resPaths;

    for (auto curEndpoint : timingGraph->getSortedTimingEndpoints_ByArrival())
    {
        if (isCovered[curEndpoint->getId()])
            continue;
        if (curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio * 0.9 ||
            curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio &&
                (resPaths.size() > pathNumThr / 4 || !clockEnhanced)) // testing
            break;
        std::vector<int> resPath;
        bool findSuccessfully =
            timingGraph->backTraceDelayLongestPathFromNode(curEndpoint->getId(), isCovered, resPath, converThr);
        if (findSuccessfully)
        {
            // std::cout << "find endpoint [" << curEndpoint->getDesignNode()
            //           << "] delay=" << curEndpoint->getLatestInputArrival() << " with " << resPath.size()
            //           << " nodes in path.\n";
            resPaths.push_back(resPath);
            if (checkOverlap)
            {
                bool hasNonRegDSP = false;
                if (DSPCritical && clockEnhanced)
                {
                    for (auto cellId : resPath)
                    {
                        auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                        if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                        {
                            if (PU->checkHasDSP())
                            {
                                hasNonRegDSP = true;
                                break;
                            }
                        }
                    }
                }

                if (!hasNonRegDSP)
                {
                    for (auto cellId : resPath)
                    {
                        auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PU))
                        {
                            isCovered[unpackedCell->getCell()->getCellId()]++;
                        }
                        else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                        {
                            for (auto cell : macro->getCells())
                            {
                                isCovered[cell->getCellId()]++;
                            }
                        }
                    }
                }
            }
        }

        if (resPaths.size() > pathNumThr)
            break;
    }

    print_info("PlacementTimingOptimizer: find #CriticalPaths=" + std::to_string(resPaths.size()) +
               " #pathDemand=" + std::to_string(pathNumThr));
    if (resPaths.size() < pathNumThr / 20 && backwardSearch)
    {
        auto moreCellIdsInCriticalPaths = findCriticalPaths_inv(criticalRatio, checkOverlap, pathNumThr, converThr);
        for (auto newPath : moreCellIdsInCriticalPaths)
            resPaths.push_back(newPath);
        print_info("PlacementTimingOptimizer: update more path by inverse DFS: find #CriticalPaths=" +
                   std::to_string(resPaths.size()));
    }

    return resPaths;
}

std::vector<std::vector<int>> PlacementTimingOptimizer::findCriticalPaths_tolerate(float criticalRatio,
                                                                                   bool checkOverlap, int pathNumThr,
                                                                                   int converThr, bool backwardSearch)
{
    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    timingGraph->sortedEndpointByDelay();
    std::vector<int> isCovered(timingGraph->getNodes().size(), 0);
    std::vector<std::vector<int>> resPaths;

    for (auto curEndpoint : timingGraph->getSortedTimingEndpoints_ByArrival())
    {
        if (isCovered[curEndpoint->getId()])
            continue;
        if (curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio * 0.9 ||
            curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio &&
                (resPaths.size() > pathNumThr / 4 || !clockEnhanced)) // testing
            break;
        std::vector<std::vector<int>> tmpPaths =
            timingGraph->backTraceDelayLongestPathsFromNodeWithTolerance(curEndpoint->getId(), 0.99);
        for (auto &resPath : tmpPaths)
        {
            resPaths.push_back(resPath);
        }

        if (resPaths.size() > pathNumThr)
            break;
    }

    print_info("PlacementTimingOptimizer: find #CriticalPaths=" + std::to_string(resPaths.size()) +
               " #pathDemand=" + std::to_string(pathNumThr));
    return resPaths;
}

std::vector<std::vector<int>> PlacementTimingOptimizer::findCriticalPaths_inv(float criticalRatio, bool checkOverlap,
                                                                              int pathNumThr, int converThr)
{
    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    timingGraph->sortedEndpointByDelay_inv();
    std::vector<int> isCovered(timingGraph->getNodes().size(), 0);
    std::vector<std::vector<int>> resPaths;

    for (auto curEndpoint : timingGraph->getSortedTimingEndpoints_ByArrival_inv())
    {
        if (isCovered[curEndpoint->getId()])
            continue;
        if (curEndpoint->getLatestInputArrival_inv() / timingGraph->getClockPeriod() < criticalRatio * 0.9 ||
            curEndpoint->getLatestInputArrival_inv() / timingGraph->getClockPeriod() < criticalRatio &&
                ((resPaths.size() > pathNumThr / 4 && clockEnhanced) ||
                 (resPaths.size() > pathNumThr / 20 && !clockEnhanced))) // testing
            break;
        std::vector<int> resPath;
        bool findSuccessfully =
            timingGraph->backTraceDelayLongestPathFromNode_inv(curEndpoint->getId(), isCovered, resPath, converThr);
        if (findSuccessfully)
        {
            // std::cout << "find endpoint [" << curEndpoint->getDesignNode()
            //           << "] delay=" << curEndpoint->getLatestInputArrival() << " with " << resPath.size()
            //           << " nodes in path.\n";
            resPaths.push_back(resPath);
            if (checkOverlap)
            {
                bool hasNonRegDSP = false;
                if (DSPCritical && clockEnhanced)
                {
                    for (auto cellId : resPath)
                    {
                        auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                        if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                        {
                            if (PU->checkHasDSP())
                            {
                                hasNonRegDSP = true;
                                break;
                            }
                        }
                    }
                }

                if (!hasNonRegDSP)
                {
                    for (auto cellId : resPath)
                    {
                        auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PU))
                        {
                            isCovered[unpackedCell->getCell()->getCellId()]++;
                        }
                        else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                        {
                            for (auto cell : macro->getCells())
                            {
                                isCovered[cell->getCellId()]++;
                            }
                        }
                    }
                }
            }
        }

        if (resPaths.size() > pathNumThr)
            break;
    }

    return resPaths;
}

void PlacementTimingOptimizer::encodeNodesInCriticalPaths(float criticalRatio, bool checkOverlap, int pathNumThr,
                                                          int converThr)
{
    print_status("PlacementTimingOptimizer: encoding nodes in critical paths");
    std::ofstream outfile0(JSONCfg["dumpDirectory"] + "cellEncode.txt");
    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    auto &nodes = timingInfo->getSimplePlacementTimingGraph()->getNodes();
    timingGraph->sortedEndpointByDelay();
    std::vector<int> isCovered(timingGraph->getNodes().size(), 0);
    dataPathsToBeClustered.clear();
    auto &cells = placementInfo->getDesignInfo()->getCells();
    auto &designNodeHash = placementInfo->getDesignNodeHash();
    designNodeHash = std::vector<int>(cells.size(), -1);
    cellInputCodes = std::vector<int>(cells.size(), -1);

    for (auto curEndpoint : timingGraph->getSortedTimingEndpoints_ByArrival())
    {
        if (isCovered[curEndpoint->getId()])
            continue;
        if (curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio)
            break;
        std::vector<int> resPath;
        bool findSuccessfully =
            timingGraph->backTraceDelayLongestPathFromNode(curEndpoint->getId(), isCovered, resPath, converThr);
        if (findSuccessfully)
        {
            dataPathsToBeClustered.push_back(resPath);
            if (checkOverlap)
            {
                for (auto cellId : resPath)
                {
                    auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                    if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PU))
                    {
                        isCovered[unpackedCell->getCell()->getCellId()]++;
                    }
                    else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                    {
                        for (auto cell : macro->getCells())
                        {
                            isCovered[cell->getCellId()]++;
                        }
                    }
                }
            }

            int entirePathCode = 0;

            for (int orderI = resPath.size() - 1; orderI >= 0; orderI--)
            {
                long long curNodeLocalCode;

                int inputCode = 0;
                if (cellInputCodes[resPath[orderI]] < 0)
                {
                    for (auto inNet : cells[resPath[orderI]]->getInputNets())
                    {
                        if (inNet->getDriverPins().size())
                        {
                            if (auto driverCell = inNet->getDriverPins()[0])
                            {
                                inputCode += driverCell->getCell()->getCellType() * 31;
                                inputCode *= 43;
                                inputCode %= 107;
                            }
                        }
                    }
                    cellInputCodes[resPath[orderI]] = inputCode;
                }
                else
                {
                    inputCode = cellInputCodes[resPath[orderI]];
                }

                curNodeLocalCode = ((cells[resPath[orderI]]->getCellType() + 1) * (21841 + orderI * 151) +
                                    (517619 + orderI * 379) * nodes[resPath[orderI]]->getOutEdges().size()) %
                                       999983 +
                                   inputCode;

                if (curNodeLocalCode < 0)
                    curNodeLocalCode = -curNodeLocalCode;

                entirePathCode += curNodeLocalCode * 113;
                entirePathCode %= 6893911;
            }

            for (int orderI = resPath.size() - 1; orderI >= 0; orderI--)
            {
                if (designNodeHash[resPath[orderI]] < 0)
                {
                    int curNodeGlobalCode =
                        (entirePathCode + (cells[resPath[orderI]]->getCellType() + 1) * (21841 + orderI * 151) +
                         (517619 + orderI * 379) * nodes[resPath[orderI]]->getOutEdges().size()) %
                            999983 +
                        cellInputCodes[resPath[orderI]];
                    if (curNodeGlobalCode < 0)
                        curNodeGlobalCode = -curNodeGlobalCode;

                    designNodeHash[resPath[orderI]] = curNodeGlobalCode;
                    outfile0 << cells[resPath[orderI]] << " hashCode:" << curNodeGlobalCode << "\n";
                }
            }
        }

        if (dataPathsToBeClustered.size() > pathNumThr)
            break;
    }
    outfile0.close();
    print_status("PlacementTimingOptimizer: encoded nodes in critical paths and #cluster=" +
                 std::to_string(dataPathsToBeClustered.size()));
}

void PlacementTimingOptimizer::clusterCriticalDataPath()
{
    auto &cells = placementInfo->getDesignInfo()->getCells();
    auto &clusters = placementInfo->getDatapathClusters();
    auto &nodes = timingInfo->getSimplePlacementTimingGraph()->getNodes();
    clusters.clear();

    std::vector<bool> isClustered(cells.size(), false);
    for (auto cellIdsInCriticalPath : dataPathsToBeClustered)
    {
        for (int orderI = 0; orderI < cellIdsInCriticalPath.size() && orderI < 10; orderI++)
        {
            int cellId = cellIdsInCriticalPath[orderI];
            if (isClustered[cellId])
                continue;
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            if (curPU->checkHasBRAM() || curPU->checkHasCARRY() || curPU->checkHasDSP())
                continue;
            if (!curPU->checkHasFF() && !curPU->checkHasLUT())
                continue;
            std::vector<DesignInfo::DesignCell *> *neighborCells =
                placementInfo->findNeiborsFromBinGridBasedOnHash(cells[cellId], 6);

            if (neighborCells->size() >= 3)
            {
                std::vector<DesignInfo::DesignCell *> curCluster;
                curCluster.clear();
                curCluster.push_back(cells[cellId]);

                for (auto tmpCell : *neighborCells)
                {
                    if (!isClustered[tmpCell->getCellId()])
                    {
                        curCluster.push_back(tmpCell);
                    }
                }

                if (curCluster.size() >= 3)
                {
                    clusters.push_back(curCluster);
                    for (auto tmpCell : curCluster)
                    {
                        isClustered[tmpCell->getCellId()] = true;
                    }
                }
            }

            delete neighborCells;
        }
    }
}

std::vector<std::vector<int>>
PlacementTimingOptimizer::findCriticalPaths(float criticalRatio, std::vector<bool> &FFDirectlyDrivenButNotInOneSlot)
{
    AMF_PROFILE_FUNCTION("timing");

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    timingGraph->sortedEndpointByDelay();
    std::vector<int> isCovered(timingGraph->getNodes().size(), 0);
    std::vector<std::vector<int>> resPaths;

    for (auto curEndpoint : timingGraph->getSortedTimingEndpoints_ByArrival())
    {
        if (isCovered[curEndpoint->getId()] || !FFDirectlyDrivenButNotInOneSlot[curEndpoint->getId()])
            continue;
        if (curEndpoint->getLatestInputArrival() / timingGraph->getClockPeriod() < criticalRatio)
            break;
        std::vector<int> resPath;
        bool findSuccessfully =
            timingGraph->backTraceDelayLongestPathFromNode(curEndpoint->getId(), isCovered, resPath, 100);
        if (findSuccessfully)
        {
            // std::cout << "find endpoint [" << curEndpoint->getDesignNode()
            //           << "] delay=" << curEndpoint->getLatestInputArrival() << " with " << resPath.size()
            //           << " nodes in path.\n";
            resPath.resize(2);
            resPaths.push_back(resPath);
            for (auto cellId : resPath)
            {
                auto PU = placementInfo->getPlacementUnitByCellId(cellId);
                if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(PU))
                {
                    isCovered[unpackedCell->getCell()->getCellId()] = 1;
                }
                else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                {
                    for (auto cell : macro->getCells())
                    {
                        isCovered[cell->getCellId()] = 1;
                    }
                }
            }
        }
    }

    return resPaths;
}

float PlacementTimingOptimizer::conductStaticTimingAnalysis(bool disableOptimisticTiming)
{
    AMF_PROFILE_FUNCTION("timing");
    print_status("PlacementTimingOptimizer: conducting Static Timing Analysis");

    unsigned int highFanoutThr = 10000;

    if (placementInfo->getNetDistributionByDensity(512) < 200)
    {
        highFanoutThr = 1000;
        print_warning("highFanoutThr is set to 1000");
    }

    if (enableCounter)
        STA_Cnt++;

    increaseLowDelayVal = false;
    if (disableOptimisticTiming)
        increaseLowDelayVal = true;

    effectFactor = (STA_Cnt / 30.0);
    if (effectFactor < 1)
        effectFactor = std::pow(effectFactor, 1);
    else
        effectFactor = (effectFactor - 1) * 0.3 + 1;

    bool printOut = false;
    std::string dumpFileName = "optNetDelayInfo.txt";
    std::ofstream outfile0;
    if (JSONCfg.find("PlacementTimingOptimizer_EdgesDelayLog") != JSONCfg.end())
    {
        printOut = true;
        std::string dumpFileName = JSONCfg["PlacementTimingOptimizer_EdgesDelayLog"];
        print_status("PlacementTimingOptimizer: dumping PlacementTimingOptimizer_EdgesDelayLog to: " + dumpFileName);
        outfile0.open(dumpFileName.c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for PlacementTimingOptimizer_EdgesDelayLog does not exist and please check "
               "your path settings");
    }

    // std::string cellName = "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //                        "face_detect_mac_mYie_DSP48_16_U/p";
    // "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/face_detect_mac_mYie_DSP48_16_U/"
    // "p";
    // int targetCellId = placementInfo->getDesignInfo()->getCell(cellName)->getCellId();
    // auto targetCellLoc = placementInfo->getCellId2location()[targetCellId];
    // print_warning("targetCellLoc X:" + std::to_string(targetCellLoc.X) + " Y:" + std::to_string(targetCellLoc.Y));

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    setPinsLocation();

    auto &pinLoc = placementInfo->getPinId2location();
    auto &cellLoc = placementInfo->getCellId2location();

    auto &edges = timingGraph->getEdges();
    int numEdges = edges.size();
    chainTimingEnabled = effectFactor > 1;

#pragma omp parallel for
    for (int i = 0; i < numEdges; i++)
    {
        auto edge = edges[i];
        auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
        auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
        if (pin1Loc.X < -5 && pin1Loc.Y < -5)
            continue;
        if (pin2Loc.X < -5 && pin2Loc.Y < -5)
            continue;

        float delay = getDelayByModel(edge->getSink(), edge->getSource(), pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
        //  if (edge->getSink()->checkIsRegister() && edge->getSource()->getOutEdges().size() > 1 &&
        // isPackingTiming)
        //     edge->setDelay(delay + 0.15);
        // else
        edge->setDelay(delay);
    }

    timingGraph->propogateArrivalTime();
    timingGraph->backPropogateRequiredArrivalTime();
    maxDelay = 0;
    int maxDelayId = -1;
    for (unsigned int i = 0; i < timingGraph->getNodes().size(); i++)
    {
        if (timingGraph->getNodes()[i]->getLatestInputArrival() > maxDelay &&
            timingGraph->getNodes()[i]->getSlowestPredecessorId() >= 0)
        {
            maxDelay = timingGraph->getNodes()[i]->getLatestInputArrival();
            maxDelayId = i;
        }
    }

    auto resPath = maxDelayId >= 0 ? timingGraph->backTraceDelayLongestPathFromNode(maxDelayId) : std::vector<int>{};
    std::cout << "An example of long delay path for the current placement:\n";

    print_warning("======================================resPathBackward=====================================\n");
    for (unsigned int i = 0; i < resPath.size(); i++)
    {
        auto id = resPath[i];
        if (id < 0) continue;
        if (i != 0)
        {
            for (auto edge : timingGraph->getNodes()[id]->getOutEdges())
            {
                auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
                auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
                if (pin1Loc.X < -5 && pin1Loc.Y < -5)
                    continue;
                if (pin2Loc.X < -5 && pin2Loc.Y < -5)
                    continue;
                float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
                if (edge->getSink()->getId() == resPath[i - 1])
                {
                    std::cout << designInfo->getCells()[id]
                              << " TotalFanout: " << timingGraph->getNodes()[id]->getTotalFanout()
                              << " LocalFanout:" << timingGraph->getNodes()[id]->getOutEdges().size()
                              << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y << " netDelay:" << delay
                              << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
                              << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime()
                              << "\n";
                    break;
                }
            }
        }
        else
        {
            std::cout << designInfo->getCells()[id] << " TotalFanout: " << timingGraph->getNodes()[id]->getTotalFanout()
                      << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y
                      << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
                      << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
        }
    }

    // print_warning("======================================DSP=====================================\n");
    // for (auto edge : timingGraph->getNodes()[targetCellId]->getInEdges())
    // {
    //     auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
    //     auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
    //     // if (pin1Loc.X < -5 && pin1Loc.Y < -5)
    //     //     continue;
    //     // if (pin2Loc.X < -5 && pin2Loc.Y < -5)
    //     //     continue;
    //     float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
    //     int id = edge->getSource()->getDesignNode()->getCellId();
    //     if (id == targetCellId)
    //         continue;
    //     std::cout << edge->getSource()->getDesignNode() << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y
    //               << " netDelay:" << delay << "   [forwardLevel]: " << timingGraph->getNodes()[id]->getForwardLevel()
    //               << "   [sinkLevel]: " << timingGraph->getNodes()[targetCellId]->getForwardLevel()
    //               << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //               << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    // }
    // auto similarPaths = timingGraph->backTraceDelayLongestPathsFromNodeWithTolerance(maxDelayId, 0.975);
    // for (int p = 0; p < 4 && p < similarPaths.size(); p++)
    // {
    //     print_warning("-------------------------------------------------------------------");
    //     float totalDelay = 0;
    //     std::reverse(similarPaths[p].begin(), similarPaths[p].end());
    //     for (int i = 0; i < similarPaths[p].size() - 1; i++)
    //     {
    //         auto id = similarPaths[p][i];
    //         auto curNode = timingGraph->getNodes()[id];
    //         for (auto edge : curNode->getOutEdges())
    //         {
    //             auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
    //             auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
    //             if (pin1Loc.X < -5 && pin1Loc.Y < -5)
    //                 continue;
    //             if (pin2Loc.X < -5 && pin2Loc.Y < -5)
    //                 continue;
    //             float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
    //             if (edge->getSinkPin()->getCell()->getCellId() == similarPaths[p][i + 1])
    //             {
    //                 totalDelay += curNode->getInnerDelay() + delay;
    //                 std::cout << edge->getSinkPin()->getCell() << " X:" << pin1Loc.X << " Y:" << pin1Loc.Y
    //                           << "  [InputTotalDelay]: " << totalDelay << "   [ToNextDelay]: " << delay << "\n";

    //                 break;
    //             }
    //         }
    //     }
    //     print_warning("-------------------------------------------------------------------");
    // }

    // float worstRequireDelay = 10;
    // int worstRequireDelayId = -1;
    // for (unsigned int i = 0; i < timingGraph->getNodes().size(); i++)
    // {
    //     if (timingGraph->getNodes()[i]->checkIsRegister() && !timingGraph->getNodes()[i]->getDesignNode()->isIO())
    //     {
    //         if (timingGraph->getNodes()[i]->getEndpointCriticality() < worstRequireDelay &&
    //             timingGraph->getNodes()[i]->getEarlestSuccessorId() >= 0)
    //         {
    //             worstRequireDelay = timingGraph->getNodes()[i]->getEndpointCriticality();
    //             worstRequireDelayId = i;
    //         }
    //     }
    // }

    // if (worstRequireDelayId == -1)
    // {
    //     timingGraph->sortedEndpointByRequiredDelay();
    //     auto &sortedList = timingGraph->getSortedTimingEndpoints_ByRequiredArrival();
    //     for (int i = 0; i < 50; i++)
    //     {
    //         std::cout << "cellInfo: " << timingGraph->getNodes()[sortedList[i]->getId()]->getDesignNode()
    //                   << " EndpointCriticality:"
    //                   << timingGraph->getNodes()[sortedList[i]->getId()]->getEndpointCriticality()
    //                   << " earlestSuccessorId:"
    //                   << timingGraph->getNodes()[sortedList[i]->getId()]->getEarlestSuccessorId() << "\n";
    //     }
    //     assert(false);
    // }

    // print_warning(
    //     "======================================resPathForward=====================================worstRequireDelay="
    //     + std::to_string(worstRequireDelay));

    // auto resPathForward = timingGraph->forwardTraceDelayLongestPathFromNode(worstRequireDelayId);
    // print_warning("======================================resPathForward====================================="
    //               "resPathForward.size()=" +
    //               std::to_string(resPathForward.size()));
    // std::reverse(resPathForward.begin(), resPathForward.end());
    // for (unsigned int i = 0; i < resPathForward.size(); i++)
    // {
    //     auto id = resPathForward[i];
    //     if (i != 0)
    //     {
    //         for (auto edge : timingGraph->getNodes()[id]->getOutEdges())
    //         {
    //             auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
    //             auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
    //             if (pin1Loc.X < -5 && pin1Loc.Y < -5)
    //                 continue;
    //             if (pin2Loc.X < -5 && pin2Loc.Y < -5)
    //                 continue;
    //             float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
    //             if (edge->getSink()->getId() == resPathForward[i - 1])
    //             {
    //                 std::cout << designInfo->getCells()[id]
    //                           << " TotalFanout: " << timingGraph->getNodes()[id]->getTotalFanout()
    //                           << " LocalFanout:" << timingGraph->getNodes()[id]->getOutEdges().size()
    //                           << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y << " netDelay:" << delay
    //                           << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                           << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime()
    //                           << "\n";
    //                 break;
    //             }
    //         }
    //     }
    //     else
    //     {
    //         std::cout << designInfo->getCells()[id] << " TotalFanout: " <<
    //         timingGraph->getNodes()[id]->getTotalFanout()
    //                   << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y
    //                   << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                   << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    //     }
    // }

    // // // important:::  chip/tile1/g_ariane_core.core/ariane/id_stage_i/operand_b_q[63]_i_31__0
    // print_warning("===========================================================================\n");
    // std::string cellNames[28] = {
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/bank_mapping_load_1_reg_63008_reg[3]",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_5920",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_5919",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_5914",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_4230",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_4227",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_2634",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_1598",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_3500",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_2118",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_1123",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_747",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_331",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_347",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_114",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_42",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_10",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_18",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_2",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m_i_1",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/m",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U79/"
    //     "face_detect_mac_mYie_DSP48_16_U/p",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mac_mYie_U78/"
    //     "face_detect_mac_mYie_DSP48_16_U/p",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mul_mWhU_U76/"
    //     "face_detect_mul_mWhU_DSP48_14_U/icmp_ln3402_reg_64833[0]_i_27",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mul_mWhU_U76/"
    //     "face_detect_mul_mWhU_DSP48_14_U/icmp_ln3402_reg_64833_reg[0]_i_3",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mul_mWhU_U76/"
    //     "face_detect_mul_mWhU_DSP48_14_U/icmp_ln3402_reg_64833_reg[0]_i_2",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/face_detect_mul_mWhU_U76/"
    //     "face_detect_mul_mWhU_DSP48_14_U/icmp_ln3402_reg_64833[0]_i_1",
    //     "design_1_i/face_detect_0/inst/grp_processImage_fu_371/icmp_ln3402_reg_64833_reg[0]"};

    // std::vector<std::string> cellNameVec(cellNames, cellNames + 28);
    // float totalDelay = 0;
    // for (int i = 0; i < cellNameVec.size() - 1; i++)
    // {
    //     auto id = designInfo->getCell(cellNameVec[i])->getCellId();
    //     auto curNode = timingGraph->getNodes()[id];
    //     for (auto edge : curNode->getOutEdges())
    //     {
    //         auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
    //         auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
    //         if (pin1Loc.X < -5 && pin1Loc.Y < -5)
    //             continue;
    //         if (pin2Loc.X < -5 && pin2Loc.Y < -5)
    //             continue;
    //         float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
    //         if (edge->getSinkPin()->getCell()->getName() == cellNameVec[i + 1])
    //         {
    //             totalDelay += curNode->getInnerDelay() + delay;
    //             std::cout << edge->getSinkPin()->getCell() << "X:" << pin1Loc.X << " Y:" << pin1Loc.Y
    //                       << "  [InputTotalDelay]: " << totalDelay
    //                       << "  [STAInputDelay]: " << edge->getSink()->getLatestInputArrival()
    //                       << "   [ToNextDelay]: " << delay << "\n";

    //             break;
    //         }
    //     }
    // }
    if (printOut)
    {
        for (auto node : timingInfo->getSimplePlacementTimingInfo())
        {
            if (node->getOutEdges().size() > 32)
                continue;
            for (auto edge : node->getOutEdges())
            {
                outfile0 << " src:" << edge->getSourcePin()->getName()
                         << " TotalFanout:" << edge->getSource()->getTotalFanout()
                         << " LocalFanout:" << edge->getSource()->getOutEdges().size()
                         << " sink:" << edge->getSinkPin()->getName() << " delay:" << edge->getDelay() << "\n";
            }
        }
        outfile0.close();
    }

    if(deviceInfo->getPhysicalBoundaryModel())
    {
        BoundaryAwareClusterer physical(placementInfo,this,JSONCfg);
        if(placementInfo->boundaryClusteringEnabled())physical.refresh();
        physical.audit("sta-"+std::to_string(STA_Cnt));
    }
    return maxDelay;
}

float PlacementTimingOptimizer::getSlackThr()
{
    AMF_PROFILE_FUNCTION("timing");
    assert(placementInfo->getTimingInfo());

    // float maxEnhanceRatio = 0;
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    auto &cellLoc = placementInfo->getCellId2location();
    assert(cellLoc.size() == timingNodes.size());

    // std::ofstream outfile0("timingOptProc.log");

    // auto deviceInfo = placementInfo->getDeviceInfo();

    std::vector<int> slackCntVec(200, 0);
    int totalSlackChecked = 0;

    netActualSlackPinNum.clear();

    for (auto curNet : placementInfo->getPlacementNets())
    {
        auto designNet = curNet->getDesignNet();
        if (designNet->checkIsPowerNet() || designNet->checkIsGlobalClock())
            continue;

        if (curNet->getDriverUnits().size() != 1 || curNet->getUnits().size() <= 1 || curNet->getUnits().size() >= 1000)
            continue;
        auto &pins = designNet->getPins();
        int pinNum = pins.size();

        assert(curNet->getUnits().size() == (unsigned int)pinNum);

        int driverPinInNet = -1;

        for (int i = 0; i < pinNum; i++)
        {
            if (pins[i]->isOutputPort())
            {
                driverPinInNet = i;
                break;
            }
        }

        assert(driverPinInNet >= 0);

        // get the srcPin information
        auto srcCell = pins[driverPinInNet]->getCell();
        unsigned int srcCellId = srcCell->getCellId();
        auto srcNode = timingNodes[srcCellId];
        auto srcLoc = cellLoc[srcCellId];
        // iterate the sinkPin for evaluation and enhancement

        netActualSlackPinNum[curNet] = 0;
        for (int pinBeDriven = 0; pinBeDriven < pinNum; pinBeDriven++)
        {
            if (pinBeDriven == driverPinInNet)
                continue;

            // get the sinkPin information
            auto sinkCell = pins[pinBeDriven]->getCell();
            unsigned int sinkCellId = sinkCell->getCellId();
            auto sinkNode = timingNodes[sinkCellId];
            auto sinkLoc = cellLoc[sinkCellId];
            float netDelay = getDelayByModel(sinkNode, srcNode, sinkLoc.X, sinkLoc.Y, srcLoc.X, srcLoc.Y);
            float slack = sinkNode->getRequiredArrivalTime() - srcNode->getLatestOutputArrival() - netDelay;

            if (slack > 0)
                continue;

            netActualSlackPinNum[curNet]++;
            int slotId = (int)(-slack / 0.1);
            if (slotId >= 100)
                slotId = 100;
            if (slotId < 0)
                slotId = 0;
            slackCntVec[slotId] += 1;
            totalSlackChecked++;
        }
    }

    float slackThr = 0;
    int slackUnderThrCnt = 0;
    float careRatio = 0.333;

    if (careRatio <= 0.9 && totalSlackChecked > 0)
    {
        for (unsigned int i = 0; i < slackCntVec.size(); i++)
        {
            slackUnderThrCnt += slackCntVec[i];
            if (slackUnderThrCnt > totalSlackChecked * (1 - careRatio))
                break;
            slackThr -= 0.1;
        }
    }

    std::string outputStr = "";
    for (unsigned int i = 0; i < slackCntVec.size(); i++)
    {
        outputStr += " " + std::to_string(slackCntVec[i]);
    }
    print_info("Slack distribution:" + outputStr);
    print_info("slackThr = " + std::to_string(slackThr));
    return slackThr;
}

void PlacementTimingOptimizer::enhanceFFControlSetNets()
{
    print_status("PlacementTimingOptimizer: enhancing FF Control Set Nets");
    auto &controlSets = designInfo->getControlSets();
    auto &cells = designInfo->getCells();
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    float pathLen = placementInfo->getLongPathThresholdLevel();

    int enhancedCnt = 0;
    float enhancedAvgRatio = 0;
    if (controlSets.size()) //  && controlSets.size() > cells.size() / 5000
    {
        float enhanceRatio = std::pow((float)40.0 / placementInfo->getClusterNum(), 0.5);
        for (auto CS : controlSets)
        {
            if (CS->getCE())
            {
                if (CS->getCE()->getPins().size() < 5000 && CS->getCE()->getPins().size() > 200)
                {
                    CS->getCE()->setOverallClusterNetEnhancement(1.5 * enhanceRatio);
                    enhancedAvgRatio += 1.5 * enhanceRatio;
                    enhancedCnt++;
                    continue;
                }
            }
            if (CS->getSR())
            {
                if (CS->getSR()->getPins().size() < 5000 && CS->getSR()->getPins().size() > 200)
                {
                    CS->getSR()->setOverallClusterNetEnhancement(1.5 * enhanceRatio);
                    enhancedAvgRatio += 1.5 * enhanceRatio;
                    enhancedCnt++;
                    continue;
                }
            }
        }
    }
    print_status("PlacementTimingOptimizer: enhanced FF Control Set Nets (num=" + std::to_string(enhancedCnt) +
                 ", by averagely " + std::to_string(enhancedAvgRatio / enhancedCnt) + ")");
}

void PlacementTimingOptimizer::incrementalStaticTimingAnalysis(
    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> movedPUs, bool noBackPropogation)
{
    AMF_PROFILE_FUNCTION("timing");
    incrementalNodeSnapshot.clear();
    incrementalEdgeSnapshot.clear();
    incrementalPinSnapshot.clear();

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    timingGraph->invalidateReverseArrival();
    // Capture each changed edge once, before any endpoint updates it.
    for (auto pu : movedPUs)
    {
        std::vector<DesignInfo::DesignCell *> cells;
        if (auto cell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) cells.push_back(cell->getCell());
        else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu)) cells = macro->getCells();
        for (auto cell : cells)
        {
            auto node = timingGraph->getNodes().at(cell->getCellId());
            for (auto edge : node->getInEdges()) incrementalEdgeSnapshot.emplace(edge, edge->getDelay());
            for (auto edge : node->getOutEdges()) incrementalEdgeSnapshot.emplace(edge, edge->getDelay());
            for (auto pin : cell->getPins())
                incrementalPinSnapshot.emplace(pin->getElementIdInType(), placementInfo->getPinId2location().at(pin->getElementIdInType()));
        }
    }

    // update pin location
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    auto &pinLoc = placementInfo->getPinId2location();

    for (auto curPU : movedPUs)
    {
        if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
        {
            int binIdX, binIdY;
            float cellX = curUnpackedCell->X();
            float cellY = curUnpackedCell->Y();
            DesignInfo::DesignCell *curCell = curUnpackedCell->getCell();
            cellLoc[curCell->getCellId()].X = cellX;
            cellLoc[curCell->getCellId()].Y = cellY;

            auto tmpCellLoc = cellLoc[curCell->getCellId()];
            for (auto tmpPin : curCell->getPins())
            {
                float pinX = tmpCellLoc.X + tmpPin->getOffsetXInCell();
                float pinY = tmpCellLoc.Y + tmpPin->getOffsetYInCell();
                assert((unsigned int)tmpPin->getElementIdInType() < pinLoc.size());
                pinLoc[tmpPin->getElementIdInType()].X = pinX;
                pinLoc[tmpPin->getElementIdInType()].Y = pinY;
            }
        }
        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
        {
            for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
            {
                float offsetX_InMacro, offsetY_InMacro;
                DesignInfo::DesignCellType cellType;
                curMacro->getVirtualCellInfo(vId, offsetX_InMacro, offsetY_InMacro, cellType);
                DesignInfo::DesignCell *curCell = curMacro->getCell(vId);

                int binIdX, binIdY;
                float cellX = curMacro->X() + offsetX_InMacro;
                float cellY = curMacro->Y() + offsetY_InMacro;

                cellLoc[curCell->getCellId()].X = cellX;
                cellLoc[curCell->getCellId()].Y = cellY;

                int cellId = curCell->getCellId();
                auto tmpCellLoc = cellLoc[cellId];
                for (auto tmpPin : curCell->getPins())
                {
                    float pinX = tmpCellLoc.X + tmpPin->getOffsetXInCell();
                    float pinY = tmpCellLoc.Y + tmpPin->getOffsetYInCell();
                    assert((unsigned int)tmpPin->getElementIdInType() < pinLoc.size());
                    pinLoc[tmpPin->getElementIdInType()].X = pinX;
                    pinLoc[tmpPin->getElementIdInType()].Y = pinY;
                }
            }
        }
    }

    // update delay
    auto &timingNodes = timingInfo->getSimplePlacementTimingInfo();
    auto updatedNodes = std::vector<int>(0);
    for (auto curPU : movedPUs)
    {
        if (auto curUnpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
        {
            DesignInfo::DesignCell *curCell = curUnpackedCell->getCell();
            updateDelayRelatedToCell(curCell);
            updatedNodes.push_back(curCell->getCellId());
        }
        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
        {
            for (int vId = 0; vId < curMacro->getNumOfCells(); vId++)
            {
                DesignInfo::DesignCell *curCell = curMacro->getCell(vId);
                updateDelayRelatedToCell(curCell);
                updatedNodes.push_back(curCell->getCellId());
            }
        }
    }

    forwardBFSToFindNodesToBeUpdated(updatedNodes);
    // std::cout << "updatedNodes info (before):\n";
    // if (updatedNodes.size() > 0 && updatedNodes.size() < 5)
    // {
    //     for (auto id : incrementalUpdatedNodes)
    //     {
    //         std::cout << designInfo->getCells()[id]
    //                   << "   [delay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                   << "   [required]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    //         if (timingGraph->getNodes()[id]->getSlowestPredecessorId() >= 0)
    //         {
    //             int tmpId = timingGraph->getNodes()[id]->getSlowestPredecessorId();
    //             std::cout << "              pred:" << designInfo->getCells()[tmpId]
    //                       << "   [delay]: " << timingGraph->getNodes()[tmpId]->getLatestInputArrival()
    //                       << "   [required]: " << timingGraph->getNodes()[tmpId]->getRequiredArrivalTime() << "\n";
    //         }
    //     }
    // }

    incremental_propogateArrivalTime();
    // std::cout << "updatedNodes info (after):\n";
    // if (updatedNodes.size() > 0 && updatedNodes.size() < 5)
    // {
    //     for (auto id : incrementalUpdatedNodes)
    //     {
    //         std::cout << designInfo->getCells()[id]
    //                   << "   [delay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                   << "   [required]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    //         if (timingGraph->getNodes()[id]->getSlowestPredecessorId() >= 0)
    //         {
    //             int tmpId = timingGraph->getNodes()[id]->getSlowestPredecessorId();
    //             std::cout << "              pred:" << designInfo->getCells()[tmpId]
    //                       << "   [delay]: " << timingGraph->getNodes()[tmpId]->getLatestInputArrival()
    //                       << "   [required]: " << timingGraph->getNodes()[tmpId]->getRequiredArrivalTime() << "\n";
    //         }
    //     }
    // }

    if (!noBackPropogation)
    {
        backwardBFSToFindNodesToBeUpdated();
        incremental_backPropogateRequiredArrivalTime();
    }

    incr_maxDelay = 0.0;
    incr_totalTimingBenefit = 0;
    int maxDelayId = -1;
    for (unsigned int i = 0; !incremental_forwardLevel2NodeIds.empty() && i < incremental_forwardLevel2NodeIds[0].size(); i++)
    {
        if (timingGraph->getNodes()[incremental_forwardLevel2NodeIds[0][i]]->getLatestInputArrival() > incr_maxDelay)
        {
            incr_maxDelay = timingGraph->getNodes()[incremental_forwardLevel2NodeIds[0][i]]->getLatestInputArrival();
            maxDelayId = incremental_forwardLevel2NodeIds[0][i];
        }
    }

    int effCnt = 0;

    for (unsigned int i = 0; !incremental_forwardLevel2NodeIds.empty() && i < incremental_forwardLevel2NodeIds[0].size(); i++)
    {
        if (timingGraph->getNodes()[incremental_forwardLevel2NodeIds[0][i]]->getLatestInputArrival() >
            incr_maxDelay * 0.9)
        {
            effCnt++;
            incr_totalTimingBenefit +=
                (timingGraph->getNodes()[incremental_forwardLevel2NodeIds[0][i]]
                     ->getLatestInputArrival_inLastIteration() -
                 timingGraph->getNodes()[incremental_forwardLevel2NodeIds[0][i]]->getLatestInputArrival());
        }
    }

    if (effCnt)
        incr_totalTimingBenefit /= effCnt;

    // auto resPath = maxDelayId >= 0 ? timingGraph->backTraceDelayLongestPathFromNode(maxDelayId) : std::vector<int>{};

    // std::cout << "An example of long delay path for the current placement (incremental STA) and incr_maxDelay="
    //           << incr_maxDelay << "\n";

    // for (unsigned int i = 0; i < resPath.size(); i++)
    // {
    //     auto id = resPath[i];
    //     if (i != 0)
    //     {
    //         for (auto edge : timingGraph->getNodes()[id]->getOutEdges())
    //         {
    //             auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
    //             auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
    //             if (pin1Loc.X < -5 && pin1Loc.Y < -5)
    //                 continue;
    //             if (pin2Loc.X < -5 && pin2Loc.Y < -5)
    //                 continue;
    //             float delay = getDelayByModel(pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y);
    //             if (edge->getSink()->getId() == resPath[i - 1])
    //             {
    //                 std::cout << designInfo->getCells()[id]
    //                           << " TotalFanout: " << timingGraph->getNodes()[id]->getTotalFanout()
    //                           << " LocalFanout:" << timingGraph->getNodes()[id]->getOutEdges().size()
    //                           << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y << " netDelay:" << delay
    //                           << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                           << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime()
    //                           << "\n";
    //                 break;
    //             }
    //         }
    //     }
    //     else
    //     {
    //         std::cout << designInfo->getCells()[id] << " TotalFanout: " <<
    //         timingGraph->getNodes()[id]->getTotalFanout()
    //                   << " X:" << cellLoc[id].X << " Y:" << cellLoc[id].Y
    //                   << "   [arrivalDelay]: " << timingGraph->getNodes()[id]->getLatestInputArrival()
    //                   << "   [requiredDelay]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    //     }
    // }
}

void PlacementTimingOptimizer::clusterLongPathInOneClockRegion(int pathLenThr, float clusterThrRatio)
{
    AMF_PROFILE_FUNCTION("timing");

    if (noBlockAware)
    {
        auto &PU2ClockRegionCenter = placementInfo->getPU2ClockRegionCenters();
        auto &PU2ClockRegionColumn = placementInfo->getPU2ClockRegionColumn();
        PU2ClockRegionCenter.clear();
        PU2ClockRegionColumn.clear();
        return;
    }
    // if (DSPCritical)
    //     return;
    placementInfo->updateElementBinGrid();
    conductStaticTimingAnalysis();
    print_warning("PlacementTimingOptimizer: clustering long path in one clock region");

    auto &sortedTimingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    // auto &sortedTimingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    auto simpleTimingGraph = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph();
    auto &cellLoc = placementInfo->getCellId2location();
    auto deviceInfo = placementInfo->getDeviceInfo();
    auto YX2ClockRegion = deviceInfo->getClockRegions();
    auto &PU2ClockRegionCenter = placementInfo->getPU2ClockRegionCenters();
    auto &PU2ClockRegionColumn = placementInfo->getPU2ClockRegionColumn();
    PU2ClockRegionCenter.clear();
    PU2ClockRegionColumn.clear();

    std::set<int> extractedCellIds;
    std::set<PlacementInfo::PlacementUnit *> extractedPUs;

    extractedCellIds.clear();
    extractedPUs.clear();
    clockRegionclusters.clear();

    unsigned int fanoutThr = 3000; // placementInfo->getHighFanOutThr();
    int sizeThr = 20000;
    if (placementInfo->isDensePlacement() || clockRegionClusterTooLarge)
        sizeThr = 2000;

    std::set<DesignInfo::DesignCell *> highFanoutRegs;
    if (initialClustering && placementInfo->getClusterNum() <= 20 && false)
    {
        for (unsigned int nodeId = 0; nodeId < timingNodes.size(); nodeId++)
        {
            auto timingNode = timingNodes[nodeId];
            if (timingNode->getForwardLevel() == 1 && timingNode->getLongestPathLength() > pathLenThr * 0.9)
            {
                for (auto inEdge : timingNode->getInEdges())
                {
                    auto driverNode = inEdge->getSource();
                    if (auto driverCell = driverNode->getDesignNode())
                    {
                        if (driverNode->getForwardLevel() == 0)
                        {
                            if (driverNode->getOutEdges().size() >= 128)
                            {
                                if (highFanoutRegs.find(driverCell) == highFanoutRegs.end())
                                {
                                    float left = 1000, right = -1000, top = -1000, bottom = 1000;
                                    for (auto outEdge : driverNode->getOutEdges())
                                    {
                                        auto fanoutNode = outEdge->getSink();
                                        if (fanoutNode->getLongestPathLength() > pathLenThr * 0.9)
                                        {
                                            auto tmpLoc = cellLoc[fanoutNode->getId()];
                                            if (tmpLoc.Y < bottom)
                                                bottom = tmpLoc.Y;
                                            if (tmpLoc.Y > top)
                                                top = tmpLoc.Y;
                                            if (tmpLoc.X < left)
                                                left = tmpLoc.X;
                                            if (tmpLoc.X > right)
                                                right = tmpLoc.X;
                                        }
                                    }
                                    if (right - left + y2xRatio * (top - bottom) > 40)
                                    {
                                        highFanoutRegs.insert(driverCell);
                                        std::cout << driverCell << " has " << driverNode->getOutEdges().size()
                                                  << " fanouts level=" << timingNode->getLongestPathLength()
                                                  << " left=" << left << " right=" << right << " bottom=" << bottom
                                                  << " top=" << top
                                                  << " HPWL=" << right - left + y2xRatio * (top - bottom) << "\n";
                                        // if (extractedCellIds.find(nodeId) != extractedCellIds.end())
                                        //     continue;
                                        // auto candidateCellIds = simpleTimingGraph->BFSFromNode(
                                        //     driverNode->getId(), pathLenThr * 0.6666, sizeThr, extractedCellIds);
                                        // std::cout << driverCell << " has " << driverNode->getOutEdges().size()
                                        //           << " fanouts level=" << timingNode->getLongestPathLength()
                                        //           << " left=" << left << " right=" << right << " bottom=" << bottom
                                        //           << " top=" << top
                                        //           << " HPWL=" << right - left + y2xRatio * (top - bottom) << " with "
                                        //           << candidateCellIds.size() << " succ cells\n";

                                        // if (candidateCellIds.size() >= 4000)
                                        // {
                                        //     std::cout << driverCell << " has " << driverNode->getOutEdges().size()
                                        //               << " fanouts level=" << timingNode->getLongestPathLength()
                                        //               << " left=" << left << " right=" << right << " bottom=" <<
                                        //               bottom
                                        //               << " top=" << top
                                        //               << " HPWL=" << right - left + y2xRatio * (top - bottom)
                                        //               << " with " << candidateCellIds.size()
                                        //               << " succ cells excluded from clustering\n";
                                        //     std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                                        //     PUsInLongPaths.clear();

                                        //     float totalWeight = 0;
                                        //     for (auto &candidateCellId : candidateCellIds)
                                        //     {
                                        //         auto PUInPath =
                                        //             placementInfo->getPlacementUnitByCellId(candidateCellId);
                                        //         if (extractedPUs.find(PUInPath) == extractedPUs.end() &&
                                        //             PUsInLongPaths.find(PUInPath) == PUsInLongPaths.end())
                                        //         {
                                        //             PUsInLongPaths.insert(PUInPath);
                                        //             totalWeight += PUInPath->getWeight();
                                        //         }
                                        //     }

                                        //     if (totalWeight >= pathLenThr)
                                        //     {
                                        //         std::map<std::pair<int, int>, float> clockRegionYX2Cnt;
                                        //         clockRegionYX2Cnt.clear();

                                        //         float maxClockRegionWeight = 0;
                                        //         float totalClockRegionWeight = 0;
                                        //         std::pair<int, int> optClockLocYX;

                                        //         for (auto tmpPU : PUsInLongPaths)
                                        //         {
                                        //             extractedPUs.insert(tmpPU);
                                        //             if (tmpPU->isFixed())
                                        //                 continue;
                                        //             if (auto unpackCell =
                                        //                     dynamic_cast<PlacementInfo::PlacementUnpackedCell
                                        //                     *>(tmpPU))
                                        //             {
                                        //                 extractedCellIds.insert(unpackCell->getCell()->getCellId());
                                        //             }
                                        //             else if (auto curMacro =
                                        //                          dynamic_cast<PlacementInfo::PlacementMacro
                                        //                          *>(tmpPU))
                                        //             {

                                        //                 int cellIdInMacro = 0;
                                        //                 for (auto tmpCell : curMacro->getCells())
                                        //                 {
                                        //                     extractedCellIds.insert(tmpCell->getCellId());
                                        //                 }
                                        //             }
                                        //         }
                                        //     }
                                        // }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool checkRegFanout = highFanoutRegs.size() >= 4;

    print_warning("PlacementTimingOptimizer: clustering long path in one clock region with sizeThr=" +
                  std::to_string(sizeThr));
    for (unsigned int nodeId = 0; nodeId < sortedTimingNodes.size() * 0.1; nodeId++)
    {
        auto timingNode = sortedTimingNodes[nodeId];
        int startNodeId = timingNode->getId();
        if (timingNode->getLongestPathLength() > pathLenThr)
        {
            // if (timingNode->getForwardLevel() == 1 && checkRegFanout)
            // {
            //     for (auto inEdge : timingNode->getInEdges())
            //     {
            //         auto driverNode = inEdge->getSource();
            //         if (auto driverCell = driverNode->getDesignNode())
            //         {
            //             if (driverNode->getForwardLevel() == 0)
            //             {
            //                 if (driverNode->getOutEdges().size() >= 128)
            //                 {
            //                     startNodeId = driverCell->getCellId();
            //                 }
            //             }
            //         }
            //     }
            // }
            if (extractedCellIds.find(startNodeId) != extractedCellIds.end())
                continue;
            auto candidateCellIds =
                simpleTimingGraph->DFSFromNode(startNodeId, pathLenThr, sizeThr, extractedCellIds, 1000000);

            if (candidateCellIds.size() >= pathLenThr * 0.8)
            {
                std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                PUsInLongPaths.clear();

                float totalWeight = 0;
                for (auto &candidateCellId : candidateCellIds)
                {
                    auto PUInPath = placementInfo->getPlacementUnitByCellId(candidateCellId);
                    if (extractedPUs.find(PUInPath) == extractedPUs.end() &&
                        PUsInLongPaths.find(PUInPath) == PUsInLongPaths.end())
                    {
                        PUsInLongPaths.insert(PUInPath);
                        totalWeight += PUInPath->getWeight();
                    }
                }

                if (totalWeight >= pathLenThr)
                {
                    std::map<std::pair<int, int>, float> clockRegionYX2Cnt;
                    clockRegionYX2Cnt.clear();

                    float maxClockRegionWeight = 0;
                    float totalClockRegionWeight = 0;
                    std::pair<int, int> optClockLocYX;

                    for (auto tmpPU : PUsInLongPaths)
                    {
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                        {
                            int cellId = unpackedCell->getCell()->getCellId();
                            int clockRegionX, clockRegionY;
                            auto tmpLoc = cellLoc[cellId];
                            deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);
                            std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                            // if (clockRegionX < 2)
                            //     clockRegionX = 1;
                            // if (clockRegionX > 2)
                            //     clockRegionX = 3;
                            if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                            {
                                clockRegionYX2Cnt[tmpClockLocYX] = 0;
                            }
                            clockRegionYX2Cnt[tmpClockLocYX] += 1;
                            totalClockRegionWeight += 1;

                            if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                            {
                                maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                optClockLocYX = tmpClockLocYX;
                            }
                        }
                        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                        {
                            for (auto tmpCell : curMacro->getCells())
                            {
                                int cellId = tmpCell->getCellId();
                                int clockRegionX, clockRegionY;
                                auto tmpLoc = cellLoc[cellId];
                                deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);

                                std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                                if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                                {
                                    clockRegionYX2Cnt[tmpClockLocYX] = 0;
                                }
                                clockRegionYX2Cnt[tmpClockLocYX] += 1;
                                totalClockRegionWeight += 1;

                                if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                                {
                                    maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                    optClockLocYX = tmpClockLocYX;
                                }
                            }
                        }
                    }

                    if ((maxClockRegionWeight > totalClockRegionWeight * clusterThrRatio) &&
                        maxClockRegionWeight >= pathLenThr * 0.333)
                    {
                        // if (optClockLocYX.second < 2)
                        //     optClockLocYX.second = 1;
                        // if (optClockLocYX.second > 2)
                        //     optClockLocYX.second = 3;
                        auto optClockRegion = YX2ClockRegion[0][optClockLocYX.second];
                        float cX = (optClockRegion->getLeft() + optClockRegion->getRight()) / 2;
                        std::vector<int> PUIdsInLongPaths;
                        PUIdsInLongPaths.clear();
                        bool abandon = false;
                        for (auto curPU : PUsInLongPaths)
                        {
                            if (curPU->checkHasDSP())
                            {
                                if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                                {
                                    for (auto tmpCell : curMacro->getCells())
                                    {
                                        if (timingNodes[tmpCell->getCellId()]->getBackwardLevel() > 10)
                                            abandon = true;
                                    }
                                }
                            }
                            if (abandon)
                                break;
                        }
                        if (abandon)
                            break;
                        for (auto curPU : PUsInLongPaths)
                        {
                            if (!curPU->isFixed() && !curPU->checkHasBRAM() && !curPU->checkHasDSP() && !curPU->checkHasURAM())
                            {
                                float fX = cX;
                                float fY = curPU->Y();
                                placementInfo->legalizeXYInArea(curPU, fX, fY);
                                // curPU->setAnchorLocationAndForgetTheOriginalOne(fX, fY);
                                extractedPUs.insert(curPU);

                                PU2ClockRegionCenter[curPU] = std::pair<float, float>(fX, fY);
                                PU2ClockRegionColumn[curPU] = optClockLocYX.second;

                                if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
                                {
                                    int cellId = unpackedCell->getCell()->getCellId();
                                    //  if (timingNodes[cellId]->getOutEdges().size() < fanoutThr)
                                    extractedCellIds.insert(cellId);
                                }
                                else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                                {
                                    for (auto tmpCell : curMacro->getCells())
                                    {
                                        int cellId = tmpCell->getCellId();
                                        //   if (timingNodes[cellId]->getOutEdges().size() < fanoutThr)
                                        extractedCellIds.insert(cellId);
                                    }
                                }
                                PUIdsInLongPaths.push_back(curPU->getId());
                            }
                        }

                        clockRegionclusters.push_back(PUIdsInLongPaths);
                        std::cout << "maxClockRegionWeight: " << maxClockRegionWeight
                                  << " totalClockRegionWeight:" << totalClockRegionWeight
                                  << " #extractedCellIds=" << extractedCellIds.size()
                                  << " #extractedPUs=" << extractedPUs.size()
                                  << " pathLength=" << timingNode->getLongestPathLength() << "\n";
                    }

                    else if (totalClockRegionWeight >= 20000)
                    {
                        for (auto tmpCellId : candidateCellIds)
                        {
                            if (timingNodes[tmpCellId]->getOutEdges().size() < fanoutThr)
                                extractedCellIds.insert(tmpCellId);
                        }
                    }
                }
            }
        }

        if (extractedCellIds.size() > timingNodes.size() * 0.2 && sizeThr > 20000)
        {
            PU2ClockRegionCenter.clear();
            PU2ClockRegionColumn.clear();

            extractedCellIds.clear();
            extractedPUs.clear();
            clockRegionclusters.clear();

            sizeThr = 2000;
            clockRegionClusterTooLarge = true;
        }
    }
    initialClustering = false;
    dumpClockRegionClusters();
    // if (!placementInfo->isDensePlacement())
    // Isolated ablation71: retain clustering; skip only column stretching.
    print_info("ABLATION71 stretchClockRegionColumns skipped");
}

void PlacementTimingOptimizer::stretchClockRegionColumns()
{
    AMF_PROFILE_FUNCTION("timing");
    auto &binGrid = placementInfo->getGlobalBinGrid();
    auto &PU2ClockRegionColumn = placementInfo->getPU2ClockRegionColumn();
    auto YX2ClockRegion = deviceInfo->getClockRegions();
    int clockRegionXNum = YX2ClockRegion[0].size();
    std::vector<int> curClockRegionX2cellNum(clockRegionXNum, 0);
    std::vector<std::vector<PlacementInfo::PlacementUnit *>> clockRegionX2PUs(
        clockRegionXNum, std::vector<PlacementInfo::PlacementUnit *>(0));
    std::vector<float> curClockRegionX2MinY(clockRegionXNum, placementInfo->getGlobalBinMaxLocY());
    std::vector<float> curClockRegionX2MaxY(clockRegionXNum, placementInfo->getGlobalBinMinLocY());

    for (auto PU : placementInfo->getPlacementUnits())
    {
        int clockRegionX, clockRegionY;
        deviceInfo->getClockRegionByLocation(PU->X(), PU->X(), clockRegionX, clockRegionY);
        curClockRegionX2cellNum[clockRegionX] += PU->getWeight();
        clockRegionX2PUs[clockRegionX].push_back(PU);

        int binIdX, binIdY;
        placementInfo->getGridXY(PU->X(), PU->Y(), binIdX, binIdY);

        if (binGrid[binIdY][binIdX]->getCells().size() > 16)
        {
            if (PU->Y() > curClockRegionX2MaxY[clockRegionX])
            {
                curClockRegionX2MaxY[clockRegionX] = PU->Y();
            }
            if (PU->Y() < curClockRegionX2MinY[clockRegionX])
            {
                curClockRegionX2MinY[clockRegionX] = PU->Y();
            }
        }
    }

    std::vector<int> newClockRegionX2cellNum = curClockRegionX2cellNum;
    for (auto pair : PU2ClockRegionColumn)
    {
        auto PU = pair.first;
        int oriClockRegionX, oriClockRegionY;
        deviceInfo->getClockRegionByLocation(PU->X(), PU->X(), oriClockRegionX, oriClockRegionY);
        newClockRegionX2cellNum[oriClockRegionX] -= PU->getWeight();
        newClockRegionX2cellNum[pair.second] += PU->getWeight();
    }

    for (unsigned int colX = 0; colX < curClockRegionX2cellNum.size(); colX++)
    {
        float stretchRatio = (float)newClockRegionX2cellNum[colX] / (float)curClockRegionX2cellNum[colX];

        float bottomLimit = placementInfo->getGlobalBinMinLocY();
        float topLimit = placementInfo->getGlobalBinMaxLocY();
        float completeH = topLimit - bottomLimit;
        float oriTopY = curClockRegionX2MaxY[colX];
        float oriBottomY = curClockRegionX2MinY[colX];
        float oriH = std::abs(oriTopY - oriBottomY);
        bool maybeCongestion =
            completeH * 0.625 > oriH && (oriBottomY < 0.075 * completeH || oriTopY > 0.925 * completeH);

        if (oriBottomY < 0.075 * completeH)
            bottomLimit = std::max(std::max(oriBottomY, bottomLimit), (float)0.05 * completeH);
        if (oriTopY > 0.925 * completeH)
            topLimit = std::min(std::min(oriTopY, topLimit), (float)0.95 * completeH);

        if (std::abs(oriTopY - oriBottomY) < 0.1)
            continue;
        if (stretchRatio > 1 || (maybeCongestion && stretchRatio > 0.95))
        {
            if (maybeCongestion)
            {
                print_warning("PlacementTimingOptimizer: " + std::to_string(colX) +
                              "th column is stretched more to avoid potential congestion");
                stretchRatio *= 1.1;
            }
            else
                stretchRatio *= 1.05;
            float newH = stretchRatio * oriH;
            float deltaH = newH - oriH;
            float newTopY = oriTopY + deltaH / 2;
            float newBottomY = oriBottomY - deltaH / 2;

            if (newBottomY < bottomLimit)
            {
                newTopY += (bottomLimit - newBottomY);
                newBottomY = bottomLimit;
            }

            if (newTopY > topLimit)
            {
                newBottomY = std::max(bottomLimit, newBottomY - (newTopY - topLimit));
                newTopY = topLimit;
            }
            newH = newTopY - newBottomY;
            stretchRatio = newH / oriH;
            for (auto PU : clockRegionX2PUs[colX])
            {
                if (!PU->isFixed())
                {
                    float fY = (PU->Y() - oriBottomY) * stretchRatio + newBottomY;
                    float fX = PU->X();
                    placementInfo->legalizeXYInArea(PU, fX, fY);
                    PU->setAnchorLocationAndForgetTheOriginalOne(fX, fY);
                }
            }
            print_warning("PlacementTimingOptimizer: " + std::to_string(colX) + "th column is stretched by " +
                          std::to_string(stretchRatio) + " oriBottomY=" + std::to_string(oriBottomY) +
                          " oriTopY=" + std::to_string(oriTopY) + " newBottomY=" + std::to_string(newBottomY) +
                          " newTopY=" + std::to_string(newTopY));
        }
    }

    placementInfo->updateElementBinGrid();
}

void PlacementTimingOptimizer::dumpClockRegionClusters()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    std::string dumpClockRegionClustersFile = JSONCfg["Dump Cluster file"] + "-clockRegion";
    if (dumpClockRegionClustersFile != "")
    {
        print_status("dumping cluster information to " + dumpClockRegionClustersFile);
        std::ofstream outfile0((dumpClockRegionClustersFile + ".tcl").c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for cluster result dumping does not exist and please check your path settings");
        for (unsigned int cluster_id = 0; cluster_id < clockRegionclusters.size(); cluster_id++)
        {
            outfile0 << "highlight -color_index " << (cluster_id) % 20 + 1 << "  [get_cells {";
            for (int id : clockRegionclusters[cluster_id])
            {
                if (auto tmpMacro =
                        dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnits()[id]))
                {
                    for (auto cell : tmpMacro->getCells())
                    {
                        outfile0 << cell->getName() << " ";
                    }
                }
                else if (auto tmpUnpacked = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(
                             placementInfo->getPlacementUnits()[id]))
                {
                    outfile0 << tmpUnpacked->getName() << " ";
                }
                else
                {
                    assert(false);
                }
            }
            outfile0 << "}]\n";
        }
        outfile0.close();
    }
}

void PlacementTimingOptimizer::propogateArrivalTime()
{
    AMF_PROFILE_FUNCTION("timing");
    assert(timingInfo);
    // auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
}

std::vector<float> &PlacementTimingOptimizer::getPUId2Slack(bool update)
{
    AMF_PROFILE_FUNCTION("timing");
    if (update)
    {
        auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
        // float clockPeriod = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph()->getClockPeriod();
        auto &cellLoc = placementInfo->getCellId2location();
        assert(cellLoc.size() == timingNodes.size());

        unsigned int highFanoutThr = 1000;

        int PUNum = placementInfo->getPlacementUnits().size();
        PUId2Slack.clear();
        PUId2Slack.resize(PUNum, 0);

#pragma omp parallel for
        for (int PUId = 0; PUId < PUNum; PUId++)
        {
            auto &curPU = placementInfo->getPlacementUnits()[PUId];

            for (auto curNet : *(curPU->getNetsSetPtr()))
            {
                if (curNet->getDriverUnits().size() == 0)
                    continue;
                if (curNet->getDriverUnits()[0] != curPU)
                    continue;

                auto designNet = curNet->getDesignNet();
                assert(designNet);
                if (designNet->checkIsPowerNet() || designNet->checkIsGlobalClock())
                    continue;

                if (curNet->getDriverUnits().size() != 1 || curNet->getUnits().size() <= 1 ||
                    curNet->getUnits().size() >= highFanoutThr)
                    continue;
                auto &PUs = curNet->getUnits();
                auto &pins = designNet->getPins();
                int pinNum = pins.size();

                assert(curNet->getUnits().size() == (unsigned int)pinNum);

                int driverPinInNet = -1;

                for (int i = 0; i < pinNum; i++)
                {
                    if (pins[i]->isOutputPort())
                    {
                        driverPinInNet = i;
                        break;
                    }
                }

                assert(driverPinInNet >= 0);

                // get the srcPin information
                auto srcCell = pins[driverPinInNet]->getCell();
                unsigned int srcCellId = srcCell->getCellId();
                auto srcNode = timingNodes[srcCellId];
                auto srcLoc = cellLoc[srcCellId];

                for (int pinBeDriven = 0; pinBeDriven < pinNum; pinBeDriven++)
                {
                    if (pinBeDriven == driverPinInNet)
                        continue;

                    // get the sinkPin information
                    auto sinkCell = pins[pinBeDriven]->getCell();
                    unsigned int sinkCellId = sinkCell->getCellId();
                    auto sinkNode = timingNodes[sinkCellId];
                    auto sinkLoc = cellLoc[sinkCellId];

                    float netDelay = getDelayByModel(sinkNode, srcNode, sinkLoc.X, sinkLoc.Y, srcLoc.X, srcLoc.Y);
                    float slack = sinkNode->getRequiredArrivalTime() - srcNode->getLatestOutputArrival() - netDelay;
                    if (slack < 0)
                    {
                        if (slack < PUId2Slack[PUId])
                            PUId2Slack[PUId] = slack;
                    }
                }
            }
        }
    }
    return PUId2Slack;
}

void PlacementTimingOptimizer::clusterCriticalPathsByPhysicalRegion(bool deferSLRSpreading)
{
    AMF_PROFILE_FUNCTION("boundary_clustering");
    placementInfo->updateElementBinGrid();
    placementInfo->clearRegionPreferences();
    conductStaticTimingAnalysis();
    BoundaryAwareClusterer physical(placementInfo,this,JSONCfg);
    physical.run(deferSLRSpreading);
    physical.audit("cluster-selected");
}

void PlacementTimingOptimizer::spreadPaperBoundarySLR()
{
    if (placementInfo->paperBoundaryClusteringEnabled() && placementInfo->paperSLRStage())
        BoundaryAwareClusterer(placementInfo, this, JSONCfg).spreadPaperSLR();
}

void PlacementTimingOptimizer::advancePaperBoundaryStage()
{
    if (placementInfo->paperBoundaryClusteringEnabled() && placementInfo->paperSLRStage())
        BoundaryAwareClusterer(placementInfo, this, JSONCfg).advancePaperStage();
}

void PlacementTimingOptimizer::auditPhysicalBoundaries(const std::string &stage)
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    if(deviceInfo->getPhysicalBoundaryModel())BoundaryAwareClusterer(placementInfo,this,JSONCfg).audit(stage);
}

void PlacementTimingOptimizer::forwardBFSToFindNodesToBeUpdated(std::vector<int> &initialNodeIds)
{
    auto &nodes = timingInfo->getSimplePlacementTimingInfo();
    incremental_forwardLevel2NodeIds.assign(
        timingInfo->getSimplePlacementTimingGraph()->getForwardlevel2NodeIds().size(), {});
    incrementalUpdatedNodes.clear();
    std::queue<int> queue;
    std::set<int> seen;
    auto add = [&](int id, bool expand) {
        if (id < 0 || id >= int(nodes.size()) || nodes[id]->getForwardLevel() < 0 || !seen.insert(id).second) return;
        incrementalNodeSnapshot.emplace(id, *nodes[id]);
        incrementalUpdatedNodes.push_back(id);
        incremental_forwardLevel2NodeIds.at(nodes[id]->getForwardLevel()).push_back(id);
        nodes[id]->setLatestInputArrival_inLastIteration(nodes[id]->getLatestInputArrival());
        if (expand) queue.push(id);
    };
    for (int id : initialNodeIds) add(id, true);
    while (!queue.empty())
    {
        int id = queue.front(); queue.pop();
        for (auto edge : nodes[id]->getOutEdges())
            add(edge->getSink()->getId(), !edge->getSink()->checkIsRegister());
    }
    // Compare exactly the same endpoints before and after the trial.
    oriMaxIncrDelay = 0;
    if (!incremental_forwardLevel2NodeIds.empty())
        for (int id : incremental_forwardLevel2NodeIds[0])
            oriMaxIncrDelay = std::max(oriMaxIncrDelay, nodes[id]->getLatestInputArrival());
}

void PlacementTimingOptimizer::backwardBFSToFindNodesToBeUpdated()
{
    auto &nodes = timingInfo->getSimplePlacementTimingInfo();
    incremental_backwardLevel2NodeIds.assign(
        timingInfo->getSimplePlacementTimingGraph()->getBackwardlevel2NodeIds().size(), {});
    std::queue<int> queue;
    std::set<int> seen;
    auto add = [&](int id, bool expand) {
        if (id < 0 || id >= int(nodes.size()) || nodes[id]->getBackwardLevel() < 0 || !seen.insert(id).second) return;
        incrementalNodeSnapshot.emplace(id, *nodes[id]);
        incremental_backwardLevel2NodeIds.at(nodes[id]->getBackwardLevel()).push_back(id);
        if (expand) queue.push(id);
    };
    for (int id : incrementalUpdatedNodes) add(id, true);
    // Changed incoming edges can affect required times even when arrival times
    // do not change. Never prune this traversal by an arrival-time tolerance.
    for (auto entry : incrementalEdgeSnapshot) add(entry.first->getSource()->getId(), true);
    while (!queue.empty())
    {
        int id = queue.front(); queue.pop();
        for (auto edge : nodes[id]->getInEdges())
            add(edge->getSource()->getId(), !edge->getSource()->checkIsRegister());
    }
}

void PlacementTimingOptimizer::incremental_propogateArrivalTime()
{
    auto &nodes = timingInfo->getSimplePlacementTimingInfo();

    for (auto j : incrementalUpdatedNodes)
    {
        auto curNode = nodes[j];
        curNode->setLatestInputArrival(0.0);
        curNode->setLatestOutputArrival(0.0);
        curNode->setSlowestPredecessorId(-1);
    }
    for (unsigned int i = 1; i < incremental_forwardLevel2NodeIds.size(); i++)
    {
        int numNodeInLayer = incremental_forwardLevel2NodeIds[i].size();
#pragma omp parallel for
        for (int j = 0; j < numNodeInLayer; j++)
        {
            auto curNodeId = incremental_forwardLevel2NodeIds[i][j];
            auto curNode = nodes[curNodeId];

            assert(curNode->getLatestInputArrival() < 0.01);

            for (auto inEdge : curNode->getInEdges())
            {
                int predId = inEdge->getSource()->getId();
                float predDelay = inEdge->getSource()->getLatestInputArrival();
                float edgeDelay = inEdge->getDelay();
                float newDelay;

                if (inEdge->getSource()->getInnerDelay() < 1.0 || inEdge->getSource()->getForwardLevel() > 0)
                {
                    if (inEdge->getSource()->getForwardLevel() == 0)
                        newDelay = edgeDelay + inEdge->getSource()->getInnerDelay();
                    else
                        newDelay = predDelay + edgeDelay + inEdge->getSource()->getInnerDelay();
                }
                else
                {
                    // Full STA propagates from level-zero launch nodes before
                    // accumulating their input arrivals. Incremental STA may
                    // retain that captured arrival outside the update cone.
                    newDelay = edgeDelay;
                }

                if (newDelay > curNode->getLatestInputArrival())
                {
                    assert(inEdge->getSource()->getForwardLevel() < curNode->getForwardLevel());
                    curNode->setLatestInputArrival(newDelay);
                    curNode->setSlowestPredecessorId(predId);
                    // curNode->setLatestOutputArrival(newDelay + curNode->getInnerDelay());
                    curNode->setLatestOutputArrival(newDelay);
                }
            }
        }
    }

    if (incremental_forwardLevel2NodeIds.empty()) return;
    int numNodeInLayer = incremental_forwardLevel2NodeIds[0].size();
#pragma omp parallel for
    for (int j = 0; j < numNodeInLayer; j++)
    {
        auto curNodeId = incremental_forwardLevel2NodeIds[0][j];
        assert(curNodeId < nodes.size());
        auto curNode = nodes[curNodeId];
        if (curNode->getDesignNode()->isVirtualCell())
            continue;

        for (auto inEdge : curNode->getInEdges())
        {
            if (inEdge->getSource())
            {
                if (inEdge->getSource()->getForwardLevel() > 0)
                {
                    int predId = inEdge->getSource()->getId();
                    float predDelay = inEdge->getSource()->getLatestInputArrival();
                    float edgeDelay = inEdge->getDelay();
                    float newDelay = predDelay + edgeDelay + inEdge->getSource()->getInnerDelay();
                    if (newDelay > curNode->getLatestInputArrival())
                    {
                        curNode->setLatestInputArrival(newDelay);
                        curNode->setSlowestPredecessorId(predId);
                    }
                }
            }
        }
    }
}

void PlacementTimingOptimizer::incremental_backPropogateRequiredArrivalTime()
{
    auto &nodes = timingInfo->getSimplePlacementTimingInfo();
    float clockPeriod = timingInfo->getSimplePlacementTimingGraph()->getClockPeriod();
    int nodeNum = nodes.size();
    for (const auto &layer : incremental_backwardLevel2NodeIds)
    for (auto j : layer)
    {
        auto curNode = nodes[j];
        curNode->setInitialRequiredArrivalTime(clockPeriod);
        curNode->setEarlestSuccessorId(-1);
        curNode->setEndpointCriticality(1000.0);
    }
    for (unsigned int i = 1; i < incremental_backwardLevel2NodeIds.size(); i++)
    {
        int numNodeInLayer = incremental_backwardLevel2NodeIds[i].size();
#pragma omp parallel for
        for (int j = 0; j < numNodeInLayer; j++)
        {
            auto curNodeId = incremental_backwardLevel2NodeIds[i][j];
            auto curNode = nodes[curNodeId];
            assert(!curNode->checkIsRegister());
            for (auto outEdge : curNode->getOutEdges())
            {
                int succId = outEdge->getSink()->getId();
                float succRequiredArrival = outEdge->getSink()->getRequiredArrivalTime();
                float edgeDelay = outEdge->getDelay();
                float newRequiredArrival = succRequiredArrival - edgeDelay - outEdge->getSink()->getInnerDelay();
                if (newRequiredArrival < curNode->getRequiredArrivalTime())
                {
                    curNode->setRequiredArrivalTime(newRequiredArrival);
                    curNode->setEarlestSuccessorId(succId);
                }
            }
        }
    }

    if (incremental_backwardLevel2NodeIds.empty()) return;
    int numNodeInLayer = incremental_backwardLevel2NodeIds[0].size();
#pragma omp parallel for
    for (int j = 0; j < numNodeInLayer; j++)
    {
        auto curNodeId = incremental_backwardLevel2NodeIds[0][j];
        auto curNode = nodes[curNodeId];
        assert(curNode->checkIsRegister());
        for (auto outEdge : curNode->getOutEdges())
        {
            int succId = outEdge->getSink()->getId();
            float succRequiredArrival = outEdge->getSink()->getRequiredArrivalTime();
            float edgeDelay = outEdge->getDelay();
            float newRequiredArrival = succRequiredArrival - edgeDelay - outEdge->getSink()->getInnerDelay();
            if (newRequiredArrival < curNode->getEndpointCriticality())
            {
                curNode->setEndpointCriticality(newRequiredArrival);
                assert(succId >= 0);
                curNode->setEarlestSuccessorId(succId);
            }
        }
    }
}

void PlacementTimingOptimizer::checkCrossRegionDegree()
{

    print_status("PlacementTimingOptimizer: check Cross Region Degree");
    auto oriCellIdsInCriticalPaths = findCriticalPaths(1, true, 5000, 50, false);
    std::set<std::pair<PlacementInfo::PlacementUnit *, PlacementInfo::PlacementUnit *>> recordPairs;
    int fourBoundaryCnt = 0;
    for (auto cellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        for (int i = cellIdsInCriticalPath.size() / 2; i < cellIdsInCriticalPath.size() - 1; i++)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellIdsInCriticalPath[i]);
            auto nextPU = placementInfo->getPlacementUnitByCellId(cellIdsInCriticalPath[i + 1]);
            std::pair<PlacementInfo::PlacementUnit *, PlacementInfo::PlacementUnit *> curPair(curPU, nextPU);
            if (recordPairs.find(curPair) == recordPairs.end())
            {
                recordPairs.insert(curPair);
                fourBoundaryCnt += countCrossIOBank(curPU->X(), nextPU->X());
            }
        }
    }
    recordPairs.clear();
    fineTuneTiming = true;
    int sixBoundaryCnt = 0;
    for (auto cellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        for (int i = cellIdsInCriticalPath.size() / 2; i < cellIdsInCriticalPath.size() - 1; i++)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellIdsInCriticalPath[i]);
            auto nextPU = placementInfo->getPlacementUnitByCellId(cellIdsInCriticalPath[i + 1]);
            std::pair<PlacementInfo::PlacementUnit *, PlacementInfo::PlacementUnit *> curPair(curPU, nextPU);
            if (recordPairs.find(curPair) == recordPairs.end())
            {
                recordPairs.insert(curPair);
                sixBoundaryCnt += countCrossIOBank(curPU->X(), nextPU->X());
            }
        }
    }
    print_info("PlacementTimingOptimizer: numRoutingCross4Boundary=" + std::to_string(fourBoundaryCnt) +
               "  numRoutingCross6Boundary=" + std::to_string(sixBoundaryCnt));
    if (sixBoundaryCnt < placementInfo->getClusterNum() * 4)
        fineTuneTiming = false;
}

void PlacementTimingOptimizer::recoverArrivalDelayFromLastIncrementalSTA()
{
    auto &nodes = timingInfo->getSimplePlacementTimingInfo();
    for (const auto &entry : incrementalNodeSnapshot) *nodes.at(entry.first) = entry.second;
    for (const auto &entry : incrementalEdgeSnapshot) entry.first->setDelay(entry.second);
    for (const auto &entry : incrementalPinSnapshot) placementInfo->getPinId2location().at(entry.first) = entry.second;
    timingInfo->getSimplePlacementTimingGraph()->invalidateReverseArrival();
    incrementalNodeSnapshot.clear(); incrementalEdgeSnapshot.clear(); incrementalPinSnapshot.clear();
}
