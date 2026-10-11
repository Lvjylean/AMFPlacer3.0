/**
 * @file PlacementTimingOptimizer.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#ifndef _PlacementTimingOptimizer
#define _PlacementTimingOptimizer

#include "DesignInfo.h"
#include "DeviceInfo.h"
#include "PlacementInfo.h"
#include "PlacementTimingInfo.h"
#include "dumpZip.h"
#include <array>
#include <assert.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <semaphore.h>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class PlacementTimingOptimizer
{
  public:
    PlacementTimingOptimizer(PlacementInfo *placementInfo, std::map<std::string, std::string> &JSONCfg);
    ~PlacementTimingOptimizer()
    {
    }

    void propogateArrivalTime();
    std::vector<int> findCriticalPath();
    std::vector<std::vector<int>> findCriticalPaths(float criticalRatio, bool checkOverlap = true,
                                                    int pathNumThr = 1000, int converThr = 30,
                                                    bool backwardSearch = true);

    std::vector<std::vector<int>> findCriticalPaths_tolerate(float criticalRatio, bool checkOverlap = true,
                                                             int pathNumThr = 1000, int converThr = 30,
                                                             bool backwardSearch = true);

    std::vector<std::vector<int>> findCriticalPaths_inv(float criticalRatio, bool checkOverlap = true,
                                                        int pathNumThr = 1000, int converThr = 30);

    void encodeNodesInCriticalPaths(float criticalRatio, bool checkOverlap = true, int pathNumThr = 1000,
                                    int converThr = 30);
    std::vector<std::vector<int>> findCriticalPaths(float criticalRatio,
                                                    std::vector<bool> &FFDirectlyDrivenButNotInOneSlot);
    float getWorstSlackOfCell(DesignInfo::DesignCell *srcCell);
    float conductStaticTimingAnalysis(bool enforeOptimisticTiming = false);
    void incrementalStaticTimingAnalysis(std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> movedPUs,
                                         bool noBackPropogation = false);
    void setPinsLocation();
    void clusterLongPathInOneClockRegion(int pathLenThr, float clusterThrRatio);
    void clusterCriticalPathsByPhysicalRegion(bool deferSLRSpreading = false);
    void spreadPaperBoundarySLR();
    void advancePaperBoundaryStage();
    void auditPhysicalBoundaries(const std::string &stage);
    void dumpClockRegionClusters();
    void stretchClockRegionColumns();

    inline void updateDelayRelatedToCell(DesignInfo::DesignCell *curCell)
    {
        auto &pinLoc = placementInfo->getPinId2location();
        auto &timingNodes = timingInfo->getSimplePlacementTimingInfo();
        for (auto edge : timingNodes[curCell->getCellId()]->getInEdges())
        {
            auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
            auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
            if (pin1Loc.X < -5 && pin1Loc.Y < -5)
                continue;
            if (pin2Loc.X < -5 && pin2Loc.Y < -5)
                continue;

            edge->setDelay(
                getDelayByModel(edge->getSink(), edge->getSource(), pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y));
        }
        for (auto edge : timingNodes[curCell->getCellId()]->getOutEdges())
        {
            auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
            auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
            if (pin1Loc.X < -5 && pin1Loc.Y < -5)
                continue;
            if (pin2Loc.X < -5 && pin2Loc.Y < -5)
                continue;

            edge->setDelay(
                getDelayByModel(edge->getSink(), edge->getSource(), pin1Loc.X, pin1Loc.Y, pin2Loc.X, pin2Loc.Y));
        }
    }

    void checkCrossRegionDegree();

    void initPois()
    {
        pois.push_back(std::exp(-lambdaPois));
        for (int i = 1; i < poisN; i++)
        {
            pois.push_back(pois[i - 1] * lambdaPois / i);
        }
        for (int i = 1; i < poisN; i++)
        {
            pois[i] = std::pow(pois[i] / pois[100], 0.25);
        }
    }

    inline float getPois(int v)
    {
        if (v > 200)
        {
            return 0;
        }
        else
        {
            return pois[v];
        }
    }

    inline float getEffectFactor()
    {
        return effectFactor;
    }

    inline void setEffectFactor(float _effectFactor)
    {
        effectFactor = _effectFactor;
    }

    inline float getDelayByModel(PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode *node1,
                                 PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode *node2, float X1,
                                 float Y1, float X2, float Y2)
    {
        float delay = getDelayByModel_conservative(X1, Y1, X2, Y2);
        if (proPrimitiveTiming && !DSPCritical && isPackingTiming)
        {
            if (node1->getDesignNode()->isDSP() && node1->getForwardLevel() == 0)
                delay += 0.5;
            else if (node2->getDesignNode()->isDSP() && node2->getForwardLevel() == 0)
                delay += 0.5;
        }
        if (proPrimitiveTiming && chainTimingEnabled && node1->getDesignNode()->isCarry() && node2->getDesignNode()->isCarry())
        {
            delay -= 0.15;
            if (delay < 0.027)
                delay = 0.027;
        }
        return delay;
    }

    inline float getDelayByModel(float X1, float Y1, float X2, float Y2)
    {
        return getDelayByModel_conservative(X1, Y1, X2, Y2);
    }

    float IOX6[6] = {7.5, 15.5, 33.5, 51.5, 69.5, 76.5};
    float IOX4[4] = {7.5, 33.5, 51.5, 76.5};

    inline bool crossIOBank(float X1, float X2)
    {
        float minX = std::min(X1, X2);
        float maxX = std::max(X1, X2);
        if (fineTuneTiming)
        {
            for (int i = 0; i < 6; i++)
                if (minX < IOX6[i] && maxX > IOX6[i])
                    return true;
        }
        else
        {
            for (int i = 0; i < 4; i++)
                if (minX < IOX4[i] && maxX > IOX4[i])
                    return true;
        }

        return false;
    }

    inline int countCrossIOBank(float X1, float X2)
    {
        float minX = std::min(X1, X2);
        float maxX = std::max(X1, X2);
        int res = 0;
        if (fineTuneTiming)
        {
            for (int i = 0; i < 6; i++)
                if (minX < IOX6[i] && maxX > IOX6[i])
                    res++;
        }
        else
        {
            for (int i = 0; i < 4; i++)
                if (minX < IOX4[i] && maxX > IOX4[i])
                    res++;
        }

        return res;
    }

    const float timingC0[10] = {95.05263521, -26.50563359, 77.42394117, 106.29195883, -14.975527};
    // Selected once per device; both delay interfaces use these same coefficients.
    const std::array<float, 10> timingC1;
    const std::array<float, 10> timingC2;

    inline float getDelayByModel_conservative(float X1, float Y1, float X2, float Y2)
    {
        int clockRegionX0, clockRegionY0;
        deviceInfo->getClockRegionByLocation(X1, Y1, clockRegionX0, clockRegionY0);
        int clockRegionX1, clockRegionY1;
        deviceInfo->getClockRegionByLocation(X2, Y2, clockRegionX1, clockRegionY1);

        float X = std::fabs(X1 - X2);
        float Y = std::fabs(Y1 - Y2);
        auto physical = deviceInfo->isPhysicalBoundaryTimingEnabled() ? deviceInfo->getPhysicalBoundaryModel() : nullptr;
        const float slrDelay = physical ? physical->penalty(X1, Y1, X2, Y2, slrBoundaryDelayNs) :
            slrBoundaryDelayNs * deviceInfo->getSLRBoundaryCount(clockRegionY0, clockRegionY1);

        const bool ioCrossing = proLegacyIOBoundary && isPackingTiming && crossIOBank(X1, X2);

        if (X * X + Y * Y < 9)
        {
            X *= 2;
            float delay = (timingC0[0] + std::pow(X, 0.3) * timingC0[1] + std::pow(Y, 0.3) * timingC0[2] +
                           std::pow(X, 0.5) * timingC0[3] + std::pow(Y, 0.5) * timingC0[4]) /
                          1000.0;
            if (ioCrossing) delay += 0.4f;
            else if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
        else if (X * X + Y * Y < 36)
        {
            X *= 2;
            float delay = (timingC1[0] + std::pow(X, 0.3) * timingC1[1] + std::pow(Y, 0.3) * timingC1[2] +
                           std::pow(X, 0.5) * timingC1[3] + std::pow(Y, 0.5) * timingC1[4]) /
                          1000.0;
            if (ioCrossing) delay += 0.4f;
            else if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
        else
        {
            X *= 2;
            float delay = (timingC2[0] + std::pow(X, 0.3) * timingC2[1] + std::pow(Y, 0.3) * timingC2[2] +
                           std::pow(X, 0.5) * timingC2[3] + std::pow(Y, 0.5) * timingC2[4]) /
                          1000.0;
            if (ioCrossing) delay += 0.4f;
            else if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
    }

    inline void pauseCounter()
    {
        enableCounter = false;
    }

    inline void startCounter()
    {
        enableCounter = true;
    }

    float getSlackThr();
    std::map<PlacementInfo::PlacementNet *, int> &getNetActualSlackPinNum()
    {
        return netActualSlackPinNum;
    }

    std::vector<float> &getPUId2Slack(bool update = false);

    inline std::vector<PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode *> &getSortedTimingNodes()
    {
        assert(timingInfo);
        auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
        timingGraph->sortedEndpointByDelay();
        return timingGraph->getSortedTimingEndpoints_ByArrival();
    }

    inline float getMaxDelay()
    {
        return maxDelay;
    }

    inline float getMaxIncrementalDelay()
    {
        return incr_maxDelay;
    }

    inline float getOriginalMaxIncrementalDelay()
    {
        return oriMaxIncrDelay;
    }

    // Restores the previous timing transaction. The caller owns the PU/packing
    // rollback; pin locations and edge/node timing caches are restored here.
    void recoverArrivalDelayFromLastIncrementalSTA();

    inline void enablePackingTiming()
    {
        isPackingTiming = true;
    }

    inline float getIncrementalTotalTimingBenefit()
    {
        return incr_totalTimingBenefit;
    }

    void enhanceFFControlSetNets();

    void forwardBFSToFindNodesToBeUpdated(std::vector<int> &initialNodeIds);
    void backwardBFSToFindNodesToBeUpdated();
    void incremental_propogateArrivalTime();

    void incremental_backPropogateRequiredArrivalTime();

    void clusterCriticalDataPath();

    void enableFineTuneTiming()
    {
        fineTuneTiming = true;
    }

    void setClockEnhanced()
    {
        clockEnhanced = true;
    }

  private:
    PlacementInfo *placementInfo = nullptr;
    PlacementTimingInfo *timingInfo = nullptr;
    DesignInfo *designInfo;
    DeviceInfo *deviceInfo;

    // settings
    std::map<std::string, std::string> &JSONCfg;

    bool verbose = false;
    float y2xRatio = 1;
    bool DSPCritical = false;
    // Heuristic ns per physical SLR seam; 0 reproduces the pre-SLR model.
    float slrBoundaryDelayNs = 1.5f;
    using TimingNode = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode;
    using TimingEdge = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingEdge;
    std::map<int, TimingNode> incrementalNodeSnapshot;
    std::map<TimingEdge *, float> incrementalEdgeSnapshot;
    std::map<int, PlacementInfo::Location> incrementalPinSnapshot;
    bool noBlockAware = false;
    bool proPrimitiveTiming = false;
    bool proLegacyIOBoundary = false;

    inline float getDis(float x1, float y1, float x2, float y2)
    {
        return std::abs(x1 - x2) + y2xRatio * std::abs(y1 - y2);
    }

    float xDelayUnit = 0.13; // ns/unit
    float yDelayUnit = 0.10; // ns/unit
    float maxDelay = 0;

    int poisN = 1000;
    float lambdaPois = 100;
    int STA_Cnt = 0;
    float effectFactor = 0;
    bool clockRegionClusterTooLarge = false;
    std::vector<float> pois;
    std::vector<std::vector<int>> clockRegionclusters;
    std::map<PlacementInfo::PlacementNet *, int> netActualSlackPinNum;
    std::vector<float> PUId2Slack;
    bool increaseLowDelayVal = false;
    bool enableCounter = true;
    bool isPackingTiming = false;
    float incr_maxDelay = 0;
    float oriMaxIncrDelay = 0;
    float incr_totalTimingBenefit = 0;

    std::vector<std::vector<int>> incremental_forwardLevel2NodeIds;
    std::vector<std::vector<int>> incremental_backwardLevel2NodeIds;
    std::vector<int> incrementalUpdatedNodes;
    std::vector<int> cellInputCodes;

    std::vector<std::vector<int>> dataPathsToBeClustered;

    bool chainTimingEnabled = false;

    bool fineTuneTiming = false;

    bool initialClustering = true;

    bool clockEnhanced = false;
};

#endif
