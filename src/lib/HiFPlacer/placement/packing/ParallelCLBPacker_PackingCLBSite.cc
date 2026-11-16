#include <sstream>
#include "../../../utils/RuntimeProfiler.h"
/**
 * @file ParallelCLBPacker_PackingCLBSite.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief  This implementation file contains APIs' implementation related to the CLBSite-level processes in the
 * ParallelCLBPacker which finally packs LUT/FF/MUX/CARRY elements into legal CLB sites in a parallel approach.
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "ParallelCLBPacker.h"
#include "../legalization/SRLSiteSharing.h"
#include "CarryLUTRules.h"
#include "MacroLocalRoutes.h"
#include "MemoryFFRules.h"
#include "RAMFFProfiles.h"
#include "../legalization/RAMSiteSharing.h"
#include <unordered_map>

void ParallelCLBPacker::PackingCLBSite::refreshPrioryQueue()
{
    AMF_PROFILE_FUNCTION("final_packing");
    std::sort(priorityQueue.begin(), priorityQueue.end(), [](PackingCLBCluster *a, PackingCLBCluster *b) -> bool {
        return (a->getScoreInSite() == b->getScoreInSite()) ? a->getHash() > b->getHash()
                                                            : (a->getScoreInSite() > b->getScoreInSite());
    });
    for (unsigned int i = 1; i < priorityQueue.size(); i++)
    {
        if (!(priorityQueue[i - 1]->getScoreInSite() >= priorityQueue[i]->getScoreInSite()))
        {
            std::cout << "priorityQueue[" << i - 1 << "]:\n" << priorityQueue[i - 1] << "\n";
            std::cout << "priorityQueue[i]:\n" << priorityQueue[i] << "\n";
            for (unsigned int j = 0; j < priorityQueue.size(); j++)
            {
                std::cout << "#" << j << ": " << priorityQueue[j]->getScoreInSite() << "\n";
            }
        }
        assert(priorityQueue[i - 1]->getScoreInSite() >= priorityQueue[i]->getScoreInSite());
    }
}

void ParallelCLBPacker::PackingCLBSite::removeInvalidClustersFromPQ()
{
    AMF_PROFILE_FUNCTION("final_packing");
    int validCnt = 0;
    for (unsigned int i = 0; i < priorityQueue.size(); i++)
    {
        if (priorityQueue[i]->areAllPUsValidForThisSite(PUId2PackingCLBSite, this))
        {
            priorityQueue[validCnt] = priorityQueue[i];
            validCnt++;
        }
        else
        {
            delete priorityQueue[i];
            priorityQueue[i] = nullptr;
        }
    }
    if (determinedClusterInSite)
        assert(determinedClusterInSite->areAllPUsValidForThisSite(PUId2PackingCLBSite, this));
    priorityQueue.resize(validCnt);
}

void ParallelCLBPacker::PackingCLBSite::removeClustersIncompatibleWithDetClusterFromPQ()
{
    AMF_PROFILE_FUNCTION("final_packing");
    if (determinedClusterInSite)
    {
        int validCnt = 0;
        for (unsigned int i = 0; i < priorityQueue.size(); i++)
        {
            bool changed = false;
            for (auto tmpPU : determinedClusterInSite->getPUs())
            {
                if (!priorityQueue[i]->contains(tmpPU))
                {
                    if (!priorityQueue[i]->addPU(tmpPU, true))
                    {
                        delete priorityQueue[i];
                        priorityQueue[i] = nullptr;
                        break;
                    }
                    else
                    {
                        changed = true;
                    }
                }
            }
            if (priorityQueue[i])
            {
                if (changed)
                {
                    priorityQueue[i]->refreshId();
                    priorityQueue[i]->updateScoreInSite();
                }
                priorityQueue[validCnt] = priorityQueue[i];
                validCnt++;
            }
        }
        priorityQueue.resize(validCnt);
    }
}

void ParallelCLBPacker::PackingCLBSite::removeInvalidPUsFromNeighborPUs()
{
    AMF_PROFILE_FUNCTION("final_packing");
    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> newNbr;
    newNbr.clear();
    for (auto tmpPU : neighborPUs)
    {
        bool PUValid = packingDieAllows(tmpPU);
        const auto &candidateOwners = getCandidateOwners();
        if (candidateOwners[tmpPU->getId()])
        {
            if (candidateOwners[tmpPU->getId()] != this)
            {
                PUValid = false;
            }
        }
        if (PUValid)
        {
            bool detCompatible = true;
            if (determinedClusterInSite)
            {
                if (!determinedClusterInSite->contains(tmpPU))
                {
                    PackingCLBCluster *testDetCluster = new PackingCLBCluster(determinedClusterInSite);
                    detCompatible = (testDetCluster->addPU(tmpPU, true));
                    delete testDetCluster;
                }
            }
            if (detCompatible)
                newNbr.insert(tmpPU);
        }
    }
    neighborPUs = newNbr;
}

void ParallelCLBPacker::PackingCLBSite::updateConsistentPUsInTop()
{
    AMF_PROFILE_FUNCTION("final_packing");
    if (priorityQueue.size()) // if there are possible condidates
    {
        PackingCLBCluster *topCluster = priorityQueue[0];
        std::vector<PlacementInfo::PlacementUnit *> countedPUs;
        countedPUs.clear();
        for (auto pair : PU2TopCnt)
            countedPUs.push_back(pair.first);

        std::vector<PlacementInfo::PlacementUnit *> PUsToAdd;
        PUsToAdd.clear();
        for (auto PU : countedPUs)
        {
            if (topCluster->contains(PU) && PUId2PackingCLBSite[PU->getId()] == this)
            {
                assert(PU2TopCnt.find(PU) != PU2TopCnt.end());
                PU2TopCnt[PU]++;
                if (PU2TopCnt[PU] >= unchangedIterationThr)
                {
                    if (determinedClusterInSite) // if there is determinedClusterInSite, only add PUs not in in
                    {
                        if (!determinedClusterInSite->contains(PU))
                        {
                            PUsToAdd.push_back(PU);
                        }
                    }
                    else // if there is no determinedClusterInSite yet, all PUs meeting requirement can add
                    {
                        PUsToAdd.push_back(PU);
                    }
                }
            }
            else
            {
                PU2TopCnt.erase(PU); // erase the records of the PUs not in top
            }
        }

        // add the PUs into record if they are not recorded yet
        for (auto PU : topCluster->getPUs())
        {
            if (PU2TopCnt.find(PU) == PU2TopCnt.end())
            {
                PU2TopCnt[PU] = 1;
            }
        }

        if (PUsToAdd.size()) // some new consistent PUs found in top
        {

            // remove candidates in PQ which is not compatible with added PUs
            int validCnt = 0;
            for (unsigned int i = 0; i < priorityQueue.size(); i++)
            {
                bool changed = false;
                for (auto tmpPU : PUsToAdd)
                {
                    if (!priorityQueue[i]->contains(tmpPU))
                    {
                        if (!priorityQueue[i]->addPU(tmpPU, true))
                        {
                            delete priorityQueue[i];
                            priorityQueue[i] = nullptr;
                            break;
                        }
                        else
                        {
                            changed = true;
                        }
                    }
                }
                if (priorityQueue[i])
                {
                    if (changed)
                    {
                        priorityQueue[i]->refreshId();
                        priorityQueue[i]->updateScoreInSite();
                    }
                    priorityQueue[validCnt] = priorityQueue[i];
                    validCnt++;
                }
            }
            priorityQueue.resize(validCnt);

            if (!determinedClusterInSite) // if there is determinedClusterInSite
            {
                delete determinedClusterInSite;
                // priorityQueue.push_back(determinedClusterInSite);
            }

            determinedClusterInSite = new PackingCLBCluster(topCluster);

            for (auto countedPair : PU2TopCnt)
            {
                if (countedPair.second < unchangedIterationThr ||
                    !placementInfo->checkClockColumnLegalization(countedPair.first, CLBSite))
                {
                    determinedClusterInSite->removePUToConstructDetCluster(countedPair.first);
                }
                else
                {
                    placementInfo->addPUIntoClockColumn(countedPair.first, CLBSite);
                }
            }

            determinedClusterInSite->clusterHash();
            determinedClusterInSite->refreshId();
            determinedClusterInSite->updateScoreInSite();
            detScore = determinedClusterInSite->baseScoreInSite();
        }
    }
}

void ParallelCLBPacker::PackingCLBSite::findNewClustersWithNeighborPUs()
{
    AMF_PROFILE_FUNCTION("final_packing");

    std::set<int> hashIdSet;
    hashIdSet.clear();
    for (auto tmpSeedCluster : seedClusters)
    {
        hashIdSet.insert(tmpSeedCluster->getHash());
    }
    for (auto tmpSeedCluster : seedClusters)
    {
        PackingCLBCluster::ScoreExtensionState scoreState;
        bool scoreStateReady = false;
        for (auto tmpPU : neighborPUs)
        {
            // if (hashIdSet.find(tmpSeedCluster->clusterHashWithAdditionalPU(tmpPU)) != hashIdSet.end())
            //     continue;
            PackingCLBCluster *tmpCluster = new PackingCLBCluster(tmpSeedCluster);

            if (tmpCluster->addPU(tmpPU))
            {
                tmpCluster->refreshId();
                tmpCluster->clusterHash();
                if (hashIdSet.find(tmpCluster->getHash()) != hashIdSet.end())
                {
                    delete tmpCluster;
                    tmpCluster = nullptr;
                }
                else
                {
                    // Score only candidates surviving the existing membership-hash check.
                    if (!scoreStateReady)
                    {
                        tmpSeedCluster->prepareScoreExtension(scoreState);
                        scoreStateReady = true;
                    }
                    tmpCluster->incrementalUpdateScoreInSite(tmpPU, scoreState);
                    hashIdSet.insert(tmpCluster->getHash());
                    priorityQueue.push_back(tmpCluster);
                }
            }
            else
            {
                // if (tmpSeedCluster->getPUs().size() == 0 && !isCarrySite && !isLUTRAMSite &&
                //     (tmpSeedCluster->isPUTypeCompatibleWithSiteType(tmpPU)))
                // {
                //     PackingCLBCluster *tmpCluster = new PackingCLBCluster(tmpSeedCluster);
                //     tmpCluster->addPUFailReason(tmpPU);
                //     delete tmpCluster;
                //     std::cout.flush();
                //     assert(false);
                // }
                // assert(tmpCluster->getPUs().size() || isCarrySite || isLUTRAMSite ||
                //        !(tmpSeedCluster->isPUTypeCompatibleWithSiteType(tmpPU)));
                delete tmpCluster;
                tmpCluster = nullptr;
            }
        }
    }
}

std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> *
ParallelCLBPacker::PackingCLBSite::findNeiborPUsFromBinGrid(
    DesignInfo::DesignCellType curCellType, float targetX, float targetY, float displacementLowerbound,
    float displacementUpperbound, int PUNumThreshold, const std::vector<PackingCLBSite *> &PUId2PackingCLBSite,
    float y2xRatio, std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> *res, bool clockRegionAware)
{
    AMF_PROFILE_FUNCTION("final_packing");
    const auto &candidateOwners = getCandidateOwners();
    assert(displacementLowerbound < displacementUpperbound);
    // please note that the input DesignCell is only used to find the corresponding binGrid for site search.
    if (!res)
    {
        std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> *res =
            new std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare>();
        res->clear();
    }

    int binIdX, binIdY, clockRegionX, clockRegionY;
    placementInfo->getGridXY(targetX, targetY, binIdX, binIdY);
    placementInfo->getDeviceInfo()->getClockRegionByLocation(targetX, targetY, clockRegionX, clockRegionY);

    auto sharedTypeIds = placementInfo->getPotentialBELTypeIDs(curCellType);

    for (auto sharedTypeId : sharedTypeIds)
    {
        std::vector<std::vector<PlacementInfo::PlacementBinInfo *>> &curBinGrid =
            placementInfo->getBinGrid(sharedTypeId);
        assert(binIdY >= 0);
        assert((unsigned int)binIdY < curBinGrid.size());
        assert(binIdX >= 0);
        assert((unsigned int)binIdX < curBinGrid[binIdY].size());

        std::queue<std::pair<int, int>> binXYqueue;
        std::set<std::pair<int, int>> reachedBinXYs;
        binXYqueue.emplace(binIdX, binIdY);
        reachedBinXYs.emplace(binIdX, binIdY);

        while (binXYqueue.size() > 0)
        {
            std::pair<int, int> curXY = binXYqueue.front();
            binXYqueue.pop();
            int curbinIdX = curXY.first, curbinIdY = curXY.second;

            PlacementInfo::PlacementBinInfo *curBin = curBinGrid[curbinIdY][curbinIdX];
            float bin2TargetXYDistance = curBin->getManhattanDistanceTo(targetX, targetY, y2xRatio);
            if (bin2TargetXYDistance > displacementUpperbound)
                continue;
            for (auto tmpCell : curBin->getCells())
            {
                if (tmpCell->isLUT() || tmpCell->isFF())
                {
                    auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);

                    if (!packingDieAllows(tmpPU)) continue;
                    if (tmpPU->isFixed() || tmpPU->isPacked())
                        continue;
                    if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                    {
                        if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_CARRY ||
                            tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MCLB)
                        {
                            continue;
                        }
                    }
                    if (!candidateOwners[tmpPU->getId()])
                    {
                        float tmpPUDis = fabs(targetX - tmpPU->X()) + y2xRatio * fabs(targetY - tmpPU->Y());
                        if (tmpPUDis > displacementLowerbound && tmpPUDis <= displacementUpperbound)
                        {
                            int PUClockRegionX, PUClockRegionY;
                            placementInfo->getDeviceInfo()->getClockRegionByLocation(tmpPU->X(), targetY,
                                                                                     PUClockRegionX, PUClockRegionY);
                            if (PUClockRegionX == clockRegionX || placementInfo->boundaryClusteringEnabled())
                                res->insert(tmpPU);
                        }
                    }
                    else
                    {
                        if (candidateOwners[tmpPU->getId()] == this)
                        {
                            float tmpPUDis = fabs(targetX - tmpPU->X()) + y2xRatio * fabs(targetY - tmpPU->Y());
                            if (tmpPUDis > displacementLowerbound && tmpPUDis <= displacementUpperbound)
                            {
                                int PUClockRegionX, PUClockRegionY;
                                placementInfo->getDeviceInfo()->getClockRegionByLocation(
                                    tmpPU->X(), targetY, PUClockRegionX, PUClockRegionY);
                                if (PUClockRegionX == clockRegionX || placementInfo->boundaryClusteringEnabled())
                                    res->insert(tmpPU);
                            }
                        }
                    }
                }
            }

            // if (res->size() < (unsigned int)PUNumThreshold)
            {
                for (int nextY = curbinIdY - 1; nextY <= curbinIdY + 1; nextY++)
                {
                    for (int nextX = curbinIdX - 1; nextX <= curbinIdX + 1; nextX++)
                    {
                        if (!(nextY >= 0))
                            continue;
                        if (!((unsigned int)nextY < curBinGrid.size()))
                            continue;
                        if (!(nextX >= 0))
                            continue;
                        if (!((unsigned int)nextX < curBinGrid[binIdY].size()))
                            continue;
                        PlacementInfo::PlacementBinInfo *nextBin = curBinGrid[nextY][nextX];
                        float nextBin2TargetXYDistance = nextBin->getManhattanDistanceTo(targetX, targetY, y2xRatio);
                        if (nextBin2TargetXYDistance > displacementUpperbound)
                            continue;
                        std::pair<int, int> nextXY(nextX, nextY);
                        if (reachedBinXYs.find(nextXY) == reachedBinXYs.end())
                        {
                            reachedBinXYs.insert(nextXY);
                            binXYqueue.push(nextXY);
                        }
                    }
                }
            }
        }
    }

    return res;
}

// Replay spatial query results only. Cluster compatibility, score and auction
// are deliberately evaluated by the original callers after this merge.
void ParallelCLBPacker::PackingCLBSite::refillNeighbors(float radius)
{
    const bool atMax = radius == maxD;
    refillAction = atMax ? RefillAction::Scan : RefillAction::None;
    const bool cacheable = atMax && currentRefillGeneration != 0;
    if (cacheable && savedRefillGeneration == currentRefillGeneration)
    {
        const auto &owners = getCandidateOwners();
        const auto &units = placementInfo->getPlacementUnits();
        for (unsigned i = 0; i < savedRefillSize; ++i)
        {
            auto *unit = units[savedRefillIds[i]];
            auto *owner = owners[unit->getId()];
            if (!unit->isFixed() && !unit->isPacked() && (!owner || owner == this))
                neighborPUs.insert(unit);
        }
        refillAction = RefillAction::Reuse;
        return;
    }
    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> raw;
    auto &destination = cacheable ? raw : neighborPUs;
    findNeiborPUsFromBinGrid(DesignInfo::CellType_LUT6, CLBSite->X(), CLBSite->Y(), 0, radius, numNeighbor,
                             PUId2PackingCLBSite, y2xRatio, &destination);
    findNeiborPUsFromBinGrid(DesignInfo::CellType_FDCE, CLBSite->X(), CLBSite->Y(), 0, radius, numNeighbor,
                             PUId2PackingCLBSite, y2xRatio, &destination);
    if (!cacheable) return;
    neighborPUs.insert(raw.begin(), raw.end());
    if (raw.size() > savedRefillIds.size() || raw.size() > numNeighbor)
    {
        savedRefillGeneration = 0;
        refillAction = RefillAction::Oversize;
        return;
    }
    savedRefillSize = 0;
    for (auto *unit : raw) savedRefillIds[savedRefillSize++] = unit->getId();
    savedRefillGeneration = currentRefillGeneration;
}

// Node-centric DL algorithm flow at each computation node
void ParallelCLBPacker::PackingCLBSite::updateStep(bool initial, bool debug)
{
    AMF_PROFILE_FUNCTION("final_packing");
    if (debug)
        setDebug();
    removeInvalidClustersFromPQ();
    removeClustersIncompatibleWithDetClusterFromPQ();
    removeInvalidPUsFromNeighborPUs();
    updateConsistentPUsInTop(); //  the PQ top might be kept updated but some of its PUs might be consistent

    seedClusters = priorityQueue;
    if (seedClusters.size() == 0)
    {
        if (determinedClusterInSite)
        {
            seedClusters.push_back(new PackingCLBCluster(determinedClusterInSite));
        }
        else
        {
            seedClusters.push_back(new PackingCLBCluster(this));
        }
        seedClusters[0]->clusterHash();
    }

    // Temporary ownership can remove an inner neighbor which becomes free later.
    // Revisit the whole bounded neighborhood when refilling, including at maxD.
    // Keep the existing refill threshold, radius growth and ownership filters.
    refillAction = RefillAction::None;
    if (neighborPUs.size() < numNeighbor)
    {
        if (!initial) curD = std::min(curD + deltaD, maxD);
        refillNeighbors(curD);
    }

    if (hasSRLRowOutputs()) addNearbySRLConsumers();
    findNewClustersWithNeighborPUs();

    refreshPrioryQueue();

    // if (CLBSite->getName() == "SLICE_X38Y213")
    // {
    //     std::ofstream debugFile;
    //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
    //     assert(debugFile.is_open() && debugFile.good() &&
    //            "The path for placement Tcl dumping does not exist and please check your path settings");

    //     debugFile << "####################################SLICE_X38Y213######################################\n";
    //     for (unsigned int i = 0; i < priorityQueue.size(); i++)
    //     {
    //         debugFile << priorityQueue[i] << "\n";
    //     }
    //     debugFile.close();
    // }
    // if (CLBSite->getName() == "SLICE_X38Y215")
    // {
    //     std::ofstream debugFile;
    //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
    //     assert(debugFile.is_open() && debugFile.good() &&
    //            "The path for placement Tcl dumping does not exist and please check your path settings");

    //     debugFile << "####################################SLICE_X38Y215######################################\n";
    //     for (unsigned int i = 0; i < priorityQueue.size(); i++)
    //     {
    //         debugFile << priorityQueue[i] << "\n";
    //     }
    //     debugFile.close();
    // }
    if (priorityQueue.size() > PQSize)
    {
        int finalPQSize = PQSize;
        float scoreThreshold = priorityQueue[PQSize - 1]->getScoreInSite() * 0.99;
        bool extendable = true;
        for (unsigned int i = PQSize; i < priorityQueue.size(); i++)
        {
            if (extendable && priorityQueue[i]->getScoreInSite() > scoreThreshold)
            {
                finalPQSize = i + 1;
                if (finalPQSize > 1.5 * PQSize)
                {
                    extendable = false;
                }
            }
            else
            {
                extendable = false;
                assert(priorityQueue[i]);
                delete priorityQueue[i];
                priorityQueue[i] = nullptr;
            }
        }
        priorityQueue.resize(finalPQSize);
    }
}

bool isLUT6(DesignInfo::DesignCell *cell)
{
    if (!cell)
        return false;
    return (cell->getOriCellType() == DesignInfo::CellType_LUT6) ||
           (cell->getOriCellType() == DesignInfo::CellType_LUT6_2);
}

void ParallelCLBPacker::PackingCLBSite::finalMapToSlotsForCarrySite()
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(checkIsPrePackedSite() && checkIsCarrySite());
    assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() <= 8);

    for (int i = 0; i < 24; i++)
        finalMapToSlotsForCarrySite(i);

    for (int targetFFSetId = 0; targetFFSetId < 4; targetFFSetId++)
    {
        if (determinedClusterInSite->evictFFsFromCarryHalfCLB(targetFFSetId))
        {
            for (int i = 0; i < 24; i++)
                finalMapToSlotsForCarrySite(i);
            determinedClusterInSite->recoverFFControlSets();
        }
    }

    requireLegalSlotMapping("complete-site");
    slotMapping = best_SlotMapping;
    mappedCells = best_mappedCells;
    mappedLUTs = best_mappedLUTs;
    mappedFFs = best_mappedFFs;
    moveLUTToLUT6Slot();
}

void ParallelCLBPacker::PackingCLBSite::finalMapToSlotsForCarrySite(int FFControlSetOrderId)
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(isCarrySite);

    int FFSwapOption[24][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
                               {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
                               {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
                               {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};

    slotMapping = SiteBELMapping(ffQBlockedMask);
    mappedCells.clear();
    mappedLUTs.clear();
    mappedFFs.clear();
    mapCarryRelatedCellsToSlots(CARRYChain, CARRYChainSiteOffset);
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> FF2LUT;
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> LUT2FF;
    auto singleLUTs = determinedClusterInSite->getSortedSingleLUTs();
    auto pairedLUTs = determinedClusterInSite->getSortedPairedLUTs();

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            int halfCLBId = i * 2 + j;
            auto &CSFF = determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];
            int anotherHalfCLBId = i * 2 + (1 - j);
            auto &anotherCSFF =
                determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]];
            if (!compatibleInOneHalfCLB(FFSwapOption[FFControlSetOrderId][halfCLBId],
                                        FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]))
            {
                return;
            }
            if (halfCLBId != FFSwapOption[FFControlSetOrderId][halfCLBId])
            {
                for (auto FF : CSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasCARRY())
                    {
                        return;
                    }
                }
            }
            if (anotherHalfCLBId != FFSwapOption[FFControlSetOrderId][anotherHalfCLBId])
            {
                for (auto FF : anotherCSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasCARRY())
                    {
                        return;
                    }
                }
            }
        }
    }

    // mapped FFs
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (!slotMapping.FFs[i][j][k])
                {
                    int halfCLBId = i * 2 + j;
                    auto &CSFF =
                        determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];

                    for (auto tmpFF : CSFF.getFFs())
                    {
                        if (mappedFFs.find(tmpFF) == mappedFFs.end())
                        {
                            mappedFFs.insert(tmpFF);
                            mappedCells.insert(tmpFF);
                            slotMapping.FFs[i][j][k] = tmpFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            PlacementInfo::PlacementMacro *pairMacro =
                dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnitByCell(curFF));
            if (pairMacro)
            {
                if (pairMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
                {
                    assert(pairMacro->getCells().size() == 2);
                    assert(pairMacro->getCells()[0]->isLUT());
                    assert(pairMacro->getCells()[1]->isFF());
                    FF2LUT[pairMacro->getCells()[1]] = pairMacro->getCells()[0];
                    LUT2FF[pairMacro->getCells()[0]] = pairMacro->getCells()[1];
                }
            }
        }
    }

    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            if (FF2LUT.find(curFF) == FF2LUT.end())
            {
                for (auto pair : pairedLUTs)
                {
                    if (isDirectConnectLUTFF(pair.first, curFF))
                    {
                        FF2LUT[curFF] = pair.first;
                        LUT2FF[pair.first] = curFF;
                        break;
                    }
                    if (isDirectConnectLUTFF(pair.second, curFF))
                    {
                        FF2LUT[curFF] = pair.second;
                        LUT2FF[pair.second] = curFF;
                        break;
                    }
                }
                if (FF2LUT.find(curFF) == FF2LUT.end())
                {
                    for (auto curLUT : singleLUTs)
                    {
                        if (isDirectConnectLUTFF(curLUT, curFF))
                        {
                            FF2LUT[curFF] = curLUT;
                            LUT2FF[curLUT] = curFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    assert(fixedPairedLUTs.size() + determinedClusterInSite->getSingleLUTs().size() +
               determinedClusterInSite->getPairedLUTs().size() <=
           8);
    // map LUT connected to carry and their paired LUT
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (!slotMapping.LUTs[i][j][k])
                {
                    if (slotMapping.LUTs[i][1 - j][k])
                    {
                        if (conflictLUTsContain(slotMapping.LUTs[i][1 - j][k]))
                        {
                            for (auto pair : pairedLUTs)
                            {
                                if (pair.first == slotMapping.LUTs[i][1 - j][k])
                                {
                                    slotMapping.LUTs[i][1 - j][k] = pair.first;
                                    slotMapping.LUTs[i][j][k] = pair.second;
                                    mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                    mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                    mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                    mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                    break;
                                }
                                if (pair.second == slotMapping.LUTs[i][1 - j][k])
                                {
                                    slotMapping.LUTs[i][1 - j][k] = pair.second;
                                    slotMapping.LUTs[i][j][k] = pair.first;
                                    mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                    mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                    mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                    mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // mapped paired LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto pair : pairedLUTs)
                {
                    if (mappedLUTs.find(pair.first) == mappedLUTs.end())
                    {
                        if (mappedLUTs.find(pair.second) != mappedLUTs.end())
                        {
                            for (int i = 0; i < 2; i++)
                            {
                                for (int j = 0; j < 2; j++)
                                {
                                    for (int k = 0; k < 4; k++)
                                    {
                                        if (slotMapping.LUTs[i][j][k])
                                        {
                                            std::cout << "i,j,k:" << i << "," << j << "," << k << "   "
                                                      << slotMapping.LUTs[i][j][k] << "\n";
                                        }
                                    }
                                }
                            }
                            determinedClusterInSite->printMyself();
                            std::cout.flush();
                        }
                        assert(mappedLUTs.find(pair.second) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = pair.first;
                        slotMapping.LUTs[i][1][k] = pair.second;
                        mappedLUTs.insert(slotMapping.LUTs[i][0][k]);
                        mappedLUTs.insert(slotMapping.LUTs[i][1][k]);
                        mappedCells.insert(slotMapping.LUTs[i][0][k]);
                        mappedCells.insert(slotMapping.LUTs[i][1][k]);
                        break;
                    }
                }
            }
        }
    }

    // mapped single LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto tmpLUT : singleLUTs)
                {
                    if (mappedLUTs.find(tmpLUT) == mappedLUTs.end())
                    {
                        assert(mappedLUTs.find(tmpLUT) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = tmpLUT;
                        mappedLUTs.insert(tmpLUT);
                        mappedCells.insert(tmpLUT);
                        break;
                    }
                }
            }
        }
    }

    assert(fixedPairedLUTs.size() + determinedClusterInSite->getSingleLUTs().size() +
               determinedClusterInSite->getPairedLUTs().size() <=
           8);

    int LUTSwapOptions[4][4] = {{0, 1, 0, 1}, {1, 0, 0, 1}, {1, 0, 1, 0}, {0, 1, 1, 0}};

    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int k0 = 0; k0 < 4; k0++)
        {
            for (int i1 = 0; i1 < 2; i1++)
            {
                for (int k1 = 0; k1 < 4; k1++)
                {
                    if (i0 == i1 && k0 == k1)
                    {
                        continue;
                    }
                    if ((isCarryMacro(slotMapping.LUTs[i0][0][k0]) || isCarryMacro(slotMapping.FFs[i0][0][k0]) ||
                         isCarryMacro(slotMapping.LUTs[i0][1][k0]) || isCarryMacro(slotMapping.FFs[i0][1][k0]) ||
                         isCarryMacro(slotMapping.LUTs[i1][0][k1]) || isCarryMacro(slotMapping.FFs[i1][0][k1]) ||
                         isCarryMacro(slotMapping.LUTs[i1][1][k1]) || isCarryMacro(slotMapping.FFs[i1][1][k1])))
                        continue;
                    float oriDirectInternalRoute_slack =
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][0][k0]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][1][k0]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][0][k1], slotMapping.FFs[i1][0][k1]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][1][k1], slotMapping.FFs[i1][1][k1]);

                    float optDirectInternalRoute_slack = -100000000;
                    int optimalOption = -1;
                    for (int optionId = 0; optionId < 4; optionId++)
                    {
                        // switch locations
                        float newDirectInternalRoute_slack =
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][LUTSwapOptions[optionId][0]][k1],
                                                          slotMapping.FFs[i0][0][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1],
                                                          slotMapping.FFs[i0][1][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][LUTSwapOptions[optionId][2]][k0],
                                                          slotMapping.FFs[i1][0][k1]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][LUTSwapOptions[optionId][3]][k0],
                                                          slotMapping.FFs[i1][1][k1]);
                        if (isLUT6(slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1]) ||
                            isLUT6(slotMapping
                                       .LUTs[i0][LUTSwapOptions[optionId][3]][k0])) // illegal to put LUT6 at LUT5 slot
                            continue;
                        if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                        {
                            optimalOption = optionId;
                            optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                        }
                    }

                    if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
                    {
                        DesignInfo::DesignCell *tmpLUT0 = slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][0]][k1];
                        DesignInfo::DesignCell *tmpLUT1 = slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][1]][k1];
                        DesignInfo::DesignCell *tmpLUT2 = slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][2]][k0];
                        DesignInfo::DesignCell *tmpLUT3 = slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][3]][k0];
                        slotMapping.LUTs[i0][0][k0] = tmpLUT0;
                        assert(!isLUT6(tmpLUT1));
                        slotMapping.LUTs[i0][1][k0] = tmpLUT1;
                        slotMapping.LUTs[i1][0][k1] = tmpLUT2;
                        assert(!isLUT6(tmpLUT3));
                        slotMapping.LUTs[i1][1][k1] = tmpLUT3;
                        continue;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            // if ((isCarryMacro(slotMapping.FFs[i][j][0]) || isCarryMacro(slotMapping.FFs[i][j][1]) ||
            //      isCarryMacro(slotMapping.FFs[i][j][2]) || isCarryMacro(slotMapping.FFs[i][j][3])))
            //     continue;
            float oriDirectInternalRoute_slack =
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0], slotMapping.FFs[i][j][0]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1], slotMapping.FFs[i][j][1]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2], slotMapping.FFs[i][j][2]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3], slotMapping.FFs[i][j][3]);
            float optDirectInternalRoute_slack = -100000000;
            int optimalOption = -1;
            for (int optionId = 0; optionId < 24; optionId++)
            {
                bool moveFFofCarry = false;
                for (int checkFFId = 0; checkFFId < 4; checkFFId++)
                {
                    if (FFSwapOption[optionId][checkFFId] != checkFFId)
                    {
                        if (isCarryMacro(slotMapping.FFs[i][j][checkFFId]))
                        {
                            moveFFofCarry = true;
                        }
                    }
                }
                if (moveFFofCarry)
                    continue;

                float newDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][0]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][1]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][2]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][3]]);
                if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                {
                    optimalOption = optionId;
                    optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                }
            }
            if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
            {
                DesignInfo::DesignCell *tmpFF0 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][0]];
                DesignInfo::DesignCell *tmpFF1 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][1]];
                DesignInfo::DesignCell *tmpFF2 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][2]];
                DesignInfo::DesignCell *tmpFF3 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][3]];
                slotMapping.FFs[i][j][0] = tmpFF0;
                slotMapping.FFs[i][j][1] = tmpFF1;
                slotMapping.FFs[i][j][2] = tmpFF2;
                slotMapping.FFs[i][j][3] = tmpFF3;
                continue;
            }
        }
    }

    if (determinedClusterInSite)
    {

        unsigned int FFCnt = 0;
        for (auto &CSFF : determinedClusterInSite->getFFControlSets())
            FFCnt += CSFF.getFFs().size();

        if (FFCnt != mappedFFs.size())
        {
            std::cout << "FFCnt: " << FFCnt << " mappedFFs.size():" << mappedFFs.size() << "\n";
            determinedClusterInSite->printMyself();
            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        std::cout << "i,k,j:" << i << "," << k << "," << j << ":\n";
                        if (slotMapping.LUTs[i][j][k])
                            std::cout << slotMapping.LUTs[i][j][k] << "\n";
                        if (slotMapping.FFs[i][j][k])
                            std::cout << slotMapping.FFs[i][j][k] << "\n";
                    }
                }
            }
            assert(FFCnt == mappedFFs.size());
        }
    }

    std::map<DesignInfo::DesignCell *, std::array<int, 3>> cell2slot;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (slotMapping.FFs[i][j][k])
                {
                    cell2slot[slotMapping.FFs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
                if (slotMapping.LUTs[i][j][k])
                {
                    cell2slot[slotMapping.LUTs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
            }
        }
    }
    // Move FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                if (LUT2FF.find(slotMapping.LUTs[i0][j0][k0]) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[slotMapping.LUTs[i0][j0][k0]];
                    if (slotMapping.FFs[i0][j0][k0] == nullptr)
                    {
                        int halfCLBId0 = i0 * 2 + j0;
                        auto &CSFF0 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId0]];
                        int i1 = cell2slot[targetFF].at(0);
                        int j1 = cell2slot[targetFF].at(1);
                        int k1 = cell2slot[targetFF].at(2);
                        int halfCLBId1 = i1 * 2 + j1;
                        auto &CSFF1 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId1]];
                        if (CSFF0.compatibleWith(CSFF1.getCSId()) &&
                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, i0, j0))
                        {
                            bool compatible = true;
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][1 - j0][kk])
                                {
                                    if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
                                    {
                                        if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                            !DesignInfo::FFSRCompatible(
                                                slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getFFType(),
                                                targetFF->getControlSetInfo()->getFFType()))
                                        {
                                            compatible = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!compatible)
                                continue;

                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                            slotMapping.FFs[i0][j0][k0] = targetFF;
                            slotMapping.FFs[i1][j1][k1] = nullptr;
                            cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
                        }
                    }
                }
            }
        }
    }

    // Move LUT-FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                auto targetLUT = slotMapping.LUTs[i0][j0][k0];
                if (slotMapping.LUTs[i0][1 - j0][k0])
                    continue;
                if (LUT2FF.find(targetLUT) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[targetLUT];
                    int i1 = cell2slot[targetFF].at(0);
                    int j1 = cell2slot[targetFF].at(1);
                    int k1 = cell2slot[targetFF].at(2);
                    int halfCLBId1 = i1 * 2 + j1;

                    if (i0 != i1 || j0 != j1 || k0 != k1)
                    {
                        bool optimized = false;
                        for (int ii = 0; ii < 2 && !optimized; ii++)
                        {
                            for (int jj = 0; jj < 2 && !optimized; jj++)
                            {
                                bool compatible = true;
                                if (targetLUT->isLUT6() && jj == 1)
                                    continue;
                                int halfCLBId0 = ii * 2 + jj;
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo())
                                        {
                                            if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo()->getId() !=
                                                targetFF->getControlSetInfo()->getId())
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][1 - jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo())
                                        {
                                            if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                                !DesignInfo::FFSRCompatible(
                                                    slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo()->getFFType(),
                                                    targetFF->getControlSetInfo()->getFFType()))
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }

                                if (compatible)
                                {
                                    for (int kk = 0; kk < 4; kk++)
                                    {
                                        if (!slotMapping.LUTs[ii][jj][kk] && !slotMapping.FFs[ii][jj][kk] &&
                                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, ii, jj))
                                        {
                                            // if (slotMapping.LUTs[ii][1 - jj][kk])
                                            //     continue;
                                            if (targetLUT->isLUT6())
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                    continue;
                                            }
                                            else
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk] &&
                                                    slotMapping.LUTs[ii][1 - jj][kk]->isLUT6())
                                                    continue;
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                {
                                                    int pairPinNum =
                                                        getPairPinNum(targetLUT, slotMapping.LUTs[ii][1 - jj][kk]);
                                                    if (pairPinNum > 5)
                                                        continue;
                                                }
                                            }

                                            slotMapping.LUTs[i0][j0][k0] = nullptr;
                                            slotMapping.FFs[i1][j1][k1] = nullptr;
                                            assert(!slotMapping.LUTs[ii][jj][kk]);
                                            assert(!slotMapping.FFs[ii][jj][kk]);
                                            slotMapping.LUTs[ii][jj][kk] = targetLUT;
                                            slotMapping.FFs[ii][jj][kk] = targetFF;

                                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                                            cell2slot[targetFF] = std::array<int, 3>({ii, jj, kk});
                                            cell2slot[targetLUT] = std::array<int, 3>({ii, jj, kk});
                                            optimized = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if(ffQBlockedMask && !assignAccessibleFFSlots())return;
    if(!chooseLocalPinMapping())return;

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    float clockPeriod = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph()->getClockPeriod();
    float directConnectCnt = 0;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (checkDirectLUTFFConnect(FF2LUT, slotMapping.LUTs[i][j][k], slotMapping.FFs[i][j][k]))
                {
                    auto srcCell = slotMapping.LUTs[i][j][k];
                    unsigned int srcCellId = srcCell->getCellId();
                    auto srcNode = timingNodes[srcCellId];
                    // int succPathLen = srcNode->getLongestPathLength();
                    if (srcNode->getForwardLevel() < 0)
                        continue;
                    float slack =
                        (srcNode->getLatestInputArrival() - srcNode->getRequiredArrivalTime()) / clockPeriod + 20;
                    directConnectCnt += slack;
                }
            }
        }
    }
    if (directConnectCnt > best_DirectConnect)
    {
        best_SlotMapping = slotMapping;
        best_mappedCells = mappedCells;
        best_mappedLUTs = mappedLUTs;
        best_mappedFFs = mappedFFs;
        best_DirectConnect = directConnectCnt;
    }
}

void ParallelCLBPacker::PackingCLBSite::finalMapToSlotsForCommonLUTFFInSite(int FFControlSetOrderId)
{
    AMF_PROFILE_FUNCTION("final_packing");
    int FFSwapOption[24][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
                               {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
                               {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
                               {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};

    slotMapping = SiteBELMapping(ffQBlockedMask);
    mappedCells.clear();
    mappedLUTs.clear();
    mappedFFs.clear();
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> FF2LUT;
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> LUT2FF;
    auto singleLUTs = determinedClusterInSite->getSortedSingleLUTs();
    auto pairedLUTs = determinedClusterInSite->getSortedPairedLUTs();

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            int halfCLBId = i * 2 + j;
            auto &CSFF = determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];
            int anotherHalfCLBId = i * 2 + (1 - j);
            auto &anotherCSFF =
                determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]];
            if (!compatibleInOneHalfCLB(FFSwapOption[FFControlSetOrderId][halfCLBId],
                                        FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]))
            {
                mappedCells.clear();
                mappedLUTs.clear();
                mappedFFs.clear();
                return;
            }
            if (halfCLBId != FFSwapOption[FFControlSetOrderId][halfCLBId])
            {
                for (auto FF : CSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasMUX())
                    {
                        mappedCells.clear();
                        mappedLUTs.clear();
                        mappedFFs.clear();
                        return;
                    }
                }
            }
            if (anotherHalfCLBId != FFSwapOption[FFControlSetOrderId][anotherHalfCLBId])
            {
                for (auto FF : anotherCSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasMUX())
                    {
                        mappedCells.clear();
                        mappedLUTs.clear();
                        mappedFFs.clear();
                        return;
                    }
                }
            }
        }
    }

    // mapped FFs
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (!slotMapping.FFs[i][j][k])
                {
                    int halfCLBId = i * 2 + j;
                    auto &CSFF =
                        determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];

                    for (auto tmpFF : CSFF.getFFs())
                    {
                        if (mappedFFs.find(tmpFF) == mappedFFs.end())
                        {
                            mappedFFs.insert(tmpFF);
                            mappedCells.insert(tmpFF);
                            slotMapping.FFs[i][j][k] = tmpFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    // check directConnect
    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            PlacementInfo::PlacementMacro *pairMacro =
                dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnitByCell(curFF));
            if (pairMacro)
            {
                if (pairMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
                {
                    assert(pairMacro->getCells().size() == 2);
                    assert(pairMacro->getCells()[0]->isLUT());
                    assert(pairMacro->getCells()[1]->isFF());
                    FF2LUT[pairMacro->getCells()[1]] = pairMacro->getCells()[0];
                    LUT2FF[pairMacro->getCells()[0]] = pairMacro->getCells()[1];
                }
            }
        }
    }

    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            if (FF2LUT.find(curFF) == FF2LUT.end())
            {
                for (auto pair : pairedLUTs)
                {
                    if (isDirectConnectLUTFF(pair.first, curFF))
                    {
                        FF2LUT[curFF] = pair.first;
                        LUT2FF[pair.first] = curFF;
                        break;
                    }
                    if (isDirectConnectLUTFF(pair.second, curFF))
                    {
                        FF2LUT[curFF] = pair.second;
                        LUT2FF[pair.second] = curFF;
                        break;
                    }
                }
                if (FF2LUT.find(curFF) == FF2LUT.end())
                {
                    for (auto curLUT : singleLUTs)
                    {
                        if (isDirectConnectLUTFF(curLUT, curFF))
                        {
                            FF2LUT[curFF] = curLUT;
                            LUT2FF[curLUT] = curFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    assert(singleLUTs.size() + pairedLUTs.size() <= 8);
    // map LUT connected to carry and their paired LUT
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (!slotMapping.LUTs[i][j][k])
                {
                    if (slotMapping.LUTs[i][1 - j][k])
                    {
                        for (auto pair : pairedLUTs)
                        {
                            if (pair.first == slotMapping.LUTs[i][1 - j][k])
                            {
                                slotMapping.LUTs[i][1 - j][k] = pair.first;
                                slotMapping.LUTs[i][j][k] = pair.second;
                                mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                break;
                            }
                            if (pair.second == slotMapping.LUTs[i][1 - j][k])
                            {
                                slotMapping.LUTs[i][1 - j][k] = pair.second;
                                slotMapping.LUTs[i][j][k] = pair.first;
                                mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // mapped paired LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto pair : pairedLUTs)
                {
                    if (mappedLUTs.find(pair.first) == mappedLUTs.end())
                    {
                        if (mappedLUTs.find(pair.second) != mappedLUTs.end())
                        {
                            for (int i = 0; i < 2; i++)
                            {
                                for (int j = 0; j < 2; j++)
                                {
                                    for (int k = 0; k < 4; k++)
                                    {
                                        if (slotMapping.LUTs[i][j][k])
                                        {
                                            std::cout << "i,j,k:" << i << "," << j << "," << k << "   "
                                                      << slotMapping.LUTs[i][j][k] << "\n";
                                        }
                                    }
                                }
                            }
                            determinedClusterInSite->printMyself();
                            std::cout.flush();
                        }
                        assert(mappedLUTs.find(pair.second) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = pair.first;
                        slotMapping.LUTs[i][1][k] = pair.second;
                        mappedLUTs.insert(slotMapping.LUTs[i][0][k]);
                        mappedLUTs.insert(slotMapping.LUTs[i][1][k]);
                        mappedCells.insert(slotMapping.LUTs[i][0][k]);
                        mappedCells.insert(slotMapping.LUTs[i][1][k]);
                        break;
                    }
                }
            }
        }
    }

    // mapped single LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto tmpLUT : singleLUTs)
                {
                    if (mappedLUTs.find(tmpLUT) == mappedLUTs.end())
                    {
                        assert(mappedLUTs.find(tmpLUT) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = tmpLUT;
                        mappedLUTs.insert(tmpLUT);
                        mappedCells.insert(tmpLUT);
                        break;
                    }
                }
            }
        }
    }

    // assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() * 2 ==
    //        mappedLUTs.size());

    assert(singleLUTs.size() + pairedLUTs.size() <= 8);
    if (determinedClusterInSite)
    {

        unsigned int FFCnt = 0;
        for (auto &CSFF : determinedClusterInSite->getFFControlSets())
            FFCnt += CSFF.getFFs().size();

        if (FFCnt != mappedFFs.size())
        {
            std::cout << "FFCnt: " << FFCnt << " mappedFFs.size():" << mappedFFs.size() << "\n";
            determinedClusterInSite->printMyself();
            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        std::cout << "i,k,j:" << i << "," << k << "," << j << ":\n";
                        if (slotMapping.LUTs[i][j][k])
                            std::cout << slotMapping.LUTs[i][j][k] << "\n";
                        if (slotMapping.FFs[i][j][k])
                            std::cout << slotMapping.FFs[i][j][k] << "\n";
                    }
                }
            }
            assert(FFCnt == mappedFFs.size());
        }
    }

    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int k0 = 0; k0 < 4; k0++)
        {
            if (!isLUT6(slotMapping.LUTs[i0][0][k0]) && !isLUT6(slotMapping.LUTs[i0][1][k0]))
            {
                float oriDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][0][k0]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][1][k0]);

                // switch locations
                float newDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][1][k0]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][0][k0]);

                if (oriDirectInternalRoute_slack < newDirectInternalRoute_slack)
                {
                    DesignInfo::DesignCell *tmpLUT;
                    tmpLUT = slotMapping.LUTs[i0][0][k0];
                    slotMapping.LUTs[i0][0][k0] = slotMapping.LUTs[i0][1][k0];
                    slotMapping.LUTs[i0][1][k0] = tmpLUT;
                    assert(!isLUT6(tmpLUT));
                }
            }
        }
    }

    int LUTSwapOptions[4][4] = {{0, 1, 0, 1}, {1, 0, 0, 1}, {1, 0, 1, 0}, {0, 1, 1, 0}};

    for (int attempt = 0; attempt < 2; attempt++)
    {
        for (int i0 = 0; i0 < 2; i0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                for (int i1 = 0; i1 < 2; i1++)
                {
                    for (int k1 = 0; k1 < 4; k1++)
                    {
                        if (i0 == i1 && k0 == k1)
                        {
                            continue;
                        }
                        float oriDirectInternalRoute_slack =
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0],
                                                          slotMapping.FFs[i0][0][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0],
                                                          slotMapping.FFs[i0][1][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][0][k1],
                                                          slotMapping.FFs[i1][0][k1]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][1][k1],
                                                          slotMapping.FFs[i1][1][k1]);

                        float optDirectInternalRoute_slack = -100000000;
                        int optimalOption = -1;
                        for (int optionId = 0; optionId < 4; optionId++)
                        {
                            // switch locations
                            float newDirectInternalRoute_slack =
                                checkDirectLUTFFConnect_slack(FF2LUT,
                                                              slotMapping.LUTs[i1][LUTSwapOptions[optionId][0]][k1],
                                                              slotMapping.FFs[i0][0][k0]) +
                                checkDirectLUTFFConnect_slack(FF2LUT,
                                                              slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1],
                                                              slotMapping.FFs[i0][1][k0]) +
                                checkDirectLUTFFConnect_slack(FF2LUT,
                                                              slotMapping.LUTs[i0][LUTSwapOptions[optionId][2]][k0],
                                                              slotMapping.FFs[i1][0][k1]) +
                                checkDirectLUTFFConnect_slack(FF2LUT,
                                                              slotMapping.LUTs[i0][LUTSwapOptions[optionId][3]][k0],
                                                              slotMapping.FFs[i1][1][k1]);
                            if (isLUT6(slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1]) ||
                                isLUT6(slotMapping.LUTs[i0][LUTSwapOptions[optionId][3]]
                                                       [k0])) // illegal to put LUT6 at LUT5 slot
                                continue;
                            if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                            {
                                optimalOption = optionId;
                                optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                            }
                        }

                        if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
                        {
                            DesignInfo::DesignCell *tmpLUT0 =
                                slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][0]][k1];
                            DesignInfo::DesignCell *tmpLUT1 =
                                slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][1]][k1];
                            DesignInfo::DesignCell *tmpLUT2 =
                                slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][2]][k0];
                            DesignInfo::DesignCell *tmpLUT3 =
                                slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][3]][k0];
                            slotMapping.LUTs[i0][0][k0] = tmpLUT0;
                            assert(!isLUT6(tmpLUT1));
                            slotMapping.LUTs[i0][1][k0] = tmpLUT1;
                            slotMapping.LUTs[i1][0][k1] = tmpLUT2;
                            assert(!isLUT6(tmpLUT3));
                            slotMapping.LUTs[i1][1][k1] = tmpLUT3;
                            continue;
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            float oriDirectInternalRoute_slack =
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0], slotMapping.FFs[i][j][0]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1], slotMapping.FFs[i][j][1]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2], slotMapping.FFs[i][j][2]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3], slotMapping.FFs[i][j][3]);
            float optDirectInternalRoute_slack = -100000000;
            int optimalOption = -1;
            for (int optionId = 0; optionId < 24; optionId++)
            {
                float newDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][0]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][1]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][2]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][3]]);
                if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                {
                    optimalOption = optionId;
                    optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                }
            }
            if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
            {
                DesignInfo::DesignCell *tmpFF0 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][0]];
                DesignInfo::DesignCell *tmpFF1 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][1]];
                DesignInfo::DesignCell *tmpFF2 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][2]];
                DesignInfo::DesignCell *tmpFF3 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][3]];
                slotMapping.FFs[i][j][0] = tmpFF0;
                slotMapping.FFs[i][j][1] = tmpFF1;
                slotMapping.FFs[i][j][2] = tmpFF2;
                slotMapping.FFs[i][j][3] = tmpFF3;
                continue;
            }
        }
    }

    std::map<DesignInfo::DesignCell *, std::array<int, 3>> cell2slot;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (slotMapping.FFs[i][j][k])
                {
                    cell2slot[slotMapping.FFs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
                if (slotMapping.LUTs[i][j][k])
                {
                    cell2slot[slotMapping.LUTs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
            }
        }
    }

    // Move FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                if (LUT2FF.find(slotMapping.LUTs[i0][j0][k0]) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[slotMapping.LUTs[i0][j0][k0]];
                    if (slotMapping.FFs[i0][j0][k0] == nullptr)
                    {
                        int halfCLBId0 = i0 * 2 + j0;
                        auto &CSFF0 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId0]];
                        int i1 = cell2slot[targetFF].at(0);
                        int j1 = cell2slot[targetFF].at(1);
                        int k1 = cell2slot[targetFF].at(2);
                        int halfCLBId1 = i1 * 2 + j1;
                        auto &CSFF1 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId1]];
                        if (CSFF0.compatibleWith(CSFF1.getCSId()) &&
                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, i0, j0))
                        {
                            bool compatible = true;
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][1 - j0][kk])
                                {
                                    if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
                                    {
                                        if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                            !DesignInfo::FFSRCompatible(
                                                slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getFFType(),
                                                targetFF->getControlSetInfo()->getFFType()))
                                        {
                                            compatible = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!compatible)
                                continue;

                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                            slotMapping.FFs[i0][j0][k0] = targetFF;
                            slotMapping.FFs[i1][j1][k1] = nullptr;
                            cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
                        }
                    }
                }
            }
        }
    }

    // Move LUT-FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                auto targetLUT = slotMapping.LUTs[i0][j0][k0];
                if (slotMapping.LUTs[i0][1 - j0][k0])
                    continue;
                if (LUT2FF.find(targetLUT) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[targetLUT];
                    int i1 = cell2slot[targetFF].at(0);
                    int j1 = cell2slot[targetFF].at(1);
                    int k1 = cell2slot[targetFF].at(2);
                    int halfCLBId1 = i1 * 2 + j1;

                    if (i0 != i1 || j0 != j1 || k0 != k1)
                    {
                        bool optimized = false;
                        for (int ii = 0; ii < 2 && !optimized; ii++)
                        {
                            for (int jj = 0; jj < 2 && !optimized; jj++)
                            {
                                bool compatible = true;
                                if (targetLUT->isLUT6() && jj == 1)
                                    continue;
                                int halfCLBId0 = ii * 2 + jj;
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo())
                                        {
                                            if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo()->getId() !=
                                                targetFF->getControlSetInfo()->getId())
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][1 - jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo())
                                        {
                                            if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                                !DesignInfo::FFSRCompatible(
                                                    slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo()->getFFType(),
                                                    targetFF->getControlSetInfo()->getFFType()))
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }

                                if (compatible)
                                {
                                    for (int kk = 0; kk < 4; kk++)
                                    {
                                        if (!slotMapping.LUTs[ii][jj][kk] && !slotMapping.FFs[ii][jj][kk] &&
                                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, ii, jj))
                                        {
                                            // if (slotMapping.LUTs[ii][1 - jj][kk])
                                            //     continue;
                                            if (targetLUT->isLUT6())
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                    continue;
                                            }
                                            else
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk] &&
                                                    slotMapping.LUTs[ii][1 - jj][kk]->isLUT6())
                                                    continue;
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                {
                                                    int pairPinNum =
                                                        getPairPinNum(targetLUT, slotMapping.LUTs[ii][1 - jj][kk]);
                                                    if (pairPinNum > 5)
                                                        continue;
                                                }
                                            }

                                            slotMapping.LUTs[i0][j0][k0] = nullptr;
                                            slotMapping.FFs[i1][j1][k1] = nullptr;
                                            assert(!slotMapping.LUTs[ii][jj][kk]);
                                            assert(!slotMapping.FFs[ii][jj][kk]);
                                            slotMapping.LUTs[ii][jj][kk] = targetLUT;
                                            slotMapping.FFs[ii][jj][kk] = targetFF;

                                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                                            cell2slot[targetFF] = std::array<int, 3>({ii, jj, kk});
                                            cell2slot[targetLUT] = std::array<int, 3>({ii, jj, kk});
                                            optimized = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // swap LUTs between LUT slots
    for (auto pair : LUT2FF)
    {
        auto targetLUT = pair.first;
        auto targetFF = pair.second;
        int luti = cell2slot[targetLUT].at(0);
        int lutj = cell2slot[targetLUT].at(1);
        int lutk = cell2slot[targetLUT].at(2);
        int ffi = cell2slot[targetFF].at(0);
        int ffj = cell2slot[targetFF].at(1);
        int ffk = cell2slot[targetFF].at(2);
        int halfCLBId_Ori = ffi * 2 + ffj;

        if (slotMapping.LUTs[luti][1 - lutj][lutk])
            continue;

        bool optimized = false;
        if (!(luti == ffi && lutj == ffj && lutk == ffk))
        {
            for (int i0 = 0; i0 < 2 && !optimized; i0++)
            {
                for (int k0 = 0; k0 < 4 && !optimized; k0++)
                {
                    if (isMuxMacro(slotMapping.LUTs[i0][0][k0]) || isMuxMacro(slotMapping.FFs[i0][0][k0]) ||
                        isMuxMacro(slotMapping.LUTs[i0][1][k0]) || isMuxMacro(slotMapping.FFs[i0][1][k0]))
                        continue;

                    if (!(slotMapping.FFs[i0][0][k0] || slotMapping.FFs[i0][1][k0]))
                    {
                        for (int j0 = 0; j0 < 2 && !optimized; j0++)
                        {
                            if (targetLUT->isLUT6() && j0 == 1)
                                continue;

                            bool compatible = true;

                            int halfCLBId_target = i0 * 2 + j0;
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][j0][kk])
                                {
                                    if (slotMapping.FFs[i0][j0][kk]->getControlSetInfo())
                                    {
                                        if (slotMapping.FFs[i0][j0][kk]->getControlSetInfo()->getId() !=
                                            targetFF->getControlSetInfo()->getId())
                                        {
                                            compatible = false;
                                        }
                                    }
                                }
                            }
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][1 - j0][kk])
                                {
                                    if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
                                    {
                                        if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                            !DesignInfo::FFSRCompatible(
                                                slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getFFType(),
                                                targetFF->getControlSetInfo()->getFFType()))
                                        {
                                            compatible = false;
                                        }
                                    }
                                }
                            }

                            if (compatible)
                            {

                                auto LUT0 = slotMapping.LUTs[i0][0][k0];
                                auto LUT1 = slotMapping.LUTs[i0][1][k0];

                                slotMapping.LUTs[i0][0][k0] = nullptr;
                                slotMapping.LUTs[i0][1][k0] = nullptr;

                                slotMapping.LUTs[luti][lutj][lutk] = nullptr;
                                slotMapping.FFs[ffi][ffj][ffk] = nullptr;

                                slotMapping.LUTs[i0][j0][k0] = targetLUT;
                                slotMapping.FFs[i0][j0][k0] = targetFF;

                                if (FFSwapOption[FFControlSetOrderId][halfCLBId_Ori] !=
                                    FFSwapOption[FFControlSetOrderId][halfCLBId_target])
                                    determinedClusterInSite->moveFFFromCS1ToCS0(
                                        targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId_Ori],
                                        FFSwapOption[FFControlSetOrderId][halfCLBId_target]);
                                cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
                                cell2slot[targetLUT] = std::array<int, 3>({i0, j0, k0});

                                slotMapping.LUTs[luti][0][lutk] = LUT0;
                                slotMapping.LUTs[luti][1][lutk] = LUT1;

                                if (LUT0)
                                    cell2slot[LUT0] = std::array<int, 3>({luti, 0, lutk});
                                if (LUT1)
                                    cell2slot[LUT1] = std::array<int, 3>({luti, 1, lutk});

                                optimized = true;

                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // // Move FF to empty slot if possible to increase direct connect
    // for (int i0 = 0; i0 < 2; i0++)
    // {
    //     for (int j0 = 0; j0 < 2; j0++)
    //     {
    //         for (int k0 = 0; k0 < 4; k0++)
    //         {
    //             if (!slotMapping.LUTs[i0][j0][k0])
    //                 continue;
    //             if (LUT2FF.find(slotMapping.LUTs[i0][j0][k0]) != LUT2FF.end())
    //             {
    //                 auto targetFF = LUT2FF[slotMapping.LUTs[i0][j0][k0]];
    //                 if (slotMapping.FFs[i0][j0][k0] == nullptr)
    //                 {
    //                     int halfCLBId0 = i0 * 2 + j0;
    //                     auto &CSFF0 =
    //                         determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId0]];
    //                     int i1 = cell2slot[targetFF].at(0);
    //                     int j1 = cell2slot[targetFF].at(1);
    //                     int k1 = cell2slot[targetFF].at(2);
    //                     int halfCLBId1 = i1 * 2 + j1;
    //                     auto &CSFF1 =
    //                         determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId1]];
    //                     if (CSFF0.compatibleWith(CSFF1.getCSId()))
    //                     {
    //                         bool compatible = true;
    //                         for (int kk = 0; kk < 4; kk++)
    //                         {
    //                             if (slotMapping.FFs[i0][1 - j0][kk])
    //                             {
    //                                 if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
    //                                 {
    //                                     if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getCLK() !=
    //                                             targetFF->getControlSetInfo()->getCLK() ||
    //                                         slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getSR() !=
    //                                             targetFF->getControlSetInfo()->getSR())
    //                                     {
    //                                         compatible = false;
    //                                         break;
    //                                     }
    //                                 }
    //                             }
    //                         }
    //                         if (!compatible)
    //                             continue;

    //                         if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
    //                             FFSwapOption[FFControlSetOrderId][halfCLBId0])
    //                             determinedClusterInSite->moveFFFromCS1ToCS0(
    //                                 targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
    //                                 FFSwapOption[FFControlSetOrderId][halfCLBId0]);
    //                         slotMapping.FFs[i0][j0][k0] = targetFF;
    //                         slotMapping.FFs[i1][j1][k1] = nullptr;
    //                         cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }

    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int k0 = 0; k0 < 4; k0++)
        {
            if (slotMapping.LUTs[i0][0][k0])
            {
                if (slotMapping.LUTs[i0][0][k0]->isLUT6())
                {
                    assert(!slotMapping.LUTs[i0][1][k0]);
                }
                else if (slotMapping.LUTs[i0][1][k0])
                {
                    assert(!slotMapping.LUTs[i0][1][k0]->isLUT6());
                    int pairPinNum = getPairPinNum(slotMapping.LUTs[i0][1][k0], slotMapping.LUTs[i0][0][k0]);
                    assert(pairPinNum <= 5);
                }
            }
        }
    }

    if(ffQBlockedMask && !assignAccessibleFFSlots())return;
    if(!chooseLocalPinMapping())return;

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    float clockPeriod = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph()->getClockPeriod();
    float directConnectCnt = 0;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (checkDirectLUTFFConnect(FF2LUT, slotMapping.LUTs[i][j][k], slotMapping.FFs[i][j][k]))
                {
                    auto srcCell = slotMapping.LUTs[i][j][k];
                    unsigned int srcCellId = srcCell->getCellId();
                    auto srcNode = timingNodes[srcCellId];
                    // int succPathLen = srcNode->getLongestPathLength();
                    if (srcNode->getForwardLevel() < 0)
                        continue;
                    float slack =
                        (srcNode->getLatestInputArrival() - srcNode->getRequiredArrivalTime()) / clockPeriod + 20;
                    if (j == 0)
                        slack += 0.025;
                    directConnectCnt += slack;
                }
            }
        }
    }
    if (directConnectCnt > best_DirectConnect)
    {
        best_SlotMapping = slotMapping;
        best_mappedCells = mappedCells;
        best_mappedLUTs = mappedLUTs;
        best_mappedFFs = mappedFFs;
        best_DirectConnect = directConnectCnt;
    }
}

void ParallelCLBPacker::PackingCLBSite::mapMuxF8Macro(int muxF8Offset, PlacementInfo::PlacementMacro *MUXF8Macro)
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(muxF8Offset < 2);
    slotMapping.MuxF8[muxF8Offset] = MUXF8Macro->getCells()[0];
    assert(slotMapping.MuxF8[muxF8Offset]->getOriCellType() == DesignInfo::CellType_MUXF8);
    auto curMUXF8 = slotMapping.MuxF8[muxF8Offset];
    std::vector<DesignInfo::DesignCell *> virtualLUTs;
    std::vector<DesignInfo::DesignCell *> virtualFFs;
    std::vector<DesignInfo::DesignCell *> virtualMUXs;
    std::vector<DesignInfo::DesignCell *> LUTsInMacro;
    std::vector<DesignInfo::DesignCell *> FFsInMacro;
    std::vector<DesignInfo::DesignCell *> MUXsInMacro;
    for (auto curCell : MUXF8Macro->getCells())
    {
        if (curCell->isVirtualCell())
        {
            if (curCell->isMux())
                virtualMUXs.push_back(curCell);
            if (curCell->isFF())
                virtualFFs.push_back(curCell);
            if (curCell->isLUT())
                virtualLUTs.push_back(curCell);
        }
        else
        {
            if (curCell->isMux())
                MUXsInMacro.push_back(curCell);
            if (curCell->isFF())
                FFsInMacro.push_back(curCell);
            if (curCell->isLUT())
                LUTsInMacro.push_back(curCell);
        }
    }

    // map MuxF7
    for (DesignInfo::DesignPin *pinBeDriven : curMUXF8->getInputPins())
    {
        if (!pinBeDriven->getDriverPin())
            continue;
        if (pinBeDriven->getRefPinName() == "I0")
        {
            auto I0MuxF7 = pinBeDriven->getDriverPin()->getCell();
            if (I0MuxF7->getCellType() == DesignInfo::CellType_MUXF7 && MUXF8Macro->hasCell(I0MuxF7))
            {
                assert(I0MuxF7->getCellType() == DesignInfo::CellType_MUXF7);
                slotMapping.MuxF7[muxF8Offset][1] = I0MuxF7; // I0 is for the upper one
            }
        }
        else if (pinBeDriven->getRefPinName() == "I1")
        {
            auto I1MuxF7 = pinBeDriven->getDriverPin()->getCell();
            if (I1MuxF7->getCellType() == DesignInfo::CellType_MUXF7 && MUXF8Macro->hasCell(I1MuxF7))
            {
                assert(I1MuxF7->getCellType() == DesignInfo::CellType_MUXF7);
                slotMapping.MuxF7[muxF8Offset][0] = I1MuxF7; // I1 is for the lower one
            }
        }
    }

    // fill route-thru MuxF7
    unsigned int fillVirtualCellCnt = 0;
    for (int i = 0; i < 2; i++)
    {
        if (!slotMapping.MuxF7[muxF8Offset][i])
        {
            assert(fillVirtualCellCnt < virtualMUXs.size());
            slotMapping.MuxF7[muxF8Offset][i] = virtualMUXs[fillVirtualCellCnt];
            fillVirtualCellCnt++;
        }
    }
    assert(fillVirtualCellCnt == virtualMUXs.size());

    // fill LUTs into slots
    for (int i = 0; i < 2; i++)
    {
        auto curMuxF7 = slotMapping.MuxF7[muxF8Offset][i];
        for (DesignInfo::DesignPin *pinBeDriven : curMuxF7->getInputPins())
        {
            if (!pinBeDriven->getDriverPin())
                continue;
            if (pinBeDriven->getRefPinName() == "I0")
            {
                auto I0LUT = pinBeDriven->getDriverPin()->getCell();
                if (I0LUT->isLUT() && MUXF8Macro->hasCell(I0LUT))
                {
                    assert(I0LUT->isLUT6());
                    slotMapping.LUTs[muxF8Offset][0][i * 2 + 1] = I0LUT; // I0 is for the upper one
                }
            }
            else if (pinBeDriven->getRefPinName() == "I1")
            {
                auto I1LUT = pinBeDriven->getDriverPin()->getCell();
                if (I1LUT->isLUT() && MUXF8Macro->hasCell(I1LUT))
                {
                    assert(I1LUT->isLUT6());
                    slotMapping.LUTs[muxF8Offset][0][i * 2] = I1LUT; // I1 is for the lower one
                }
            }
        }
    }

    // fill route-thru LUTs
    fillVirtualCellCnt = 0;
    for (int i = 0; i < 4; i++)
    {
        if (!slotMapping.LUTs[muxF8Offset][0][i])
        {
            assert(fillVirtualCellCnt < virtualLUTs.size());
            slotMapping.LUTs[muxF8Offset][0][i] = virtualLUTs[fillVirtualCellCnt];
            fillVirtualCellCnt++;
        }
    }
    assert(fillVirtualCellCnt == virtualLUTs.size());

    // fill selection signal FFs
    if (3 == virtualFFs.size())
    {
        slotMapping.FFs[muxF8Offset][0][0] = virtualFFs[0];
        slotMapping.FFs[muxF8Offset][0][1] = virtualFFs[1];
        slotMapping.FFs[muxF8Offset][0][2] = virtualFFs[2];
    }
    else if (2 == virtualFFs.size())
    {
        assert(FFsInMacro.size() == 1);
        slotMapping.FFs[muxF8Offset][0][0] = virtualFFs[0];
        slotMapping.FFs[muxF8Offset][0][1] = virtualFFs[1];
        slotMapping.FFs[muxF8Offset][0][2] = FFsInMacro[0];
    }
    else
    {
        assert(false && "undefined situation");
    }

    for (auto tmpCell : MUXF8Macro->getCells())
    {
        mappedCells.insert(tmpCell);
        if (tmpCell->isLUT())
            mappedLUTs.insert(tmpCell);
        else if (tmpCell->isFF())
            mappedFFs.insert(tmpCell);
    }
}

void ParallelCLBPacker::PackingCLBSite::mapMuxF7Macro(int halfCLBOffset, PlacementInfo::PlacementMacro *MUXF7Macro)
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(halfCLBOffset < 2);
    unsigned int F7Offset = 0;
    if (slotMapping.MuxF7[halfCLBOffset][F7Offset])
        F7Offset++;
    if (slotMapping.MuxF7[halfCLBOffset][F7Offset])
    {
        std::cout << determinedClusterInSite << "\n";
        std::cout.flush();
    }
    assert(!slotMapping.MuxF7[halfCLBOffset][F7Offset]);
    slotMapping.MuxF7[halfCLBOffset][F7Offset] = MUXF7Macro->getCells()[0];
    assert(slotMapping.MuxF7[halfCLBOffset][F7Offset]->getOriCellType() == DesignInfo::CellType_MUXF7);
    auto curMUXF7 = slotMapping.MuxF7[halfCLBOffset][F7Offset];
    std::vector<DesignInfo::DesignCell *> virtualLUTs;
    std::vector<DesignInfo::DesignCell *> virtualFFs;
    std::vector<DesignInfo::DesignCell *> virtualMUXs;
    std::vector<DesignInfo::DesignCell *> LUTsInMacro;
    std::vector<DesignInfo::DesignCell *> FFsInMacro;
    std::vector<DesignInfo::DesignCell *> MUXsInMacro;
    for (auto curCell : MUXF7Macro->getCells())
    {
        if (curCell->isVirtualCell())
        {
            if (curCell->isMux())
                virtualMUXs.push_back(curCell);
            if (curCell->isFF())
                virtualFFs.push_back(curCell);
            if (curCell->isLUT())
                virtualLUTs.push_back(curCell);
        }
        else
        {
            if (curCell->isMux())
                MUXsInMacro.push_back(curCell);
            if (curCell->isFF())
                FFsInMacro.push_back(curCell);
            if (curCell->isLUT())
                LUTsInMacro.push_back(curCell);
        }
    }

    // fill LUTs into slots
    for (DesignInfo::DesignPin *pinBeDriven : curMUXF7->getInputPins())
    {
        if (!pinBeDriven->getDriverPin())
        {
            continue;
        }

        assert(pinBeDriven->getDriverPin());
        if (pinBeDriven->getRefPinName() == "I0")
        {
            auto I0LUT = pinBeDriven->getDriverPin()->getCell();
            if (I0LUT->isLUT() && MUXF7Macro->hasCell(I0LUT))
            {
                assert(I0LUT->isLUT6());
                slotMapping.LUTs[halfCLBOffset][0][F7Offset * 2 + 1] = I0LUT; // I0 is for the upper one
            }
        }
        else if (pinBeDriven->getRefPinName() == "I1")
        {
            if (!pinBeDriven->getDriverPin())
            {
                continue;
            }

            assert(pinBeDriven->getDriverPin());
            auto I1LUT = pinBeDriven->getDriverPin()->getCell();
            if (I1LUT->isLUT() && MUXF7Macro->hasCell(I1LUT))
            {
                assert(I1LUT->isLUT6());
                slotMapping.LUTs[halfCLBOffset][0][F7Offset * 2] = I1LUT; // I1 is for the lower one
            }
        }
    }

    // fill route-thru LUTs
    unsigned int fillVirtualCellCnt = 0;
    assert(virtualLUTs.size() <= 2 && "There should be no more than two Virtual LUTs");
    for (unsigned int i = F7Offset * 2; i < F7Offset * 2 + 2; i++)
    {
        if (!slotMapping.LUTs[halfCLBOffset][0][i])
        {
            assert(fillVirtualCellCnt < virtualLUTs.size());
            slotMapping.LUTs[halfCLBOffset][0][i] = virtualLUTs[fillVirtualCellCnt];
            fillVirtualCellCnt++;
        }
    }
    assert(fillVirtualCellCnt == virtualLUTs.size());

    // fill selection signal FFs
    if (1 == virtualFFs.size())
    {
        slotMapping.FFs[halfCLBOffset][0][F7Offset * 2] = virtualFFs[0];
    }
    else
    {
        assert(virtualFFs.size() <= 1 && "MUXF7 macro should only have one virtual FF.");
    }

    if (1 == FFsInMacro.size())
    {
        assert(FFsInMacro.size() == 1);
        slotMapping.FFs[halfCLBOffset][0][F7Offset * 2 + 1] = FFsInMacro[0];
    }
    else
    {
        assert(FFsInMacro.size() <= 1 && "undefined situation");
    }

    for (auto tmpCell : MUXF7Macro->getCells())
    {
        mappedCells.insert(tmpCell);
        if (tmpCell->isLUT())
            mappedLUTs.insert(tmpCell);
        else if (tmpCell->isFF())
            mappedFFs.insert(tmpCell);
    }
}

int ParallelCLBPacker::PackingCLBSite::findMuxFromHalfCLB(PlacementInfo::PlacementMacro *MUXF8Macro)
{
    assert(determinedClusterInSite);
    int resHalfCLB = -1;
    for (int i = 0; i < 4; i++)
    {
        for (auto tmpFF : determinedClusterInSite->getFFControlSets()[i].getFFs())
        {
            if (MUXF8Macro->hasCell(tmpFF))
            {
                if (resHalfCLB == -1)
                    resHalfCLB = i;
                else
                    assert(resHalfCLB == i);
            }
        }
    }
    return resHalfCLB;
}
void ParallelCLBPacker::PackingCLBSite::greedyMapMuxForCommonLUTFFInSite()
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(!checkIsPrePackedSite() && checkIsMuxSite());
    assert(fixedPairedLUTs.size() == 0 && conflictLUTs.size() == 0);
    assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() <= 8);

    const auto initialBanks = determinedClusterInSite->getFFControlSets();
    for (int i = 0; i < 24; i++)
        greedyMapMuxForCommonLUTFFInSite(i);

    determinedClusterInSite->evictFFsFromMuxHalfCLB();

    for (int i = 0; i < 24; i++)
        greedyMapMuxForCommonLUTFFInSite(i);

    // Timing-dependent swaps can destroy all dedicated F7 input paths even
    // when the original bank assignment has a physical mapping. Reconstruct
    // the pre-swap assignment and subject it to the same final route checks.
    if (best_DirectConnect <= -99999999)
    {
        determinedClusterInSite->restoreControlSets(initialBanks);
        for (int order = 0; order < 24 && best_DirectConnect <= -99999999; ++order)
            greedyMapMuxForCommonLUTFFInSite(order, false);
    }
    requireLegalSlotMapping("MUX");
    slotMapping = best_SlotMapping;
    mappedCells = best_mappedCells;
    mappedLUTs = best_mappedLUTs;
    mappedFFs = best_mappedFFs;
    moveLUTToLUT6Slot();
}

void ParallelCLBPacker::PackingCLBSite::greedyMapMuxForCommonLUTFFInSite(int FFControlSetOrderId, bool optimize)
{
    AMF_PROFILE_FUNCTION("final_packing");
    int FFSwapOption[24][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2}, {0, 3, 2, 1},
                               {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
                               {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
                               {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};

    slotMapping = SiteBELMapping(ffQBlockedMask);
    mappedCells.clear();
    mappedLUTs.clear();
    mappedFFs.clear();
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> LUT2FF;
    std::map<DesignInfo::DesignCell *, DesignInfo::DesignCell *> FF2LUT;
    auto singleLUTs = determinedClusterInSite->getSortedSingleLUTs();
    auto pairedLUTs = determinedClusterInSite->getSortedPairedLUTs();

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            int halfCLBId = i * 2 + j;
            auto &CSFF = determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];
            int anotherHalfCLBId = i * 2 + (1 - j);
            auto &anotherCSFF =
                determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]];
            if (!compatibleInOneHalfCLB(FFSwapOption[FFControlSetOrderId][halfCLBId],
                                        FFSwapOption[FFControlSetOrderId][anotherHalfCLBId]))
            {
                return;
            }
            if (halfCLBId != FFSwapOption[FFControlSetOrderId][halfCLBId])
            {
                for (auto FF : CSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasMUX())
                    {
                        return;
                    }
                }
            }
            if (anotherHalfCLBId != FFSwapOption[FFControlSetOrderId][anotherHalfCLBId])
            {
                for (auto FF : anotherCSFF.getFFs())
                {
                    if (placementInfo->getPlacementUnitByCell(FF)->checkHasMUX())
                    {
                        return;
                    }
                }
            }
        }
    }

    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            PlacementInfo::PlacementMacro *pairMacro =
                dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnitByCell(curFF));
            if (pairMacro)
            {
                if (pairMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
                {
                    assert(pairMacro->getCells().size() == 2);
                    assert(pairMacro->getCells()[0]->isLUT());
                    assert(pairMacro->getCells()[1]->isFF());
                    FF2LUT[pairMacro->getCells()[1]] = pairMacro->getCells()[0];
                    LUT2FF[pairMacro->getCells()[0]] = pairMacro->getCells()[1];
                }
            }
        }
    }

    for (auto &FFSet : determinedClusterInSite->getFFControlSets())
    {
        for (auto curFF : FFSet.getFFs())
        {
            if (FF2LUT.find(curFF) == FF2LUT.end())
            {
                for (auto pair : pairedLUTs)
                {
                    if (isDirectConnectLUTFF(pair.first, curFF))
                    {
                        FF2LUT[curFF] = pair.first;
                        LUT2FF[pair.first] = curFF;
                        break;
                    }
                    if (isDirectConnectLUTFF(pair.second, curFF))
                    {
                        FF2LUT[curFF] = pair.second;
                        LUT2FF[pair.second] = curFF;
                        break;
                    }
                }
                if (FF2LUT.find(curFF) == FF2LUT.end())
                {
                    for (auto curLUT : singleLUTs)
                    {
                        if (isDirectConnectLUTFF(curLUT, curFF))
                        {
                            FF2LUT[curFF] = curLUT;
                            LUT2FF[curLUT] = curFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    std::vector<PlacementInfo::PlacementMacro *> MUXF7Macros;
    std::vector<PlacementInfo::PlacementMacro *> MUXF8Macros;
    for (auto tmpPU : determinedClusterInSite->getPUs())
    {
        if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
        {
            if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MUX7)
            {
                MUXF7Macros.push_back(tmpMacro);
            }
            else if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MUX8)
            {
                MUXF8Macros.push_back(tmpMacro);
            }
        }
    }
    assert((MUXF7Macros.size() + 1) / 2 + MUXF8Macros.size() <= 2);

    for (unsigned int i = 0; i < MUXF8Macros.size(); i++)
    {
        assert(MUXF8Macros[i]->getCells().size() == 10);
        assert(MUXF8Macros[i]->getCells()[0]->getCellType() == DesignInfo::CellType_MUXF8);
        int targetHalfCLB = findMuxFromHalfCLB(MUXF8Macros[i]);
        assert(targetHalfCLB % 2 == 0);
        mapMuxF8Macro(targetHalfCLB / 2, MUXF8Macros[i]);
    }

    for (unsigned int i = 0; i < MUXF7Macros.size(); i++)
    {
        assert(MUXF7Macros[i]->getCells().size() >= 4 && MUXF7Macros[i]->getCells().size() <= 5);
        assert(MUXF7Macros[i]->getCells()[0]->getCellType() == DesignInfo::CellType_MUXF7);
        int targetHalfCLB = findMuxFromHalfCLB(MUXF7Macros[i]);
        assert(targetHalfCLB % 2 == 0);
        mapMuxF7Macro(targetHalfCLB / 2, MUXF7Macros[i]);
    }

    // mapped FFs
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 3; k >= 0; k--)
            {
                if (!slotMapping.FFs[i][j][k])
                {
                    int halfCLBId = i * 2 + j;
                    auto &CSFF =
                        determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId]];

                    for (auto tmpFF : CSFF.getFFs())
                    {
                        if (mappedFFs.find(tmpFF) == mappedFFs.end())
                        {
                            mappedFFs.insert(tmpFF);
                            mappedCells.insert(tmpFF);
                            slotMapping.FFs[i][j][k] = tmpFF;
                            break;
                        }
                    }
                }
            }
        }
    }

    assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() <= 8);
    // map LUT connected to carry and their paired LUT
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (!slotMapping.LUTs[i][j][k])
                {
                    if (slotMapping.LUTs[i][1 - j][k])
                    {
                        for (auto pair : pairedLUTs)
                        {
                            if (pair.first == slotMapping.LUTs[i][1 - j][k])
                            {
                                slotMapping.LUTs[i][1 - j][k] = pair.first;
                                slotMapping.LUTs[i][j][k] = pair.second;
                                mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                break;
                            }
                            if (pair.second == slotMapping.LUTs[i][1 - j][k])
                            {
                                slotMapping.LUTs[i][1 - j][k] = pair.second;
                                slotMapping.LUTs[i][j][k] = pair.first;
                                mappedLUTs.insert(slotMapping.LUTs[i][j][k]);
                                mappedLUTs.insert(slotMapping.LUTs[i][1 - j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][j][k]);
                                mappedCells.insert(slotMapping.LUTs[i][1 - j][k]);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // mapped paired LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto pair : pairedLUTs)
                {
                    if (mappedLUTs.find(pair.first) == mappedLUTs.end())
                    {
                        if (mappedLUTs.find(pair.second) != mappedLUTs.end())
                        {
                            for (int i = 0; i < 2; i++)
                            {
                                for (int j = 0; j < 2; j++)
                                {
                                    for (int k = 0; k < 4; k++)
                                    {
                                        if (slotMapping.LUTs[i][j][k])
                                        {
                                            std::cout << "i,j,k:" << i << "," << j << "," << k << "   "
                                                      << slotMapping.LUTs[i][j][k] << "\n";
                                        }
                                    }
                                }
                            }
                            determinedClusterInSite->printMyself();
                            std::cout.flush();
                        }
                        assert(mappedLUTs.find(pair.second) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = pair.first;
                        slotMapping.LUTs[i][1][k] = pair.second;
                        mappedLUTs.insert(slotMapping.LUTs[i][0][k]);
                        mappedLUTs.insert(slotMapping.LUTs[i][1][k]);
                        mappedCells.insert(slotMapping.LUTs[i][0][k]);
                        mappedCells.insert(slotMapping.LUTs[i][1][k]);
                        break;
                    }
                }
            }
        }
    }

    // mapped single LUTs
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                for (auto tmpLUT : singleLUTs)
                {
                    if (mappedLUTs.find(tmpLUT) == mappedLUTs.end())
                    {
                        assert(mappedLUTs.find(tmpLUT) == mappedLUTs.end());
                        slotMapping.LUTs[i][0][k] = tmpLUT;
                        mappedLUTs.insert(tmpLUT);
                        mappedCells.insert(tmpLUT);
                        break;
                    }
                }
            }
        }
    }

    assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() <= 8);

    if (!optimize)
    {
        if ((!ffQBlockedMask || assignAccessibleFFSlots()) && chooseLocalPinMapping())
        {
            best_SlotMapping = slotMapping;
            best_mappedCells = mappedCells;
            best_mappedLUTs = mappedLUTs;
            best_mappedFFs = mappedFFs;
            best_DirectConnect = 0;
        }
        return;
    }

    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int k0 = 0; k0 < 4; k0++)
        {
            if (!isLUT6(slotMapping.LUTs[i0][0][k0]) && !isLUT6(slotMapping.LUTs[i0][1][k0]))
            {
                float oriDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][0][k0]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][1][k0]);

                // switch locations
                float newDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][1][k0]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][0][k0]);

                if (oriDirectInternalRoute_slack < newDirectInternalRoute_slack)
                {
                    DesignInfo::DesignCell *tmpLUT;
                    tmpLUT = slotMapping.LUTs[i0][0][k0];
                    slotMapping.LUTs[i0][0][k0] = slotMapping.LUTs[i0][1][k0];
                    slotMapping.LUTs[i0][1][k0] = tmpLUT;
                    assert(!isLUT6(tmpLUT));
                }
            }
        }
    }

    int LUTSwapOptions[4][4] = {{0, 1, 0, 1}, {1, 0, 0, 1}, {1, 0, 1, 0}, {0, 1, 1, 0}};

    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int k0 = 0; k0 < 4; k0++)
        {
            for (int i1 = 0; i1 < 2; i1++)
            {
                for (int k1 = 0; k1 < 4; k1++)
                {
                    if (i0 == i1 && k0 == k1)
                    {
                        continue;
                    }
                    if ((isMuxMacro(slotMapping.LUTs[i0][0][k0]) || isMuxMacro(slotMapping.FFs[i0][0][k0]) ||
                         isMuxMacro(slotMapping.LUTs[i0][1][k0]) || isMuxMacro(slotMapping.FFs[i0][1][k0]) ||
                         isMuxMacro(slotMapping.LUTs[i1][0][k1]) || isMuxMacro(slotMapping.FFs[i1][0][k1]) ||
                         isMuxMacro(slotMapping.LUTs[i1][1][k1]) || isMuxMacro(slotMapping.FFs[i1][1][k1])))
                        continue;
                    float oriDirectInternalRoute_slack =
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][0][k0], slotMapping.FFs[i0][0][k0]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][1][k0], slotMapping.FFs[i0][1][k0]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][0][k1], slotMapping.FFs[i1][0][k1]) +
                        checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][1][k1], slotMapping.FFs[i1][1][k1]);

                    float optDirectInternalRoute_slack = -100000000;
                    int optimalOption = -1;
                    for (int optionId = 0; optionId < 4; optionId++)
                    {
                        // switch locations
                        float newDirectInternalRoute_slack =
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][LUTSwapOptions[optionId][0]][k1],
                                                          slotMapping.FFs[i0][0][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1],
                                                          slotMapping.FFs[i0][1][k0]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][LUTSwapOptions[optionId][2]][k0],
                                                          slotMapping.FFs[i1][0][k1]) +
                            checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i0][LUTSwapOptions[optionId][3]][k0],
                                                          slotMapping.FFs[i1][1][k1]);
                        if (isLUT6(slotMapping.LUTs[i1][LUTSwapOptions[optionId][1]][k1]) ||
                            isLUT6(slotMapping
                                       .LUTs[i0][LUTSwapOptions[optionId][3]][k0])) // illegal to put LUT6 at LUT5 slot
                            continue;
                        if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                        {
                            optimalOption = optionId;
                            optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                        }
                    }

                    if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
                    {
                        DesignInfo::DesignCell *tmpLUT0 = slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][0]][k1];
                        DesignInfo::DesignCell *tmpLUT1 = slotMapping.LUTs[i1][LUTSwapOptions[optimalOption][1]][k1];
                        DesignInfo::DesignCell *tmpLUT2 = slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][2]][k0];
                        DesignInfo::DesignCell *tmpLUT3 = slotMapping.LUTs[i0][LUTSwapOptions[optimalOption][3]][k0];
                        slotMapping.LUTs[i0][0][k0] = tmpLUT0;
                        assert(!isLUT6(tmpLUT1));
                        slotMapping.LUTs[i0][1][k0] = tmpLUT1;
                        slotMapping.LUTs[i1][0][k1] = tmpLUT2;
                        assert(!isLUT6(tmpLUT3));
                        slotMapping.LUTs[i1][1][k1] = tmpLUT3;
                        continue;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            // if ((isMuxMacro(slotMapping.FFs[i][j][0]) || isMuxMacro(slotMapping.FFs[i][j][1]) ||
            //      isMuxMacro(slotMapping.FFs[i][j][2]) || isMuxMacro(slotMapping.FFs[i][j][3])))
            //     continue;
            float oriDirectInternalRoute_slack =
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0], slotMapping.FFs[i][j][0]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1], slotMapping.FFs[i][j][1]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2], slotMapping.FFs[i][j][2]) +
                checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3], slotMapping.FFs[i][j][3]);
            float optDirectInternalRoute_slack = -100000000;
            int optimalOption = -1;
            for (int optionId = 0; optionId < 24; optionId++)
            {
                bool moveFFofMux = false;
                for (int checkFFId = 0; checkFFId < 4; checkFFId++)
                {
                    if (FFSwapOption[optionId][checkFFId] != checkFFId)
                    {
                        if (isMuxMacro(slotMapping.FFs[i][j][checkFFId]))
                        {
                            moveFFofMux = true;
                        }
                    }
                }
                if (moveFFofMux)
                    continue;

                float newDirectInternalRoute_slack =
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][0],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][0]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][1],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][1]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][2],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][2]]) +
                    checkDirectLUTFFConnect_slack(FF2LUT, slotMapping.LUTs[i][j][3],
                                                  slotMapping.FFs[i][j][FFSwapOption[optionId][3]]);
                if (newDirectInternalRoute_slack > optDirectInternalRoute_slack)
                {
                    optimalOption = optionId;
                    optDirectInternalRoute_slack = newDirectInternalRoute_slack;
                }
            }
            if (oriDirectInternalRoute_slack < optDirectInternalRoute_slack)
            {
                DesignInfo::DesignCell *tmpFF0 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][0]];
                DesignInfo::DesignCell *tmpFF1 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][1]];
                DesignInfo::DesignCell *tmpFF2 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][2]];
                DesignInfo::DesignCell *tmpFF3 = slotMapping.FFs[i][j][FFSwapOption[optimalOption][3]];
                slotMapping.FFs[i][j][0] = tmpFF0;
                slotMapping.FFs[i][j][1] = tmpFF1;
                slotMapping.FFs[i][j][2] = tmpFF2;
                slotMapping.FFs[i][j][3] = tmpFF3;
                continue;
            }
        }
    }

    if (determinedClusterInSite)
    {

        unsigned int FFCnt = 0;
        for (auto &CSFF : determinedClusterInSite->getFFControlSets())
            FFCnt += CSFF.getFFs().size();

        if (FFCnt != mappedFFs.size())
        {
            std::cout << "FFCnt: " << FFCnt << " mappedFFs.size():" << mappedFFs.size() << "\n";
            determinedClusterInSite->printMyself();
            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        std::cout << "i,k,j:" << i << "," << k << "," << j << ":\n";
                        if (slotMapping.LUTs[i][j][k])
                            std::cout << slotMapping.LUTs[i][j][k] << "\n";
                        if (slotMapping.FFs[i][j][k])
                            std::cout << slotMapping.FFs[i][j][k] << "\n";
                    }
                }
            }
            assert(FFCnt == mappedFFs.size());
        }
    }

    std::map<DesignInfo::DesignCell *, std::array<int, 3>> cell2slot;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (slotMapping.FFs[i][j][k])
                {
                    cell2slot[slotMapping.FFs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
                if (slotMapping.LUTs[i][j][k])
                {
                    cell2slot[slotMapping.LUTs[i][j][k]] = std::array<int, 3>({i, j, k});
                }
            }
        }
    }
    // Move FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                if (LUT2FF.find(slotMapping.LUTs[i0][j0][k0]) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[slotMapping.LUTs[i0][j0][k0]];
                    if (slotMapping.FFs[i0][j0][k0] == nullptr)
                    {
                        int halfCLBId0 = i0 * 2 + j0;
                        auto &CSFF0 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId0]];
                        int i1 = cell2slot[targetFF].at(0);
                        int j1 = cell2slot[targetFF].at(1);
                        int k1 = cell2slot[targetFF].at(2);
                        int halfCLBId1 = i1 * 2 + j1;
                        auto &CSFF1 =
                            determinedClusterInSite->getFFControlSets()[FFSwapOption[FFControlSetOrderId][halfCLBId1]];
                        if (CSFF0.compatibleWith(CSFF1.getCSId()) &&
                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, i0, j0))
                        {
                            bool compatible = true;
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][1 - j0][kk])
                                {
                                    if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
                                    {
                                        if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                            !DesignInfo::FFSRCompatible(
                                                slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getFFType(),
                                                targetFF->getControlSetInfo()->getFFType()))
                                        {
                                            compatible = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!compatible)
                                continue;

                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                            slotMapping.FFs[i0][j0][k0] = targetFF;
                            slotMapping.FFs[i1][j1][k1] = nullptr;
                            cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
                        }
                    }
                }
            }
        }
    }

    // Move LUT-FF to empty slot if possible to increase direct connect
    for (int i0 = 0; i0 < 2; i0++)
    {
        for (int j0 = 0; j0 < 2; j0++)
        {
            for (int k0 = 0; k0 < 4; k0++)
            {
                if (!slotMapping.LUTs[i0][j0][k0])
                    continue;
                auto targetLUT = slotMapping.LUTs[i0][j0][k0];
                if (slotMapping.LUTs[i0][1 - j0][k0])
                    continue;
                if (LUT2FF.find(targetLUT) != LUT2FF.end())
                {
                    auto targetFF = LUT2FF[targetLUT];
                    int i1 = cell2slot[targetFF].at(0);
                    int j1 = cell2slot[targetFF].at(1);
                    int k1 = cell2slot[targetFF].at(2);
                    int halfCLBId1 = i1 * 2 + j1;

                    if (i0 != i1 || j0 != j1 || k0 != k1)
                    {
                        bool optimized = false;
                        for (int ii = 0; ii < 2 && !optimized; ii++)
                        {
                            for (int jj = 0; jj < 2 && !optimized; jj++)
                            {
                                bool compatible = true;
                                if (targetLUT->isLUT6() && jj == 1)
                                    continue;
                                int halfCLBId0 = ii * 2 + jj;
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo())
                                        {
                                            if (slotMapping.FFs[ii][jj][kk]->getControlSetInfo()->getId() !=
                                                targetFF->getControlSetInfo()->getId())
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }
                                for (int kk = 0; kk < 4; kk++)
                                {
                                    if (slotMapping.FFs[ii][1 - jj][kk])
                                    {
                                        if (slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo())
                                        {
                                            if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                                !DesignInfo::FFSRCompatible(
                                                    slotMapping.FFs[ii][1 - jj][kk]->getControlSetInfo()->getFFType(),
                                                    targetFF->getControlSetInfo()->getFFType()))
                                            {
                                                compatible = false;
                                            }
                                        }
                                    }
                                }

                                if (compatible)
                                {
                                    for (int kk = 0; kk < 4; kk++)
                                    {
                                        if (!slotMapping.LUTs[ii][jj][kk] && !slotMapping.FFs[ii][jj][kk] &&
                                            CLBSiteLegality::canPlaceFF(slotMapping, targetFF, ii, jj))
                                        {
                                            // if (slotMapping.LUTs[ii][1 - jj][kk])
                                            //     continue;
                                            if (targetLUT->isLUT6())
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                    continue;
                                            }
                                            else
                                            {
                                                if (slotMapping.LUTs[ii][1 - jj][kk] &&
                                                    slotMapping.LUTs[ii][1 - jj][kk]->isLUT6())
                                                    continue;
                                                if (slotMapping.LUTs[ii][1 - jj][kk])
                                                {
                                                    int pairPinNum =
                                                        getPairPinNum(targetLUT, slotMapping.LUTs[ii][1 - jj][kk]);
                                                    if (pairPinNum > 5)
                                                        continue;
                                                }
                                            }

                                            slotMapping.LUTs[i0][j0][k0] = nullptr;
                                            slotMapping.FFs[i1][j1][k1] = nullptr;
                                            assert(!slotMapping.LUTs[ii][jj][kk]);
                                            assert(!slotMapping.FFs[ii][jj][kk]);
                                            slotMapping.LUTs[ii][jj][kk] = targetLUT;
                                            slotMapping.FFs[ii][jj][kk] = targetFF;

                                            if (FFSwapOption[FFControlSetOrderId][halfCLBId1] !=
                                                FFSwapOption[FFControlSetOrderId][halfCLBId0])
                                                determinedClusterInSite->moveFFFromCS1ToCS0(
                                                    targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId1],
                                                    FFSwapOption[FFControlSetOrderId][halfCLBId0]);
                                            cell2slot[targetFF] = std::array<int, 3>({ii, jj, kk});
                                            cell2slot[targetLUT] = std::array<int, 3>({ii, jj, kk});
                                            optimized = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // swap LUTs between LUT slots
    for (auto pair : LUT2FF)
    {
        auto targetLUT = pair.first;
        auto targetFF = pair.second;
        int luti = cell2slot[targetLUT].at(0);
        int lutj = cell2slot[targetLUT].at(1);
        int lutk = cell2slot[targetLUT].at(2);
        int ffi = cell2slot[targetFF].at(0);
        int ffj = cell2slot[targetFF].at(1);
        int ffk = cell2slot[targetFF].at(2);
        int halfCLBId_Ori = ffi * 2 + ffj;

        if (slotMapping.LUTs[luti][1 - lutj][lutk])
            continue;

        bool optimized = false;
        if (!(luti == ffi && lutj == ffj && lutk == ffk))
        {
            for (int i0 = 0; i0 < 2 && !optimized; i0++)
            {
                for (int k0 = 0; k0 < 4 && !optimized; k0++)
                {
                    if (isMuxMacro(slotMapping.LUTs[i0][0][k0]) || isMuxMacro(slotMapping.FFs[i0][0][k0]) ||
                        isMuxMacro(slotMapping.LUTs[i0][1][k0]) || isMuxMacro(slotMapping.FFs[i0][1][k0]))
                        continue;

                    if (!(slotMapping.FFs[i0][0][k0] || slotMapping.FFs[i0][1][k0]))
                    {
                        for (int j0 = 0; j0 < 2 && !optimized; j0++)
                        {
                            if (targetLUT->isLUT6() && j0 == 1)
                                continue;

                            bool compatible = true;

                            int halfCLBId_target = i0 * 2 + j0;
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][j0][kk])
                                {
                                    if (slotMapping.FFs[i0][j0][kk]->getControlSetInfo())
                                    {
                                        if (slotMapping.FFs[i0][j0][kk]->getControlSetInfo()->getId() !=
                                            targetFF->getControlSetInfo()->getId())
                                        {
                                            compatible = false;
                                        }
                                    }
                                }
                            }
                            for (int kk = 0; kk < 4; kk++)
                            {
                                if (slotMapping.FFs[i0][1 - j0][kk])
                                {
                                    if (slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo())
                                    {
                                        if (!DesignInfo::compatibleClockAndReset(*slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo(), *targetFF->getControlSetInfo()) ||
                                            !DesignInfo::FFSRCompatible(
                                                slotMapping.FFs[i0][1 - j0][kk]->getControlSetInfo()->getFFType(),
                                                targetFF->getControlSetInfo()->getFFType()))
                                        {
                                            compatible = false;
                                        }
                                    }
                                }
                            }

                            if (compatible)
                            {

                                auto LUT0 = slotMapping.LUTs[i0][0][k0];
                                auto LUT1 = slotMapping.LUTs[i0][1][k0];

                                slotMapping.LUTs[i0][0][k0] = nullptr;
                                slotMapping.LUTs[i0][1][k0] = nullptr;

                                slotMapping.LUTs[luti][lutj][lutk] = nullptr;
                                slotMapping.FFs[ffi][ffj][ffk] = nullptr;

                                slotMapping.LUTs[i0][j0][k0] = targetLUT;
                                slotMapping.FFs[i0][j0][k0] = targetFF;

                                if (FFSwapOption[FFControlSetOrderId][halfCLBId_Ori] !=
                                    FFSwapOption[FFControlSetOrderId][halfCLBId_target])
                                    determinedClusterInSite->moveFFFromCS1ToCS0(
                                        targetFF, FFSwapOption[FFControlSetOrderId][halfCLBId_Ori],
                                        FFSwapOption[FFControlSetOrderId][halfCLBId_target]);
                                cell2slot[targetFF] = std::array<int, 3>({i0, j0, k0});
                                cell2slot[targetLUT] = std::array<int, 3>({i0, j0, k0});

                                slotMapping.LUTs[luti][0][lutk] = LUT0;
                                slotMapping.LUTs[luti][1][lutk] = LUT1;

                                if (LUT0)
                                    cell2slot[LUT0] = std::array<int, 3>({luti, 0, lutk});
                                if (LUT1)
                                    cell2slot[LUT1] = std::array<int, 3>({luti, 1, lutk});

                                optimized = true;

                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if(ffQBlockedMask && !assignAccessibleFFSlots())return;
    if(!chooseLocalPinMapping())return;

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    float clockPeriod = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph()->getClockPeriod();
    float directConnectCnt = 0;
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                if (checkDirectLUTFFConnect(FF2LUT, slotMapping.LUTs[i][j][k], slotMapping.FFs[i][j][k]))
                {
                    auto srcCell = slotMapping.LUTs[i][j][k];
                    unsigned int srcCellId = srcCell->getCellId();
                    auto srcNode = timingNodes[srcCellId];
                    // int succPathLen = srcNode->getLongestPathLength();
                    if (srcNode->getForwardLevel() < 0)
                        continue;
                    float slack =
                        (srcNode->getLatestInputArrival() - srcNode->getRequiredArrivalTime()) / clockPeriod + 20;
                    if (j == 0)
                        slack += 0.025;
                    directConnectCnt += slack;
                }
            }
        }
    }
    if (directConnectCnt > best_DirectConnect)
    {
        best_SlotMapping = slotMapping;
        best_mappedCells = mappedCells;
        best_mappedLUTs = mappedLUTs;
        best_mappedFFs = mappedFFs;
        best_DirectConnect = directConnectCnt;
    }
}

void ParallelCLBPacker::PackingCLBSite::finalMapToSlotsForCommonLUTFFInSite()
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(!checkIsPrePackedSite() && !checkIsMuxSite());
    assert(fixedPairedLUTs.size() == 0 && conflictLUTs.size() == 0);
    assert(determinedClusterInSite->getSingleLUTs().size() + determinedClusterInSite->getPairedLUTs().size() <= 8);

    for (int i = 0; i < 24; i++)
        finalMapToSlotsForCommonLUTFFInSite(i);

    requireLegalSlotMapping("complete-site");
    slotMapping = best_SlotMapping;
    mappedCells = best_mappedCells;
    mappedLUTs = best_mappedLUTs;
    mappedFFs = best_mappedFFs;
    moveLUTToLUT6Slot();
}

void ParallelCLBPacker::PackingCLBSite::moveLUTToLUT6Slot()
{
    AMF_PROFILE_FUNCTION("final_packing");
    if (!determinedClusterInSite)
        return;

    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            if (!slotMapping.LUTs[i][0][k] && slotMapping.LUTs[i][1][k])
            {
                if (determinedClusterInSite->getSingleLUTs().find(slotMapping.LUTs[i][1][k]) !=
                    determinedClusterInSite->getSingleLUTs().end())
                {
                    if (isDirectConnectLUTFF(slotMapping.LUTs[i][1][k], slotMapping.FFs[i][1][k]))
                        continue;
                    slotMapping.LUTs[i][0][k] = slotMapping.LUTs[i][1][k];
                    slotMapping.LUTs[i][1][k] = nullptr;
                    if(!chooseLocalPinMapping()) {
                        slotMapping.LUTs[i][1][k]=slotMapping.LUTs[i][0][k];
                        slotMapping.LUTs[i][0][k]=nullptr;
                    }
                }
            }
        }
    }
}

void ParallelCLBPacker::PackingCLBSite::mapLUTRAMRelatedCellsToSlots(PlacementInfo::PlacementMacro *_LUTRAMMacro, unsigned srlLimit)
{
    AMF_PROFILE_FUNCTION("final_packing");
    if(std::find(LUTRAMMacros.begin(),LUTRAMMacros.end(),_LUTRAMMacro)!=LUTRAMMacros.end())
        throw std::runtime_error("Duplicate prepacked MCLB owner");
    auto isHalfBEL = [](const std::string &bel) {
        return bel.size()==5 && bel[0]>='A' && bel[0]<='H' && bel.substr(1)=="5LUT";
    };
    const auto &fixedCells = _LUTRAMMacro->getFixedCellInfoVec();
    bool hasSRL5 = std::any_of(fixedCells.begin(),fixedCells.end(),[&](const auto &f) {
        return f.cell->originallyIsShifter() && isHalfBEL(f.BELName);
    });
    auto isRAM=[](PlacementInfo::PlacementMacro *owner) {
        return owner->getCells().size()==1 && ram_site_sharing::supported(owner->getCells()[0]->getOriCellType());
    };
    bool sharedRAM=isRAM(_LUTRAMMacro) && std::all_of(LUTRAMMacros.begin(),LUTRAMMacros.end(),isRAM);
    if((!LUTRAMMacros.empty() || hasSRL5) && !sharedRAM) {
        std::array<DesignInfo::DesignCell*,8> low{}, high{};
        unsigned pending=0;
        std::set<std::string> bels;
        std::set<DesignInfo::DesignCell *> assigned;
        unsigned srlCount=0;
        auto owners=LUTRAMMacros;owners.push_back(_LUTRAMMacro);
        for(auto macro:owners) {
            if(macro->getFixedCellInfoVec().size()!=macro->getCells().size())
                throw std::runtime_error("Shared SRL site lacks complete BEL assignment");
            for(auto cell:macro->getCells()) {
                if(cell->originallyIsShifter()) ++srlCount;
                else if(cell->getOriCellType()!=DesignInfo::CellType_MUXF7)
                    throw std::runtime_error("Non-SRL macro admitted to shared SRL site");
            }
            for(const auto &fixed:macro->getFixedCellInfoVec()) {
                if(std::find(macro->getCells().begin(),macro->getCells().end(),fixed.cell)==macro->getCells().end() ||
                   !assigned.insert(fixed.cell).second)
                    throw std::runtime_error("Shared SRL BEL assignment has wrong or duplicate member");
                const auto &bel=fixed.BELName;
                if (isHalfBEL(bel)) {
                    if (srlLimit!=8 || CLBSite->getSiteType()!="SLICEM" ||
                        placementInfo->getDeviceInfo()->getDeviceName()!="U250" ||
                        macro->getCells().size()!=1 ||
                        placementInfo->srl16PairKey(fixed.cell).empty())
                        throw std::runtime_error("Shared SRL BEL incompatible with half-row member");
                    low[bel[0]-'A']=fixed.cell;
                } else if(srl_site_sharing::shiftedBEL(bel,0).empty() ||
                   (fixed.cell->originallyIsShifter() ? bel.size()!=5 : bel.size()!=8))
                    throw std::runtime_error("Shared SRL BEL incompatible with member");
                else if (bel.size()==5) high[bel[0]-'A']=fixed.cell;
                if(!bels.insert(bel).second) throw std::runtime_error("Shared SRL BEL collision");
            }
        }
        for (unsigned row=0; row<8; ++row) {
            if (!low[row]) continue;
            if (!high[row]) pending |= 1u<<row;
            else if (placementInfo->srl16PairKey(high[row])!=placementInfo->srl16PairKey(low[row]))
                throw std::runtime_error("Incompatible shared SRL16E row inputs");
            if (row%2==0 && bels.count("F7MUX_"+std::string(1,'A'+row)+char('B'+row)))
                throw std::runtime_error("Shared SRL16E row conflicts with F7 selector");
        }
        if(srlLimit!=8 && srlCount>srlLimit) throw std::runtime_error("Shared SLICEM exceeds configured SRL limit " + std::to_string(srlLimit));
        pendingSRL5Rows=pending;
    }
    if(!LUTRAMMacro) LUTRAMMacro=_LUTRAMMacro;
    LUTRAMMacros.push_back(_LUTRAMMacro);
    isLUTRAMSite = true;
}

void ParallelCLBPacker::PackingCLBSite::mapCarryRelatedCellsToSlots(PlacementInfo::PlacementMacro *_CARRYChain,
                                                                    float siteOffset)
{
    AMF_PROFILE_FUNCTION("final_packing");
    isCarrySite = true;
    CARRYChain = _CARRYChain;
    CARRYChainSiteOffset = siteOffset;

    mappedCells.clear();
    mappedLUTs.clear();
    mappedFFs.clear();

    float siteHeight = 1.0;
    float lowerBound = siteHeight * CARRYChainSiteOffset - (1e-2);
    float uppwerBound = siteHeight * CARRYChainSiteOffset + (1e-2);
    for (auto curCell : CARRYChain->getCells())
    {
        float offsetY = CARRYChain->getCellOffsetYInMacro(curCell);
        if (offsetY > lowerBound && offsetY < uppwerBound)
        {
            if (curCell->isCarry())
            {
                carryCell = curCell;
                auto curCarry = curCell;
                slotMapping.Carry = curCarry;
                mappedCells.insert(curCarry);
                for (DesignInfo::DesignPin *pinBeDriven : curCarry->getInputPins())
                {
                    if (pinBeDriven->isUnconnected())
                        continue;
                    if (!pinBeDriven->getDriverPin()) // pin connect to GND/VCC which has no specifc driver pin
                        continue;

                    if (pinBeDriven->getRefPinName().find("S[") == 0)
                    {
                        if (placementInfo->getPlacementUnitByCell(pinBeDriven->getDriverPin()->getCell()) ==
                                CARRYChain &&
                            pinBeDriven->getDriverPin()->getCell()->isLUT())
                        {
                            char SPinCellId =
                                pinBeDriven->getRefPinName()[pinBeDriven->getRefPinName().find("[") + 1] - '0';
                            slotMapping.LUTs[SPinCellId / 4][0][SPinCellId % 4] =
                                pinBeDriven->getDriverPin()->getCell();
                            mappedCells.insert(pinBeDriven->getDriverPin()->getCell());
                            mappedLUTs.insert(pinBeDriven->getDriverPin()->getCell());
                            // char LUTCode = SPinCellId + 'A';
                            // std::string LUTSiteName = std::string(1, LUTCode) + "6LUT";
                            // outfile0 << "  " << pinBeDriven->getDriverPin()->getCell()->getName() << " "
                            //          << CLBSite->getName() << "/" + LUTSiteName << "\n";
                        }
                    }
                    else if (pinBeDriven->getRefPinName().find("DI[") == 0)
                    {
                        if (placementInfo->getPlacementUnitByCell(pinBeDriven->getDriverPin()->getCell()) ==
                                CARRYChain &&
                            CarryLUTRules::canUseDirectDI(pinBeDriven->getDriverPin()->getCell(), pinBeDriven->getNet()))
                        {
                            char DIPinCellId =
                                pinBeDriven->getRefPinName()[pinBeDriven->getRefPinName().find("[") + 1] - '0';
                            slotMapping.LUTs[DIPinCellId / 4][1][DIPinCellId % 4] =
                                pinBeDriven->getDriverPin()->getCell();
                            mappedCells.insert(pinBeDriven->getDriverPin()->getCell());
                            mappedLUTs.insert(pinBeDriven->getDriverPin()->getCell());
                            // char LUTCode = DIPinCellId + 'A';
                            // std::string LUTSiteName = std::string(1, LUTCode) + "5LUT";
                            // outfile0 << "  " << pinBeDriven->getDriverPin()->getCell()->getName() << " "
                            //          << CLBSite->getName() << "/" + LUTSiteName << "\n";
                        }
                    }
                }
                std::vector<std::string> checkFFRefPins{"O[", "CO["};
                for (DesignInfo::DesignPin *driverPin : curCarry->getOutputPins())
                {
                    if (driverPin->isUnconnected())
                        continue;
                    DesignInfo::DesignNet *curOutputNet = driverPin->getNet();
                    bool findMatchedInputPin = false;
                    for (auto patternPin : checkFFRefPins)
                    {
                        if (driverPin->getRefPinName().find(patternPin) == 0)
                        {
                            findMatchedInputPin = true;
                            break;
                        }
                    }

                    if (findMatchedInputPin)
                    {
                        int FFcnt = 0;
                        DesignInfo::DesignCell *theFF = nullptr;
                        for (auto pinBeDriven : curOutputNet->getPinsBeDriven())
                        {
                            if (pinBeDriven->getCell()->isFF())
                            {
                                FFcnt++;
                                if (pinBeDriven->getRefPinName().find("D") != std::string::npos)
                                {
                                    if (placementInfo->getPlacementUnitByCell(pinBeDriven->getCell()) == CARRYChain)
                                        theFF = pinBeDriven->getCell();
                                }
                            }
                        }
                        if (FFcnt == 1 && theFF)
                        {
                            char FFPinCellId =
                                driverPin->getRefPinName()[driverPin->getRefPinName().find("[") + 1] - '0';
                            if (driverPin->getRefPinName().find("CO[") != std::string::npos)
                            {
                                assert(!slotMapping.FFs[FFPinCellId / 4][1][FFPinCellId % 4]);
                                slotMapping.FFs[FFPinCellId / 4][1][FFPinCellId % 4] = theFF;
                                mappedCells.insert(theFF);
                                mappedFFs.insert(theFF);
                                // std::string FFSiteName = std::string(1, FFCode) + "FF2";
                                // outfile0 << "  " << theFF->getName() << " " << CLBSite->getName() << "/" + FFSiteName
                                //          << "\n";
                            }
                            else if (driverPin->getRefPinName().find("O[") != std::string::npos)
                            {
                                assert(!slotMapping.FFs[FFPinCellId / 4][0][FFPinCellId % 4]);
                                slotMapping.FFs[FFPinCellId / 4][0][FFPinCellId % 4] = theFF;
                                mappedCells.insert(theFF);
                                mappedFFs.insert(theFF);
                                // std::string FFSiteName = std::string(1, FFCode) + "FF";
                                // outfile0 << "  " << theFF->getName() << " " << CLBSite->getName() << "/" + FFSiteName
                                //          << "\n";
                            }
                        }
                    }
                }
            }

            if (curCell->isVirtualCell())
            {
                assert(mappedCells.find(curCell) == mappedCells.end());
                if (curCell->isLUT())
                {
                    assert(curCell->isLUT6());
                    assert(curCell->getName().find('(') != std::string::npos);
                    int slotId = curCell->getName()[curCell->getName().find('(') - 1] - '0';
                    assert(slotId < 8 && slotId >= 0);
                    assert(!slotMapping.LUTs[slotId / 4][0][slotId % 4]);
                    mappedCells.insert(curCell);
                    mappedLUTs.insert(curCell);
                    slotMapping.LUTs[slotId / 4][0][slotId % 4] = curCell;
                }
                else
                {
                    assert(curCell->isFF());
                    assert(curCell->getName().find('(') != std::string::npos);
                    int strOffset = curCell->getName().find('(');
                    int slotId = curCell->getName()[strOffset - 1] - '0';
                    assert(curCell->getName()[strOffset - 2] == '2' || curCell->getName()[strOffset - 2] == 'F');
                    int oddCLB = curCell->getName()[strOffset - 2] == '2';
                    assert(slotId < 8 && slotId >= 0);
                    assert(!slotMapping.FFs[slotId / 4][oddCLB][slotId % 4]);
                    mappedCells.insert(curCell);
                    mappedFFs.insert(curCell);
                    slotMapping.FFs[slotId / 4][oddCLB][slotId % 4] = curCell;
                }
            }
        }
    }

    fixedPairedLUTs.clear();
    conflictLUTs.clear();
    fixedLUTsInPairs.clear();

    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 4; k++)
        {
            if (slotMapping.LUTs[i][0][k] && !slotMapping.LUTs[i][1][k])
            {
                conflictLUTs.insert(slotMapping.LUTs[i][0][k]);
            }
            else if (slotMapping.LUTs[i][1][k] && !slotMapping.LUTs[i][0][k])
            {
                conflictLUTs.insert(slotMapping.LUTs[i][1][k]);
            }
            else if (slotMapping.LUTs[i][1][k] && slotMapping.LUTs[i][0][k])
            {
                if (slotMapping.LUTs[i][1][k]->getCellId() < slotMapping.LUTs[i][0][k]->getCellId())
                {
                    fixedPairedLUTs.emplace(slotMapping.LUTs[i][1][k], slotMapping.LUTs[i][0][k]);
                    fixedLUTsInPairs.insert(slotMapping.LUTs[i][1][k]);
                    fixedLUTsInPairs.insert(slotMapping.LUTs[i][0][k]);
                }
                else
                {
                    fixedPairedLUTs.emplace(slotMapping.LUTs[i][0][k], slotMapping.LUTs[i][1][k]);
                    fixedLUTsInPairs.insert(slotMapping.LUTs[i][1][k]);
                    fixedLUTsInPairs.insert(slotMapping.LUTs[i][0][k]);
                }
            }
        }
    if(macroLocalRoutesEnabled &&
       (carryRouteOwner!=CARRYChain || carryRouteOwnerSize!=CARRYChain->getCells().size() ||
        carryRouteOffset!=CARRYChainSiteOffset || carryRouteQMask!=ffQBlockedMask)) {
        carryRouteOwner=CARRYChain;carryRouteOwnerSize=CARRYChain->getCells().size();
        carryRouteOffset=CARRYChainSiteOffset;carryRouteQMask=ffQBlockedMask;
        carryFixedRouteLUTs=conflictLUTs;
        carryFixedRoutesValid=MacroLocalRoutes::compatible(slotMapping);
    }
}
// Sparse masks affect only certified sites. Ordinary packing takes its original
// path. Keep each FF in its physical clock/reset/CE bank; do not alter macros.
bool ParallelCLBPacker::PackingCLBSite::assignAccessibleFFSlots()
{
    AMF_PROFILE_SCOPE("native_pin_packing_assignment", "choose accessible FF BELs before candidate commit");
    auto candidate=slotMapping;
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)for(int k=0;k<4;++k) {
        auto ff=candidate.FFs[i][j][k];
        if(!ff||ff->isVirtualCell()||!(ffQBlockedMask&FfQPinMask::bit(i,j,k)))continue;
        if(isCarryMacro(ff)||isMuxMacro(ff))return false;
        int target=-1;
        for(int q=0;q<4;++q)
            if(!candidate.FFs[i][j][q] && !(ffQBlockedMask&FfQPinMask::bit(i,j,q))) {target=q;break;}
        if(target<0)return false;
        candidate.FFs[i][j][target]=ff;candidate.FFs[i][j][k]=nullptr;
    }
    slotMapping=candidate;
    return true;
}
void ParallelCLBPacker::PackingCLBSite::verifyFfQAccess() const
{
    if(!chooseLocalPinMapping())throw std::runtime_error("Local pin access rejected at packing commit: "+CLBSite->getName());
    if(!ffQBlockedMask)return;
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)for(int k=0;k<4;++k) {
        auto ff=slotMapping.FFs[i][j][k];
        if(ff&&!ff->isVirtualCell()&&(ffQBlockedMask&FfQPinMask::bit(i,j,k)))
            throw std::runtime_error("FF Q access rejected at packing commit: "+CLBSite->getName()+"/"+ff->getName());
    }
}
void ParallelCLBPacker::PackingCLBSite::requireLegalSlotMapping(const char *kind) const
{
    if (best_DirectConnect > -99999999) return;
    std::ostringstream detail;
    detail << "AMF_NO_LEGAL_SLOT_MAPPING site=" << CLBSite->getName()
           << " kind=" << kind << " PU IDs:";
    if (!determinedClusterInSite) throw std::runtime_error(detail.str() + " no cluster");
    for (auto pu : determinedClusterInSite->getPUs()) detail << ' ' << pu->getId();
    for (const auto &bank : determinedClusterInSite->getFFControlSets())
    {
        detail << " bank(" << bank.getCSId() << "):";
        for (auto ff : bank.getFFs()) detail << ' ' << ff->getCellId();
    }
    throw std::runtime_error(detail.str());
}

bool ParallelCLBPacker::PackingCLBSite::carryCandidateUsesFixedLUTsOnly(PackingCLBCluster *candidate) const
{
    if(!isCarrySite || !carryFixedRoutesValid || !CARRYChain || carryRouteOwner!=CARRYChain ||
       carryRouteOwnerSize!=CARRYChain->getCells().size() || carryRouteOffset!=CARRYChainSiteOffset ||
       carryRouteQMask!=ffQBlockedMask || candidate->getNumMuxes())return false;
    const auto &single=candidate->getSingleLUTs();const auto &paired=candidate->getPairedLUTs();
    if(single.size()+paired.size()*2!=carryFixedRouteLUTs.size())return false;
    for(auto lut:single)if(!carryFixedRouteLUTs.count(lut))return false;
    for(const auto &pair:paired)
        if(!carryFixedRouteLUTs.count(pair.first)||!carryFixedRouteLUTs.count(pair.second))return false;
    // Adding only ordinary FFs cannot consume a fixed LUT's output wire or
    // a fixed/virtual macro BEL. Existing FF-bank/pair legality still runs.
    return true;
}

bool ParallelCLBPacker::PackingCLBSite::macroLocalRoutesCompatible() const
{
    return !macroLocalRoutesEnabled || MacroLocalRoutes::compatible(slotMapping);
}

bool ParallelCLBPacker::PackingCLBSite::chooseLocalPinMapping(
    amf_native::LutPinAssignment::Result *chosen) const
{
    if(!macroLocalRoutesCompatible())return false;
    if(!placementInfo->localPinAccess.enabled)return true;
    std::vector<amf_native::LocalPlacementAccess::Slot> slots;
    for(int half=0;half<2;++half)for(int row=0;row<4;++row)for(int lane=0;lane<2;++lane) {
        auto lut=slotMapping.LUTs[half][lane][row];
        char letter='A'+half*4+row;
        if(lut&&!lut->isVirtualCell()) {
            bool internal=true;
            for(auto output:lut->getOutputPins()) {
                if(output->isUnconnected()||!output->getNet())continue;
                int outputLane=lane;
                if(lut->getOriCellType()==DesignInfo::CellType_LUT6_2)
                    outputLane=output->getRefPinName()=="O5"?1:0;
                for(auto sink:output->getNet()->getPinsBeDriven()) {
                    auto target=sink->getCell();auto ref=sink->getRefPinName();
                    bool direct=target==slotMapping.FFs[half][outputLane][row]&&ref=="D";
                    direct|=target==slotMapping.Carry &&
                        ref==std::string(outputLane?"DI[":"S[")+std::to_string(half*4+row)+"]";
                    direct|=outputLane==0 && target==slotMapping.MuxF7[half][row/2] &&
                        ref==(row%2?"I0":"I1");
                    if(!direct)internal=false;
                }
            }
            slots.push_back({lut,std::string(1,letter)+(lane?"5LUT":"6LUT"),internal});
        }
        auto ff=slotMapping.FFs[half][lane][row];
        if(ff&&!ff->isVirtualCell())slots.push_back({ff,std::string(1,letter)+(lane?"FF2":"FF"),false});
    }
    auto result=placementInfo->localPinAccess.slice(CLBSite->getName(),slots);
    if(chosen)*chosen=std::move(result);
    return chosen?chosen->feasible:result.feasible;
}

// In a one-way relocation only the destination ends with addPU(). Test the
// final source membership before publishing either side. Two-way exchanges
// already validate both completed memberships through their final addPU().
bool ParallelCLBPacker::PackingCLBSite::canRelocateFrom(PlacementInfo::PlacementUnit *pu)
{
    if (!macroLocalRoutesEnabled || (!checkIsCarrySite() && !checkIsMuxSite()))
        return true;

    PackingCLBCluster remaining(determinedClusterInSite);
    remaining.removePUToConstructDetCluster(pu);
    return localCandidateAllows(&remaining);
}

bool ParallelCLBPacker::PackingCLBSite::localCandidateAllows(PackingCLBCluster *candidate)
{
    if (checkIsLUTRAMSite()) return candidate->refreshMemoryFFMapping();
    const bool intrinsic=macroLocalRoutesEnabled && (checkIsCarrySite() || candidate->getNumMuxes()>0);
    const bool certified=placementInfo->localPinAccess.enabled &&
        placementInfo->localPinAccess.database.sites.count(CLBSite->getName());
    if(!intrinsic && !certified)return true;
    if(intrinsic && !placementInfo->localPinAccess.enabled && !ffQBlockedMask &&
       carryCandidateUsesFixedLUTsOnly(candidate)) {
        ++intrinsicCarryFastAccepts;
        return true;
    }
    ++intrinsicSlowProbes;
    AMF_PROFILE_SCOPE("native_local_candidate", "test candidate slot mappings before packing commit");
    auto previousCluster=determinedClusterInSite;
    auto previousSlots=slotMapping,previousBest=best_SlotMapping;
    auto previousCells=mappedCells,previousLuts=mappedLUTs,previousFfs=mappedFFs;
    auto previousBestCells=best_mappedCells,previousBestLuts=best_mappedLUTs,previousBestFfs=best_mappedFFs;
    auto previousPairs=fixedPairedLUTs;auto previousFixed=fixedLUTsInPairs,previousConflicts=conflictLUTs;
    auto previousCarryCell=carryCell;
    float previousScore=best_DirectConnect;
    auto restore=[&] {
        determinedClusterInSite=previousCluster;slotMapping=previousSlots;best_SlotMapping=previousBest;
        mappedCells=previousCells;mappedLUTs=previousLuts;mappedFFs=previousFfs;
        best_mappedCells=previousBestCells;best_mappedLUTs=previousBestLuts;best_mappedFFs=previousBestFfs;
        fixedPairedLUTs=previousPairs;fixedLUTsInPairs=previousFixed;conflictLUTs=previousConflicts;
        best_DirectConnect=previousScore;carryCell=previousCarryCell;
    };
    try {
        determinedClusterInSite=candidate;best_DirectConnect=-100000000;
        // Reuse the packer's legal control-bank/macro enumeration. No tentative
        // coordinates or cells escape this probe.
        for(int order=0;order<24;++order) {
            // Slot enumeration can rearrange control sets. Probe a private
            // cluster per order so a rejected candidate is never modified.
            PackingCLBCluster trial(candidate);
            determinedClusterInSite=&trial;
            if(checkIsCarrySite())finalMapToSlotsForCarrySite(order);
            else if(checkIsMuxSite())greedyMapMuxForCommonLUTFFInSite(order);
            else finalMapToSlotsForCommonLUTFFInSite(order);
            if(best_DirectConnect>-99999999)break;
        }
        if (best_DirectConnect <= -99999999 && candidate->getNumMuxes() && !checkIsCarrySite())
        {
            determinedClusterInSite = candidate;
            // No optimization here: the bank assignment and candidate stay untouched.
            for (int order = 0; order < 24 && best_DirectConnect <= -99999999; ++order)
                greedyMapMuxForCommonLUTFFInSite(order, false);
        }
        bool accepted=best_DirectConnect>-99999999;restore();return accepted;
    } catch(...) {restore();throw;}
}

void ParallelCLBPacker::PackingCLBSite::recordMemoryPins(DesignInfo::DesignCell *memory)
{
    if (!memoryNets) memoryNets.reset(new MemoryFFSiteNets);
    for (auto pin : memory->getInputPins()) {
        auto net = pin->getNet();
        if (net && !net->checkIsPowerNet()) memoryNets->inputs.insert(net);
    }
    for (auto pin : memory->getOutputPins()) {
        auto net = pin->getNet();
        if (net && !net->checkIsPowerNet() && net->getDriverPins().size()==1 &&
            net->getDriverPins().front()==pin) memoryNets->outputs.insert(net);
    }
}

// A RAM read output and its same-row FF must use the same clock edge.
// Cache the output rows during site setup; candidate checks only look up D.
void ParallelCLBPacker::PackingCLBSite::recordRAMReadClock(
    DesignInfo::DesignCell *memory, unsigned row, const std::string &port)
{
    for (auto pin : memory->getOutputPins()) {
        if (pin->getRefPinName() != port || !pin->getNet()) continue;
        auto &entry = memoryNets->ramReadClocks[pin->getNet()];
        for (auto input : memory->getInputPins())
            if (input->getRefPinName() == "WCLK") {entry.clock = input->getNet(); break;}
        entry.inverted = memory->getClockInverted();
        entry.slots |= MemoryFFRules::slotBit(row, 0) | MemoryFFRules::slotBit(row, 1);
        break;
    }
}

unsigned ParallelCLBPacker::PackingCLBSite::ramReadClockSlots(
    DesignInfo::DesignCell *ff, DesignInfo::DesignNet *data) const
{
    auto found = memoryNets->ramReadClocks.find(data);
    if (found == memoryNets->ramReadClocks.end()) return 65535u;
    const auto &ram = found->second;
    auto control = ff->getControlSetInfo();
    if (control && ram.clock && control->getCLK() == ram.clock &&
        control->getClockInverted() == ram.inverted) return 65535u;
    // Preserve all other rows, including independent FF bypass inputs.
    return 65535u & ~ram.slots;
}

void ParallelCLBPacker::PackingCLBSite::addNearbySRLConsumers()
{
    const auto &owners = getCandidateOwners();
    int siteCRX, siteCRY;
    placementInfo->getDeviceInfo()->getClockRegionByLocation(CLBSite->X(), CLBSite->Y(), siteCRX, siteCRY);
    for (auto cell : memoryNets->directSRLFFs) {
        auto pu = placementInfo->getPlacementUnitByCell(cell);
        if (neighborPUs.count(pu) || pu->isFixed() || pu->isPacked() || !withinSRLRewardRadius(pu) ||
            (owners[pu->getId()] && owners[pu->getId()] != this) || !acceptsMemoryFFPU(pu))
            continue;
        int crX, crY;
        placementInfo->getDeviceInfo()->getClockRegionByLocation(pu->X(), CLBSite->Y(), crX, crY);
        if (crX == siteCRX || placementInfo->boundaryClusteringEnabled())
            neighborPUs.insert(pu);
    }
}

unsigned ParallelCLBPacker::PackingCLBSite::countDirectMemoryPairMembers(PlacementInfo::PlacementUnit *pu) const
{
    auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu);
    if (!macro || macro->getMacroType()!=PlacementInfo::PlacementMacro::PlacementMacroType_FFFFPair ||
        macro->getCells().size()!=2 || !memoryNets) return 0;
    unsigned members = 0;
    for (auto ff : macro->getCells()) {
        // SRL.Q -> FF.D is scored from the occupied physical slot, once per lane.
        if (hasSRLRowOutputs()) {
            bool srlData = false;
            for (auto pin : ff->getInputPins())
                if (pin->getRefPinName() == "D" && memoryNets->srlOutputSlots.count(pin->getNet()))
                    srlData = true;
            if (srlData) continue;
        }
        bool direct = false;
        for (auto pin : ff->getInputPins())
            if (memoryNets->outputs.count(pin->getNet())) {direct=true; break;}
        if (!direct) {
            for (auto pin : ff->getOutputPins())
                if (pin->getRefPinName()=="Q" && memoryNets->inputs.count(pin->getNet()) &&
                    pin->getNet()->getDriverPins().size()==1 && pin->getNet()->getDriverPins().front()==pin) {
                    direct=true;
                    break;
                }
        }
        if (direct) ++members;
    }
    return members;
}

void ParallelCLBPacker::PackingCLBSite::prepareSharedRAMSite()
{
    using namespace ram_site_sharing;
    std::sort(LUTRAMMacros.begin(),LUTRAMMacros.end(),[](Macro *a,Macro *b){return a->getId()<b->getId();});
    std::vector<Unit> units;
    for (auto macro:LUTRAMMacros) {
        Unit unit;
        if (!describe(macro,placementInfo,unit) || macro->isLocked())
            throw std::runtime_error("Invalid shared RAM site owner: "+CLBSite->getName());
        units.push_back(unit);
    }
    std::vector<std::string> anchors;
    if (!assign(units,anchors)) throw std::runtime_error("Incompatible shared RAM controls or rows");
    RAMSlotProfile profile;
    unsigned occupied=0;
    for (unsigned i=0;i<units.size();++i) {
        auto macro=units[i].macro;
        auto cell=macro->getCells().front();
        const auto &fixed=macro->getFixedCellInfoVec();
        if (fixed.size()!=1 || fixed[0].cell!=cell || fixed[0].BELName!=anchors[i])
            throw std::runtime_error("Shared RAM legalizer and packing BEL assignments disagree");
        int row=anchors[i][0]-'A';
        auto local=[&](int r,int lane,const std::string &port) {
            for (auto pin:cell->getOutputPins()) if (pin->getRefPinName()==port)
                profile.local[(r/4)*8+4*lane+r%4]=pin->getNet();
            recordRAMReadClock(cell, r, port);
        };
        recordMemoryPins(cell);
        if (units[i].type==DesignInfo::CellType_RAM32X1D) {
            occupied|=3u<<row;
            int lane=anchors[i][1]=='6'?1:0;
            local(row,lane,"DPO");local(row+1,lane,"SPO");
        } else {
            occupied|=15u<<(row-3);
            if (units[i].type==DesignInfo::CellType_RAM32M) {
                for (int k=0;k<4;++k) for (int lane=0;lane<2;++lane)
                    local(row-3+k,lane,"DO"+std::string(1,char('A'+k))+"["+std::to_string(lane)+"]");
            } else {
                local(row-1,0,"O");local(row-1,1,"O");
                profile.independent|=MemoryFFRules::slotBit(row,0);
            }
        }
    }
    memoryLogicRows=(~occupied)&255u;
    for (unsigned r=0;r<8;++r) if (memoryLogicRows&(1u<<r))
        profile.independent|=MemoryFFRules::slotBit(r,0)|MemoryFFRules::slotBit(r,1);
    memoryNets->profiles.push_back(profile);
    memoryFFPackingReady=true;
    // Vivado needs the write-address owner in H before admitting lower owners.
    std::sort(LUTRAMMacros.begin(),LUTRAMMacros.end(),[](Macro *a,Macro *b) {
        return a->getFixedCellInfoVec()[0].BELName>b->getFixedCellInfoVec()[0].BELName;
    });
    LUTRAMMacro=LUTRAMMacros.front();
}

void ParallelCLBPacker::PackingCLBSite::prepareMemoryFFPacking(bool enabled, bool srlLogic)
{
    memoryFFPackingReady=false;
    memoryLogicRows=0;
    memoryNets.reset();
    memoryLocalOnlySecondaryRows=0;
    memoryMuxSelectMask=0;
    memoryLocalOnlyPrimaryRows=0;
    if (pendingSRL5Rows) throw std::runtime_error("Unmatched shared SRL16E half-row at packing handoff");
    // Native/incremental local-access demands need their own memory model.
    if (!enabled || placementInfo->localPinAccess.enabled || CLBSite->getSiteType()!="SLICEM") return;
    if (LUTRAMMacros.size()>1 && LUTRAMMacros.front()->getCells().size()==1 &&
        ram_site_sharing::supported(LUTRAMMacros.front()->getCells()[0]->getOriCellType())) {
        prepareSharedRAMSite();
        return;
    }
    unsigned seen=0;
    for (auto macro:LUTRAMMacros) {
        // Macro legalization fixes an anchor, not every unused FF BEL. Locked
        // (static/incremental) owners retain whole-site exclusion.
        if (macro->isLocked()) return;
        const auto &fixed=macro->getFixedCellInfoVec();
        if (!fixed.empty() && fixed.size()!=macro->getCells().size()) return;
        if (fixed.empty() && macro->getCells().size()!=1) return;
        for (auto cell:macro->getCells()) {
            const auto type=cell->getOriCellType();
            if (cell->isVirtualCell()) return;
            if (macro->getCellOffsetXInMacro(cell)!=0 || macro->getCellOffsetYInMacro(cell)!=0) return;
            std::string bel="H6LUT";
            if (!fixed.empty()) {
                bool found=false;
                for (const auto &f:fixed) if (f.cell==cell) {bel=f.BELName;found=true;break;}
                if (!found) return;
            }
            if (type==DesignInfo::CellType_MUXF7) {
                srlLogic=false;
                // Preserve the existing MUX select-input reservation: AX/CX/
                // EX/GX cannot simultaneously be an independent FF D bypass.
                unsigned row;
                if (bel=="F7MUX_AB") row=0;
                else if (bel=="F7MUX_CD") row=2;
                else if (bel=="F7MUX_EF") row=4;
                else if (bel=="F7MUX_GH") row=6;
                else return;
                unsigned bit=MemoryFFRules::slotBit(row,0);
                if (memoryMuxSelectMask & bit) return;
                memoryMuxSelectMask |= bit;
                continue;
            }
            // Composite RAMs are exported as an anchored primitive. Each
            // profile describes a complete simultaneous RAM/FF footprint.
            bool composite = false;
            std::unordered_map<std::string,DesignInfo::DesignNet *> ports;
            for (const auto &pattern : MemoryFFRules::ramProfiles) {
                if (pattern.type != type) continue;
                if (bel!="H6LUT" || LUTRAMMacros.size()!=1 || macro->getCells().size()!=1 ||
                    placementInfo->getDeviceInfo()->getDeviceName()!="U250") return;
                if (!composite) {
                    recordMemoryPins(cell);
                    for (auto pin : cell->getPins()) ports.emplace(pin->getRefPinName(),pin->getNet());
                }
                composite = true;
                RAMSlotProfile profile;
                for (unsigned row=0;row<8;++row) for (unsigned lane=0;lane<2;++lane) {
                    const char *port = pattern.sources[2*row+lane];
                    unsigned slot=(row/4)*8+lane*4+row%4;
                    if (*port=='*') {profile.independent |= MemoryFFRules::slotBit(row,lane); continue;}
                    if (*port=='-') continue;
                    auto found=ports.find(port);
                    if (found!=ports.end()) {
                        profile.local[slot]=found->second;
                        recordRAMReadClock(cell, row, port);
                    }
                }
                memoryNets->profiles.push_back(profile);
            }
            if (composite) {
                // U250 H6LUT anchor, verified above. These are complete spare
                // LUT rows, not independent FF inputs or spare 5LUT labels.
                switch (type) {
                    case DesignInfo::CellType_RAM32M:
                    case DesignInfo::CellType_RAM64M:
                    case DesignInfo::CellType_RAM256X1S: memoryLogicRows=0x0fu; break; // A-D
                    case DesignInfo::CellType_RAM32X1D:
                    case DesignInfo::CellType_RAM64X1D: memoryLogicRows=0x3fu; break; // A-F
                    default: break; // RAM32M16, RAM64M8, or unverified footprint
                }
                memoryFFPackingReady=true;
                return;
            }
            if (type!=DesignInfo::CellType_SRLC32E && type!=DesignInfo::CellType_SRL16E &&
                type!=DesignInfo::CellType_RAM64X1S) return;
            if (type==DesignInfo::CellType_RAM64X1S) srlLogic=false;
            if (srlLogic) {
                for (auto pin:cell->getOutputPins())
                    if (pin->getRefPinName()!="Q" && pin->getNet() && !pin->getNet()->getPinsBeDriven().empty())
                        srlLogic=false;
            }
            bool low = type==DesignInfo::CellType_SRL16E && bel.size()==5 && bel.substr(1)=="5LUT";
            if (bel.size()!=5 || (!low && bel.substr(1)!="6LUT") || bel[0]<'A' || bel[0]>'H') return;
            recordMemoryPins(cell);
            unsigned row=bel[0]-'A';
            // Exact BEL ownership and dual-SRL compatibility were verified at handoff.
            // Each occupied input bypass now belongs to its corresponding LUT output.
            seen |= 1u<<row;
            if (type==DesignInfo::CellType_SRL16E && !low) memoryLocalOnlyPrimaryRows |= 1u<<row;
            else memoryLocalOnlySecondaryRows |= 1u<<row;
            for (auto pin:cell->getOutputPins()) {
                if (pin->getRefPinName()==(type==DesignInfo::CellType_RAM64X1S?"O":"Q")) {
                    (type==DesignInfo::CellType_SRL16E && !low ? memoryNets->primaryQ : memoryNets->secondaryQ)[row]=pin->getNet();
                    if (type==DesignInfo::CellType_RAM64X1S) recordRAMReadClock(cell, row, "O");
                    if (type != DesignInfo::CellType_RAM64X1S && memoryNets->outputs.count(pin->getNet())) {
                        unsigned lane = (type == DesignInfo::CellType_SRL16E && !low) ? 0 : 1;
                        memoryNets->srlOutputSlots[pin->getNet()] |= MemoryFFRules::slotBit(row, lane);
                        for (auto sink : pin->getNet()->getPinsBeDriven())
                            if (sink->getCell()->isFF() && sink->getRefPinName() == "D")
                                memoryNets->directSRLFFs.push_back(sink->getCell());
                    }
                }
            }
        }
    }
    memoryFFPackingReady=seen!=0;
    if (srlLogic && memoryFFPackingReady && placementInfo->getDeviceInfo()->getDeviceName()=="U250")
        memoryLogicRows=(~seen)&0xffu;
}

bool ParallelCLBPacker::PackingCLBSite::acceptsMemoryPU(PlacementInfo::PlacementUnit *pu) const
{
    if (memoryLogicRows && !pu->isLocked() && (!pu->isFixed() ||
        (determinedClusterInSite && determinedClusterInSite->getPUs().count(pu)))) {
        if (auto single=dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) {
            auto cell=single->getCell();
            if (!cell->isVirtualCell()) {
                switch (cell->getOriCellType()) {
                    case DesignInfo::CellType_LUT1: case DesignInfo::CellType_LUT2:
                    case DesignInfo::CellType_LUT3: case DesignInfo::CellType_LUT4:
                    case DesignInfo::CellType_LUT5: case DesignInfo::CellType_LUT6: return true;
                    default: break;
                }
            }
        }
    }
    if (hasCompositeRAM() && memoryLogicRows && !pu->isLocked() && (!pu->isFixed() ||
        (determinedClusterInSite && determinedClusterInSite->getPUs().count(pu)))) {
        auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu);
        if (macro && macro->getMacroType()==PlacementInfo::PlacementMacro::PlacementMacroType_LUTLUTSeires &&
            macro->getCells().size()==2 && macro->getFixedCellInfoVec().empty()) {
            for (auto cell:macro->getCells())
                if (cell->getOriCellType()<DesignInfo::CellType_LUT1 || cell->getOriCellType()>DesignInfo::CellType_LUT5 ||
                    cell->isVirtualCell() || macro->getCellOffsetXInMacro(cell)!=0 || macro->getCellOffsetYInMacro(cell)!=0)
                    return false;
            return CLBSiteLegality::lutPairCompatible(macro->getCells()[0],macro->getCells()[1]);
        }
    }
    return acceptsMemoryFFPU(pu);
}

bool ParallelCLBPacker::PackingCLBSite::acceptsMemoryFFPU(PlacementInfo::PlacementUnit *pu) const
{
    if (!memoryFFPackingReady || pu->isLocked()) return false;
    // setPULocationToPackedSite fixes committed FFs before final slot mapping.
    // Only this site's committed members may bypass the movable-PU gate.
    if (pu->isFixed() && (!determinedClusterInSite ||
        !determinedClusterInSite->getPUs().count(pu))) return false;
    auto supported = [](DesignInfo::DesignCell *ff) {
        const auto type=ff->getOriCellType();
        return !ff->isVirtualCell() &&
            (type==DesignInfo::CellType_FDRE || type==DesignInfo::CellType_FDSE ||
             type==DesignInfo::CellType_FDCE || type==DesignInfo::CellType_FDPE) && ff->getControlSetInfo();
    };
    if (auto single=dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu))
        return supported(single->getCell());
    auto pair=dynamic_cast<PlacementInfo::PlacementMacro *>(pu);
    if (!pair || pair->getMacroType()!=PlacementInfo::PlacementMacro::PlacementMacroType_FFFFPair ||
        pair->getCells().size()!=2 || !pair->getFixedCellInfoVec().empty()) return false;
    const auto &members=pair->getCells();
    if (members[0]==members[1]) return false;
    for (auto ff:members)
        if (!supported(ff) || pair->getCellOffsetXInMacro(ff)!=0 || pair->getCellOffsetYInMacro(ff)!=0)
            return false;
    return members[0]->getControlSetInfo()==members[1]->getControlSetInfo();
}

bool ParallelCLBPacker::PackingCLBSite::mapMemoryFFCandidate(PackingCLBCluster *candidate,
                                                            SiteBELMapping *result,
                                                            std::vector<PlacementInfo::PlacementUnit *> *rowOwners) const
{
    const auto &pairs=candidate->getPairedLUTs();
    if (!pairs.empty() && !hasCompositeRAM()) return false;
    for (const auto &pair:pairs)
        if (!CLBSiteLegality::lutPairCompatible(pair.first,pair.second)) return false;
    const auto &logic=candidate->getSingleLUTs();
    const unsigned needed=logic.size()+pairs.size();
    if (needed>static_cast<unsigned>(__builtin_popcount(memoryLogicRows))) return false;
    // For a composite RAM, memoryLogicRows contains only complete spare LUT
    // rows. The RAM profile describes the independent D paths in each lane;
    // an ordinary 6LUT in such a row leaves those paths usable. A blocked FF
    // Q output excludes that FF BEL, but does not consume the logic LUT.
    const bool compositeRAM = memoryNets && !memoryNets->profiles.empty();
    unsigned rowCount=0, logicFFMask=0;
    std::array<unsigned,8> rows{};
    for (unsigned r=0; r<8 && rowCount<needed; ++r) {
        if (!(memoryLogicRows & (1u<<r))) continue;
        const unsigned lanes=MemoryFFRules::slotBit(r,0) | MemoryFFRules::slotBit(r,1);
        if (!compositeRAM) {
            // The SRL-only matcher still reserves route-through rows.
            if (ffQBlockedMask & lanes) continue;
            logicFFMask |= lanes;
        }
        rows[rowCount++]=r;
    }
    if (rowCount!=needed) return false;
    auto finish=[&](const SiteBELMapping &slots) {
        if (!result) return;
        *result=slots;
        std::array<DesignInfo::DesignCell*,8> sorted{};
        std::copy(logic.begin(),logic.end(),sorted.begin());
        std::sort(sorted.begin(),sorted.begin()+logic.size(),
            [](auto *a,auto *b){return a->getCellId()<b->getCellId();});
        unsigned i=0;
        for (;i<logic.size();++i) result->LUTs[rows[i]/4][0][rows[i]%4]=sorted[i];
        for (auto pair:candidate->getSortedPairedLUTs()) {
            auto row=rows[i++];
            result->LUTs[row/4][0][row%4]=pair.first;
            result->LUTs[row/4][1][row%4]=pair.second;
        }
    };
    const auto &groups=candidate->getFFControlSets();
    struct LocalFF { DesignInfo::DesignCell *cell; unsigned slots; };
    std::array<LocalFF,16> localFFs;
    unsigned localCount = 0, localSlotUnion = 0;
    std::array<std::array<DesignInfo::DesignNet*,4>,4> data = {};
    unsigned count=0;
    for (unsigned g=0;g<4;++g) {
        if (groups[g].getFFs().size()>4) return false;
        unsigned i=0;
        for (auto ff:groups[g].getFFs()) {
            if (!acceptsMemoryFFPU(placementInfo->getPlacementUnitByCell(ff))) return false;
            for (auto pin:ff->getInputPins())
                if (pin->getRefPinName()=="D") {data[g][i]=pin->getNet(); break;}
            if (hasSRLRowOutputs()) {
                auto direct = memoryNets->srlOutputSlots.find(data[g][i]);
                if (direct != memoryNets->srlOutputSlots.end()) {
                    unsigned slots = direct->second & ~(ffQBlockedMask | memoryMuxSelectMask | logicFFMask);
                    if (slots) {
                        localFFs[localCount++] = {ff, slots};
                        localSlotUnion |= slots;
                    }
                }
            }
            ++i; ++count;
        }
    }
    // Historical empty memory sites remain valid even when the feature is off.
    if (!count) {finish(SiteBELMapping(ffQBlockedMask));return true;}
    if (!memoryFFPackingReady) return false;
    const int upperBound = std::min(static_cast<int>(localCount), __builtin_popcount(localSlotUnion));
    int bestHits = -1;
    // A dedicated slot is reachable only by FFs on its owner's Q net. Within
    // a legal control bank those FFs have the same direct choices. Filling a
    // free dedicated slot cannot displace another net or reduce cardinality.
    auto consider = [&](SiteBELMapping &slots, bool sameControl) {
        if (!CLBSiteLegality::checkControlsAndLUTs(slots)) return;
        if (!localCount) { finish(slots); bestHits = 0; return; }
        auto occupant = [&](unsigned slot) -> DesignInfo::DesignCell *& {
            return slots.FFs[slot/8][(slot/4)%2][slot%4];
        };
        unsigned free = 0;
        for (unsigned slot = 0; slot < 16; ++slot)
            if (!occupant(slot)) free |= 1u << slot;
        std::array<DesignInfo::DesignCell *,16> hits;
        int hitCount = 0;
        for (unsigned i = 0; i < localCount; ++i) {
            auto &ff = localFFs[i];
            unsigned src = 0;
            while (src < 16 && occupant(src) != ff.cell) ++src;
            assert(src < 16);
            if (!(ff.slots & (1u << src))) {
                unsigned destinations = ff.slots & free;
                if (!sameControl) destinations &= 15u << (4 * (src/4));
                if (!destinations) continue;
                unsigned dst = __builtin_ctz(destinations);
                std::swap(occupant(src), occupant(dst));
                free = (free | (1u << src)) & ~(1u << dst);
            }
            hits[hitCount++] = ff.cell;
        }
        if (hitCount <= bestHits) return;
        bestHits = hitCount;
        finish(slots);
        if (rowOwners) {
            rowOwners->clear();
            for (int i = 0; i < hitCount; ++i)
                rowOwners->push_back(placementInfo->getPlacementUnitByCell(hits[i]));
        }
    };
    auto match = [&](const RAMSlotProfile *profile) {
        std::array<std::array<unsigned,4>,4> allowed = {};
        for (unsigned g=0;g<4;++g) for (unsigned i=0;i<groups[g].getFFs().size();++i) {
            allowed[g][i] = profile ? profile->allowed(data[g][i]) & ~ffQBlockedMask :
                MemoryFFRules::allowedSlots(memoryLocalOnlySecondaryRows,memoryNets->primaryQ,
                                             memoryNets->secondaryQ,data[g][i],
                                             ffQBlockedMask | memoryMuxSelectMask | logicFFMask,memoryLocalOnlyPrimaryRows);
            allowed[g][i] &= ramReadClockSlots(groups[g].getFFs()[i], data[g][i]);
        }
        std::array<DesignInfo::DesignCell*,16> ffs = {};
        std::array<unsigned,16> masks = {};
        unsigned index=0;
        bool sameControl=true;
        for(unsigned g=0;g<4;++g) for(unsigned i=0;i<groups[g].getFFs().size();++i) {
            auto ff=groups[g].getFFs()[i];
            if(index && ff->getControlSetInfo()!=ffs[0]->getControlSetInfo()) sameControl=false;
            ffs[index]=ff; masks[index++]=allowed[g][i];
        }
        if(sameControl) {
            std::array<int,16> owner;
            if(!MemoryFFRules::matchSameControl(masks,count,owner)) return false;
            SiteBELMapping slots(ffQBlockedMask);
            for(unsigned slot=0;slot<16;++slot) if(owner[slot]>=0)
                slots.FFs[slot/8][(slot%8)/4][slot%4]=ffs[owner[slot]];
            consider(slots, true);
            return bestHits >= 0;
        }
        std::array<unsigned,4> order={{0,1,2,3}};
        do {
            if (!candidate->compatibleInOneHalfCLB(order[0],order[1]) ||
                !candidate->compatibleInOneHalfCLB(order[2],order[3])) continue;
            SiteBELMapping slots(ffQBlockedMask);
            bool ok=true;
            for (unsigned bank=0;bank<4 && ok;++bank) {
                const auto g=order[bank];
                std::array<unsigned,4> masks = {};
                std::array<int,4> assignment = {{-1,-1,-1,-1}};
                unsigned n=groups[g].getFFs().size();
                for(unsigned i=0;i<n;++i) masks[i]=(allowed[g][i]>>(bank*4))&15u;
                if (!MemoryFFRules::matchBank(masks,n,assignment)) {ok=false;break;}
                for(unsigned i=0;i<n;++i) slots.FFs[bank/2][bank%2][assignment[i]]=groups[g].getFFs()[i];
            }
            if (ok) consider(slots, false);
            if (bestHits == upperBound) return true;
        } while(std::next_permutation(order.begin(),order.end()));
        return bestHits >= 0;
    };
    if (compositeRAM) {
        for (const auto &profile : memoryNets->profiles)
            if (match(&profile)) return true;
        return false;
    }
    return match(nullptr);
}

void ParallelCLBPacker::PackingCLBSite::finalMapMemoryFFs()
{
    if (!mapMemoryFFCandidate(determinedClusterInSite,&slotMapping))
        throw std::runtime_error("Memory FF packing witness missing at final mapping: "+CLBSite->getName());
    mappedCells.clear();mappedLUTs.clear();mappedFFs.clear();
    for (auto lut:determinedClusterInSite->getSingleLUTs()) {mappedCells.insert(lut);mappedLUTs.insert(lut);}
    for (auto pair:determinedClusterInSite->getPairedLUTs())
        for (auto lut:{pair.first,pair.second}) {mappedCells.insert(lut);mappedLUTs.insert(lut);}
    for(int half=0;half<2;++half)for(int lane=0;lane<2;++lane)for(int row=0;row<4;++row)
        if(auto ff=slotMapping.FFs[half][lane][row]) {mappedCells.insert(ff);mappedFFs.insert(ff);}
}
