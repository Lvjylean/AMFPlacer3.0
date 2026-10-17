#include "../../../utils/RuntimeProfiler.h"
/**
 * @file ParallelCLBPacker.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief  This implementation file contains APIs' implementation of the ParallelCLBPacker which finally packs
 * LUT/FF/MUX/CARRY elements into legal CLB sites in a parallel approach.
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "ParallelCLBPacker.h"
#include <memory>
#define TIMINGDP

void ParallelCLBPacker::prePackLegalizedMacros(PlacementInfo::PlacementMacro *tmpMacro)
{
    std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> &PULegalSite =
        placementInfo->getPULegalSite();
    if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_CARRY)
    {
        assert(PULegalSite.find(tmpMacro) != PULegalSite.end());
        std::vector<DeviceInfo::DeviceSite *> &legalSites = PULegalSite[tmpMacro];
        for (unsigned int i = 0; i < legalSites.size(); i++)
        {
            deviceSite2PackingSite[legalSites[i]]->mapCarryRelatedCellsToSlots(tmpMacro, i);
            deviceSite2PackingSite[legalSites[i]]->addCarry();
            placementInfo->addPUIntoClockColumn(tmpMacro, legalSites[i]);
        }
    }
    else if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MCLB)
    {
        assert(PULegalSite.find(tmpMacro) != PULegalSite.end());
        std::vector<DeviceInfo::DeviceSite *> &legalSites = PULegalSite[tmpMacro];
        for (unsigned int i = 0; i < legalSites.size(); i++)
        {
            deviceSite2PackingSite[legalSites[i]]->mapLUTRAMRelatedCellsToSlots(tmpMacro);
            deviceSite2PackingSite[legalSites[i]]->addLUTRAMMacro();
            placementInfo->addPUIntoClockColumn(tmpMacro, legalSites[i]);
        }
    }
}

ParallelCLBPacker::ParallelCLBPacker(DesignInfo *designInfo, DeviceInfo *deviceInfo, PlacementInfo *placementInfo,
                                     std::map<std::string, std::string> &JSONCfg, int unchangedIterationThr,
                                     int numNeighbor, float deltaD, float curD, float maxD, int PQSize,
                                     float HPWLWeight, std::string packerName,
                                     PlacementTimingOptimizer *timingOptimizer, WirelengthOptimizer *WLOptimizer)
    : designInfo(designInfo), deviceInfo(deviceInfo), placementInfo(placementInfo), JSONCfg(JSONCfg),
      unchangedIterationThr(unchangedIterationThr), numNeighbor(numNeighbor), deltaD(deltaD), curD(curD), maxD(maxD),
      PQSize(PQSize), HPWLWeight(HPWLWeight), packerName(packerName), timingOptimizer(timingOptimizer),
      WLOptimizer(WLOptimizer), PUId2PackingCLBSite(placementInfo->getPlacementUnits().size(), nullptr),
      PUId2PackingCLBSiteCandidate(placementInfo->getPlacementUnits().size(), nullptr),
      placementUnits(placementInfo->getPlacementUnits()),
      placementUnpackedCells(placementInfo->getPlacementUnpackedCells()),
      placementMacros(placementInfo->getPlacementMacros()), cellInMacros(placementInfo->getCellInMacros()),
      cellId2PlacementUnit(placementInfo->getCellId2PlacementUnit())
{
    if (JSONCfg.find("y2xRatio") != JSONCfg.end())
    {
        y2xRatio = std::stof(JSONCfg["y2xRatio"]);
    }
    if (JSONCfg.find("noSectorGuided") != JSONCfg.end())
    {
        noSectorGuided = (JSONCfg["noSectorGuided"] == "true");
    }
    if (JSONCfg.find("useBigWindow") != JSONCfg.end())
    {
        useBigWindow = (JSONCfg["useBigWindow"] == "true");
    }
    if (JSONCfg.find("noTimingPathLenFactor") != JSONCfg.end())
    {
        noTimingPathLenFactor = (JSONCfg["noTimingPathLenFactor"] == "true");
    }
    // PlacementInfo *placementInfo, DeviceInfo::DeviceSite *CLBSite, int unchangedIterationThr,
    //                        int numNeighbor, float deltaD, float curD, float maxD, int PQSize, float y2xRatio,
    //                        std::vector<PackingCLBSite *> &PUId2PackingCLBSite

    int numClockCols = placementInfo->getDeviceInfo()->getClockColumns().size();
    clockColumns2PackingSites =
        std::vector<std::vector<PackingCLBSite *>>(numClockCols, std::vector<PackingCLBSite *>());

    std::string targetSiteType = "SLICEL";
    deviceSite2PackingSite.clear();
    for (auto curSite : deviceInfo->getSitesInType(targetSiteType))
    {
        if (curSite->isOccupied())
            continue;
        PackingCLBSite *tmpPackingSite =
            new PackingCLBSite(placementInfo, curSite, unchangedIterationThr, numNeighbor, deltaD, curD, maxD, PQSize,
                               y2xRatio, HPWLWeight, PUId2PackingCLBSite);
        deviceSite2PackingSite[curSite] = tmpPackingSite;
        packingSites.push_back(tmpPackingSite);
        clockColumns2PackingSites[curSite->getClockHalfColumn()->getId()].push_back(tmpPackingSite);
    }
    targetSiteType = "SLICEM";
    for (auto curSite : deviceInfo->getSitesInType(targetSiteType))
    {
        if (curSite->isOccupied())
            continue;
        PackingCLBSite *tmpPackingSite =
            new PackingCLBSite(placementInfo, curSite, unchangedIterationThr, numNeighbor, deltaD, curD, maxD, PQSize,
                               y2xRatio, HPWLWeight, PUId2PackingCLBSite);
        deviceSite2PackingSite[curSite] = tmpPackingSite;
        packingSites.push_back(tmpPackingSite);
        clockColumns2PackingSites[curSite->getClockHalfColumn()->getId()].push_back(tmpPackingSite);
    }

    for (auto tmpMacro : placementMacros)
    {
        prePackLegalizedMacros(tmpMacro);
    }

    print_status("ParallelCLBPacker: CARRY macros are mapped to sites.");

    int numPackingSites = packingSites.size();
    for (int i = 0; i < numPackingSites; i++)
    {
        auto tmpPackingSite = packingSites[i];
        if (tmpPackingSite->getDeterminedClusterInSite())
        {
            assert(tmpPackingSite->checkIsPrePackedSite());
        }
    }

    if (noTimingPathLenFactor)
    {
        for (int i = 0; i < numPackingSites; i++)
        {
            packingSites[i]->setTimingPathLenFactorTo(false);
        }
    }
    print_status("ParallelCLBPacker: initialized.");
}

void ParallelCLBPacker::packCLBsIteration(bool initial, bool debug)
{
    AMF_PROFILE_FUNCTION("final_packing");
    int numClockCols = clockColumns2PackingSites.size();
#pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < numClockCols; i++)
    {
        for (unsigned int j = 0; j < clockColumns2PackingSites[i].size(); j++)
        {
            auto tmpPackingSite = clockColumns2PackingSites[i][j];
            tmpPackingSite->updateStep(initial, debug);
        }
    }

    //     int numPackingSites = packingSites.size();
    // #pragma omp parallel for schedule(dynamic, 16)
    //     for (int i = 0; i < numPackingSites; i++)
    //     {
    //         auto tmpPackingSite = packingSites[i];
    //         tmpPackingSite->updateStep(initial, debug);
    //     }

    // update PU's selection of packing site
    PUId2PackingCLBSite.clear();
    PUId2PackingCLBSite.resize(placementInfo->getPlacementUnits().size(), nullptr);

    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                {
                    assert(!PUId2PackingCLBSite[tmpPU->getId()]);
                    // if (!placementInfo->checkClockColumnLegalization(tmpPU, packingSite->getCLBSite()))
                    // {
                    //     placementInfo->printOutClockColumnLegalization(tmpPU, packingSite->getCLBSite());
                    // }
                    // assert(placementInfo->checkClockColumnLegalization(tmpPU, packingSite->getCLBSite()));
                    PUId2PackingCLBSite[tmpPU->getId()] = packingSite;
                    placementInfo->addPUIntoClockColumn(tmpPU, packingSite->getCLBSite());
                }
            }
        }
    }

    PUId2PackingCLBSiteCandidate.clear();
    PUId2PackingCLBSiteCandidate.resize(placementInfo->getPlacementUnits().size(), nullptr);
    for (auto tmpPackingSite : packingSites)
    {
        if (tmpPackingSite->hasValidPQTop())
        {
            const ParallelCLBPacker::PackingCLBSite::PackingCLBCluster *tmpTop = tmpPackingSite->getPriorityQueueTop();
            for (auto tmpPU : tmpTop->getPUs())
            {
                if (!PUId2PackingCLBSite[tmpPU->getId()])
                {

                    if (!placementInfo->checkClockColumnLegalization(tmpPU, tmpPackingSite->getCLBSite()))
                        continue;

                    if (!PUId2PackingCLBSiteCandidate[tmpPU->getId()])
                    {
                        PUId2PackingCLBSiteCandidate[tmpPU->getId()] = tmpPackingSite;
                        // if (tmpPU->getId() == 117104)
                        // {
                        //     std::ofstream debugFile;
                        //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
                        //     assert(debugFile.is_open() && debugFile.good() &&
                        //            "The path for placement Tcl dumping does not exist and please check your path "
                        //            "settings");

                        //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<First Choice >>>>>>>>>>>>>>>\n";
                        //     debugFile << PUId2PackingCLBSiteCandidate[117104]->getCLBSite()->getName() << "\n";
                        //     debugFile << PUId2PackingCLBSiteCandidate[117104]->getPriorityQueueTop();
                        //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>\n";
                        //     debugFile.close();
                        // }
                        // if (tmpPU->getId() == 116959)
                        // {
                        //     std::ofstream debugFile;
                        //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
                        //     assert(debugFile.is_open() && debugFile.good() &&
                        //            "The path for placement Tcl dumping does not exist and please check your path "
                        //            "settings");

                        //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<First Choice >>>>>>>>>>>>>>>\n";
                        //     debugFile << PUId2PackingCLBSiteCandidate[116959]->getCLBSite()->getName() << "\n";
                        //     debugFile << PUId2PackingCLBSiteCandidate[116959]->getPriorityQueueTop();
                        //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>\n";
                        //     debugFile.close();
                        // }
                    }
                    else
                    {
                        float oriDeltaScore =
                            PUId2PackingCLBSiteCandidate[tmpPU->getId()]->getPriorityQueueTop()->getScoreInSite() -
                            PUId2PackingCLBSiteCandidate[tmpPU->getId()]->getDetScore();
                        float newDeltaScore =
                            tmpPackingSite->getPriorityQueueTop()->getScoreInSite() - tmpPackingSite->getDetScore();
                        if (newDeltaScore > oriDeltaScore)
                        {
                            PUId2PackingCLBSiteCandidate[tmpPU->getId()] = tmpPackingSite;
                            // if (tmpPU->getId() == 117104)
                            // {
                            //     std::ofstream debugFile;
                            //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
                            //     assert(debugFile.is_open() && debugFile.good() &&
                            //            "The path for placement Tcl dumping does not exist and please check your path
                            //            " "settings");

                            //     debugFile
                            //         << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<Second Choice >>>>>>>>>>>>>>> :
                            //         newDeltaScore"
                            //         << newDeltaScore << "  oriDeltaScore:" << oriDeltaScore << "\n";
                            //     debugFile << PUId2PackingCLBSiteCandidate[117104]->getCLBSite()->getName() << "\n";
                            //     debugFile << PUId2PackingCLBSiteCandidate[117104]->getPriorityQueueTop();
                            //     debugFile << "-----------------------------------------------------------------\n";
                            //     if (PUId2PackingCLBSiteCandidate[117104]->getDeterminedClusterInSite())
                            //         debugFile << PUId2PackingCLBSiteCandidate[117104]->getDeterminedClusterInSite();
                            //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>\n";
                            //     debugFile.close();
                            // }
                            // if (tmpPU->getId() == 116959)
                            // {
                            //     std::ofstream debugFile;
                            //     debugFile.open("OpenPitonSLICE_SLICE_X38Y213", std::ios_base::app);
                            //     assert(debugFile.is_open() && debugFile.good() &&
                            //            "The path for placement Tcl dumping does not exist and please check your path
                            //            " "settings");

                            //     debugFile
                            //         << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<Second Choice >>>>>>>>>>>>>>> :
                            //         newDeltaScore"
                            //         << newDeltaScore << "  oriDeltaScore:" << oriDeltaScore << "\n";
                            //     debugFile << PUId2PackingCLBSiteCandidate[116959]->getCLBSite()->getName() << "\n";
                            //     debugFile << PUId2PackingCLBSiteCandidate[116959]->getPriorityQueueTop();
                            //     debugFile << "-----------------------------------------------------------------\n";
                            //     if (PUId2PackingCLBSiteCandidate[116959]->getDeterminedClusterInSite())
                            //         debugFile << PUId2PackingCLBSiteCandidate[116959]->getDeterminedClusterInSite();
                            //     debugFile << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>\n";
                            //     debugFile.close();
                            // }
                        }
                    }
                }
            }
        }
    }

    for (unsigned int i = 0; i < PUId2PackingCLBSite.size(); i++)
    {
        if (!PUId2PackingCLBSite[i] && PUId2PackingCLBSiteCandidate[i])
        {
            PUId2PackingCLBSite[i] = PUId2PackingCLBSiteCandidate[i];
        }
    }
}

void ParallelCLBPacker::updatePUCell2PackingSite()
{
    // print_status("ParallelCLBPacker: updatig PUCell2PackingSite");
    cellId2PackingSite = std::vector<PackingCLBSite *>(placementInfo->getCells().size(), nullptr);
    PUId2PackingCLBSite.clear();
    PUId2PackingCLBSite.resize(placementInfo->getPlacementUnits().size(), nullptr);
    int packNum = packingSites.size();

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < packNum; i++)
    {
        auto packingSite = packingSites[i];
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                auto cellSet = packingSite->getDeterminedClusterInSite()->getCellSet();
                for (auto cell : cellSet)
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(cell->getCellId());
                    if (cellId2PackingSite[cell->getCellId()])
                    {
                        std::cout << cellId2PackingSite[cell->getCellId()] << "\n";
                        std::cout << "problem PU: " << curPU << "\n";
                        std::cout << "problem cell: " << cell << "\n";
                        assert(false && "each cell should be only mapped once!");
                    }
                    cellId2PackingSite[cell->getCellId()] = packingSite;
                    if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                    {
                        if (curMacro->getCells()[0] == cell)
                        {
                            PUId2PackingCLBSite[curPU->getId()] = packingSite;
                        }
                    }
                    else
                    {
                        PUId2PackingCLBSite[curPU->getId()] = packingSite;
                    }
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    cellId2PackingSite[packingSite->getNonCLBCell()->getCellId()] = packingSite;
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                    {
                        if (curMacro->getCells()[0] == packingSite->getNonCLBCell())
                            PUId2PackingCLBSite[curPU->getId()] = packingSite;
                    }
                    else
                        PUId2PackingCLBSite[curPU->getId()] = packingSite;
                    if (curPU->getDSPNum() == 1)
                    {
                        placementInfo->getPULegalSite()[curPU] =
                            std::vector<DeviceInfo::DeviceSite *>(1, packingSite->getCLBSite());
                    }
                }
            }
        }
    }
    // print_status("ParallelCLBPacker: updated PUCell2PackingSite");
}

void ParallelCLBPacker::partiallyRecoverToBestPacking(std::set<PackingCLBSite *> &selectedPackingSites)
{
    // print_status("ParallelCLBPacker: recovering selected PUCell2PackingSite");

    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            packingSite->recoverToBestPacking();
        }
    }

    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                auto cellSet = packingSite->getDeterminedClusterInSite()->getCellSet();
                for (auto cell : cellSet)
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(cell->getCellId());
                    cellId2PackingSite[cell->getCellId()] = packingSite;
                    PUId2PackingCLBSite[curPU->getId()] = packingSite;
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    cellId2PackingSite[packingSite->getNonCLBCell()->getCellId()] = packingSite;
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    PUId2PackingCLBSite[curPU->getId()] = packingSite;
                    if (curPU->getDSPNum() == 1)
                    {
                        placementInfo->getPULegalSite()[curPU] =
                            std::vector<DeviceInfo::DeviceSite *>(1, packingSite->getCLBSite());
                    }
                }
            }
        }
    }

    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                if (packingSite->getDeterminedClusterInSite()->getPUs().size())
                {
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        tmpPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                        packingSite->getCLBSite()->Y());
                        tmpPU->setFixed();
                    }
                }
                for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                {
                    if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                    {
                        cellLoc[unpackCell->getCell()->getCellId()].X = tmpPU->X();
                        cellLoc[unpackCell->getCell()->getCellId()].Y = tmpPU->Y();
                    }
                    else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                    {
                        assert(curMacro->getCARRYNum() + curMacro->getDSPNum() + curMacro->getBRAMNum() <= 1);
                        assert(!curMacro->isPacked());
                        int cellIdInMacro = 0;
                        for (auto tmpCell : curMacro->getCells())
                        {
                            cellLoc[tmpCell->getCellId()].X = tmpPU->X();
                            cellLoc[tmpCell->getCellId()].Y = tmpPU->Y();
                            cellIdInMacro++;
                        }
                    }
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    if (curPU->isLocked())
                    {
                        continue;
                    }
                    assert(curPU->getDSPNum() == 1); // currently we can only handle this kind of macro
                    curPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                    packingSite->getCLBSite()->Y());
                    curPU->setFixed();

                    if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                    {
                        if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_DSP)
                        {
                            auto curCell = tmpMacro->getCells()[0];
                            auto targetSite = packingSite->getCLBSite();

                            cellLoc[curCell->getCellId()].X = targetSite->X();
                            cellLoc[curCell->getCellId()].Y = targetSite->Y();
                        }
                        else
                        {
                            assert(false && "undefined situtation");
                        }
                    }
                }
            }
        }
    }

    // print_status("ParallelCLBPacker: recovered selected PUCell2PackingSite");
}

void ParallelCLBPacker::partiallyRecordBestPacking(std::set<PackingCLBSite *> &selectedPackingSites)
{
    // print_status("ParallelCLBPacker: updatig selected PUCell2PackingSite");

    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            packingSite->recordBestPacking();
        }
    }

    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                auto cellSet = packingSite->getDeterminedClusterInSite()->getCellSet();
                for (auto cell : cellSet)
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(cell->getCellId());
                    if (cellId2PackingSite[cell->getCellId()] != packingSite)
                    {
                        std::cout << cellId2PackingSite[cell->getCellId()]->getDeterminedClusterInSite() << "\n";
                        std::cout << packingSite->getDeterminedClusterInSite() << "\n";
                        std::cout << "problem PU: " << curPU << "\n";
                        std::cout << "problem cell: " << cell << "\n";
                        assert(false && "each cell should be only mapped once!");
                    }
                    cellId2PackingSite[cell->getCellId()] = packingSite;
                    PUId2PackingCLBSite[curPU->getId()] = packingSite;
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    cellId2PackingSite[packingSite->getNonCLBCell()->getCellId()] = packingSite;
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    PUId2PackingCLBSite[curPU->getId()] = packingSite;
                    if (curPU->getDSPNum() == 1)
                    {
                        placementInfo->getPULegalSite()[curPU] =
                            std::vector<DeviceInfo::DeviceSite *>(1, packingSite->getCLBSite());
                    }
                }
            }
        }
    }

    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    for (auto packingSite : selectedPackingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                if (packingSite->getDeterminedClusterInSite()->getPUs().size())
                {
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        tmpPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                        packingSite->getCLBSite()->Y());
                        tmpPU->setFixed();
                    }
                }
                for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                {
                    if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                    {
                        cellLoc[unpackCell->getCell()->getCellId()].X = tmpPU->X();
                        cellLoc[unpackCell->getCell()->getCellId()].Y = tmpPU->Y();
                    }
                    else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                    {
                        assert(curMacro->getCARRYNum() + curMacro->getDSPNum() + curMacro->getBRAMNum() <= 1);
                        assert(!curMacro->isPacked());
                        int cellIdInMacro = 0;
                        for (auto tmpCell : curMacro->getCells())
                        {
                            cellLoc[tmpCell->getCellId()].X = tmpPU->X();
                            cellLoc[tmpCell->getCellId()].Y = tmpPU->Y();
                            cellIdInMacro++;
                        }
                    }
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    if (curPU->isLocked())
                    {
                        continue;
                    }
                    assert(curPU->getDSPNum() == 1); // currently we can only handle this kind of macro
                    curPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                    packingSite->getCLBSite()->Y());
                    curPU->setFixed();

                    if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                    {
                        if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_DSP)
                        {
                            auto curCell = tmpMacro->getCells()[0];
                            auto targetSite = packingSite->getCLBSite();

                            cellLoc[curCell->getCellId()].X = targetSite->X();
                            cellLoc[curCell->getCellId()].Y = targetSite->Y();
                        }
                        else
                        {
                            assert(false && "undefined situtation");
                        }
                    }
                }
            }
        }
    }

    // print_status("ParallelCLBPacker: updated selected PUCell2PackingSite");
}

void ParallelCLBPacker::recordBestPacking()
{
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            packingSite->recordBestPacking();
        }
    }
    updatePUCell2PackingSite();
    setPULocationToPackedSite();
}

void ParallelCLBPacker::recoverToBestPacking()
{
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            packingSite->recoverToBestPacking();
        }
    }
    updatePUCell2PackingSite();
    setPULocationToPackedSite();
}

void ParallelCLBPacker::packCLBs(int packIterNum, bool doExceptionHandling, bool debug)
{
    AMF_PROFILE_FUNCTION("final_packing");
    placementInfo->updateB2BAndGetTotalHPWL();
    placementInfo->updateElementBinGrid(false); // we don't need utilization information here, we can update LUT/FF
                                                // utilization when needed.

    timingOptimizer->enablePackingTiming();
    int iterCnt = 0;

    packCLBsIteration(true, debug);
    print_status("ParallelCLBPacker: initial packCLBsIteration done.");
    while (true)
    {
        bool noValidPQTop = true;
        for (auto tmpPackingSite : packingSites)
        {
            if (tmpPackingSite->hasValidPQTop())
            {
                noValidPQTop = false;
            }
        }
        if (noValidPQTop)
        {
            break;
        }

        int mappedPUCnt = 0;
        for (auto packingSite : PUId2PackingCLBSite)
        {
            if (packingSite)
                mappedPUCnt++;
        }

        print_status("ParallelCLBPacker: iter#" + std::to_string(iterCnt) +
                     " #Mapped PU=" + std::to_string(mappedPUCnt));

        int deteminedCnt = 0;
        std::map<int, int> sliceSize2SliceCnt;
        sliceSize2SliceCnt.clear();
        for (auto packingSite : packingSites)
        {
            if (packingSite)
            {
                if (packingSite->getDeterminedClusterInSite())
                {
                    if (packingSite->getDeterminedClusterInSite()->getPUs().size())
                    {
                        deteminedCnt++;
                        int tmpSize = 0;
                        for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                        {
                            if (tmpPU->getType() == PlacementInfo::PlacementUnitType_Macro)
                            {
                                if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                                {
                                    tmpSize += tmpPU->getWeight();
                                    assert((unsigned int)tmpPU->getWeight() >= curMacro->getCells().size());
                                }

                                // tmpSize += tmpPU->getWeight();
                            }
                            else
                                tmpSize += 1;
                        }
                        if (sliceSize2SliceCnt.find(tmpSize) == sliceSize2SliceCnt.end())
                        {
                            sliceSize2SliceCnt[tmpSize] = 1;
                        }
                        else
                        {
                            sliceSize2SliceCnt[tmpSize]++;
                        }
                    }
                }
            }
        }
        setPULocationToPackedSite();
        int numFixed = 0;
        for (auto PU : placementInfo->getPlacementUnits())
            if (PU->isFixed())
                numFixed++;

        float fixedRatio = (float)numFixed / (float)placementInfo->getPlacementUnits().size();
        if (fixedRatio > 0.25 && WLOptimizer && iterCnt < 16)
        {
            placementInfo->updateElementBinGrid(false);
            timingOptimizer->pauseCounter();
            timingOptimizer->conductStaticTimingAnalysis();
            placementInfo->getPU2ClockRegionCenters().clear();
            print_status("ParallelCLBPacker: Move unfixed elements with WLOptimizer");
            WLOptimizer->GlobalPlacementQPSolve(placementInfo->getPseudoNetWeight() * 2 * (1 + fixedRatio), true, true,
                                                true, true, false, 1, timingOptimizer);
            dumpAllCellsCoordinate();
        }

        print_status("ParallelCLBPacker: current HPWL=" + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));
        print_status("ParallelCLBPacker: iter#" + std::to_string(iterCnt) +
                     " #determined Slice=" + std::to_string(deteminedCnt));

        std::string distibution = "";
        for (auto tmpPair : sliceSize2SliceCnt)
        {
            distibution += ("(" + std::to_string(tmpPair.first) + "," + std::to_string(tmpPair.second) + "), ");
        }
        print_status("ParallelCLBPacker: iter#" + std::to_string(iterCnt) + " Distribution: " + distibution);

        if (iterCnt > packIterNum)
        {
            break;
        }
        iterCnt++;

        // setPULocationToPackedSite();
        // placementInfo->updateB2BAndGetTotalHPWL();

        packCLBsIteration(false, debug);

        for (auto packingSite : packingSites)
        {
            if (packingSite)
            {
                if (packingSite->getDeterminedClusterInSite())
                {
                    assert(packingSite->getDeterminedClusterInSite()->areAllPUsValidForThisSite(PUId2PackingCLBSite,
                                                                                                packingSite));
                    assert(packingSite->getDeterminedClusterInSite()->checkCellCorrectness(nullptr, true));
                }
            }
        }
        // dumpCLBPacking();
    }
    print_status("ParallelCLBPacker: finish iterative packing");
    if (doExceptionHandling)
    {
        // assert(!incrementalPacking && "for incremental packing, it is not worthy to do exception handling");
        placementInfo->updateElementBinGrid(false);
        timingOptimizer->conductStaticTimingAnalysis();
        exceptionHandling(true);
        placementInfo->updateElementBinGrid(false);
        timingOptimizer->conductStaticTimingAnalysis();

        float minCP = timingOptimizer->getMaxDelay();
        addNonCLBPackingSites();
        recordBestPacking();

        int packNum = packingSites.size();

#ifdef TIMINGDP
        int replaceCnt = 1000;
        int DPCnt = 0;
        int lastRecoverDPCnt = 0;
        int succCnt = 0;
        float clockPeriod = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph()->getClockPeriod();
        float newCP = 10000;
        bool bestPackingUpdated = false;
        float pathCriticalRatio = 0.875;
        int failPathOpt = 0;
        int pathBasedReplaceCnt = 0;
        for (int i = 0; i < 120 && DPCnt < 80; i++, succCnt++)
        {
            updatePUCell2PackingSite();
            i++;

            if (DPCnt == 20 || DPCnt == 40 || DPCnt == 60)
            {
                packNum = packingSites.size();
                recoverToBestPacking();
                timingOptimizer->conductStaticTimingAnalysis();
#pragma omp parallel for schedule(dynamic)
                for (int i = 0; i < packNum; i++)
                    packingSites[i]->finalMapToSlots();
                timingDrivenDetailedPlacement_LUTFFPairReloacationAfterSlotMapping();
                recordBestPacking();
                bestPackingUpdated = true;
            }
            else
            {
                if (i < 4)
                    replaceCnt = timingDrivenDetailedPlacement_shortestPath(100, 0.5, pathCriticalRatio);
                else if (i >= 100)
                    replaceCnt =
                        timingDrivenDetailedPlacement_shortestPath(succCnt, 1.0 - 100.0 / 105.0, pathCriticalRatio);
                else
                    replaceCnt =
                        timingDrivenDetailedPlacement_shortestPath(succCnt * 2, 1.0 - i / 105.0, pathCriticalRatio);
                if (replaceCnt == 0)
                    pathCriticalRatio = 0.8;
                packNum = packingSites.size();
            }
            setPULocationToPackedSite();
            if (replaceCnt)
                timingOptimizer->conductStaticTimingAnalysis();
            newCP = timingOptimizer->getMaxDelay();
            print_info("ParallelCLBPacker: SP algorithm get newCP=" + std::to_string(newCP));
            print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                       " clock period=" + std::to_string(clockPeriod));
            if (DPCnt < 5 && newCP < minCP + 0.015)
            {
                if (i > 4)
                    i -= 4;
                recordBestPacking();
                bestPackingUpdated = true;
            }
            else if (DPCnt > 15 && newCP < minCP + 0.015)
            {

                recordBestPacking();
                updatePUCell2PackingSite();
                if (i > 60)
                {
                    pathBasedReplaceCnt = timingDrivenDetailedPlacement_swap(0.5, 80, true);
                    failPathOpt += (pathBasedReplaceCnt == 0);
                }
                recordBestPacking();
                updatePUCell2PackingSite();
                setPULocationToPackedSite();
                timingOptimizer->conductStaticTimingAnalysis();
                newCP = timingOptimizer->getMaxDelay();
                minCP = newCP;
                bestPackingUpdated = true;
            }
            if (i > 50 && newCP > minCP * 1.05)
            {
                if (newCP > minCP * 1.1 && DPCnt > 50 && lastRecoverDPCnt < DPCnt)
                {
                    recoverToBestPacking();
                    timingOptimizer->conductStaticTimingAnalysis();
                    lastRecoverDPCnt = DPCnt + 5;
                    succCnt = 0;
                    i -= 10;
                }
                else
                {
                    if (succCnt >= 10)
                        succCnt -= 5;
                    i -= 4;
                    lastRecoverDPCnt = DPCnt;
                }
            }
            else if (i > 10 && newCP > minCP * 1.1 && succCnt >= 5)
                succCnt -= 5;
            DPCnt++;

            print_status("ParallelCLBPacker: i=" + std::to_string(i) + " DPCnt=" + std::to_string(DPCnt));
        }

        if (bestPackingUpdated)
            recoverToBestPacking();
        timingOptimizer->conductStaticTimingAnalysis();

        replaceCnt = 1000;
        int failOptCnt = 0;
        succCnt = 0;
        float disOffset = 0;
        newCP = 10000;
        for (int i = 0; i < 2; i++)
        {
            recordBestPacking();
            updatePUCell2PackingSite();
            replaceCnt = timingDrivenDetailedPlacement_swap(0.5, 50, true, pathCriticalRatio);
            setPULocationToPackedSite();
            timingOptimizer->conductStaticTimingAnalysis();

            newCP = timingOptimizer->getMaxDelay();
            if (newCP < minCP + 0.015)
            {
                minCP = newCP;
                recordBestPacking();
                bestPackingUpdated = true;
            }
            print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                       " clock period=" + std::to_string(clockPeriod));
        }

        int resetCnt = 0;
        for (int i = 0; i < 40 && newCP > clockPeriod * 0.9 && resetCnt < 2; i++)
        {
            updatePUCell2PackingSite();
            replaceCnt = timingDrivenDetailedPlacement_swap(disOffset, succCnt, false, pathCriticalRatio);
            if (replaceCnt == 0)
                pathCriticalRatio = 0.8;
            setPULocationToPackedSite();
            if (replaceCnt)
                timingOptimizer->conductStaticTimingAnalysis();
            else
            {
                disOffset += 0.25;
            }
            newCP = timingOptimizer->getMaxDelay();
            print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                       " clock period=" + std::to_string(clockPeriod));
            if (newCP < minCP + 0.015 && newCP > minCP - 0.001)
            {
                disOffset += 0.5;
            }

            if (i > 5 && newCP < minCP + 0.015)
            {
                minCP = newCP;
                recordBestPacking();
                bestPackingUpdated = true;
            }
            if (i > 10 && newCP > minCP * 1.1)
            {
                failOptCnt++;
                if (failOptCnt > 3)
                {
                    resetCnt++;
                    recoverToBestPacking();
                    timingOptimizer->conductStaticTimingAnalysis();
                    failOptCnt = 0;
                    succCnt = 0;
                }
            }
            succCnt++;
        }

        succCnt = 0;
        resetCnt = 0;
        if (bestPackingUpdated)
            recoverToBestPacking();
        timingOptimizer->conductStaticTimingAnalysis();

        print_info("ParallelCLBPacker: failPathOpt=" + std::to_string(failPathOpt));
        succCnt = 0;
        resetCnt = 0;
        for (int i = 0; i < 4 && newCP > clockPeriod * 0.9 && resetCnt < 3; i++)
        {
            recordBestPacking();
            updatePUCell2PackingSite();
            replaceCnt = timingDrivenDetailedPlacement_swap(disOffset + 0.01 * (random() % 100),
                                                            2 * succCnt + (random() % 5), true);
            setPULocationToPackedSite();
            if (replaceCnt)
                timingOptimizer->conductStaticTimingAnalysis();
            else
            {
                disOffset += 0.5;
            }
            newCP = timingOptimizer->getMaxDelay();
            print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                       " clock period=" + std::to_string(clockPeriod));
            if (newCP < minCP + 0.015)
            {
                disOffset += 0.33;
            }

            if (i > 5 && newCP < minCP + 0.015)
            {
                minCP = newCP;
                recordBestPacking();
                bestPackingUpdated = true;
            }
            else
            {
                recoverToBestPacking();
                timingOptimizer->conductStaticTimingAnalysis();
                bestPackingUpdated = true;
            }

            succCnt++;
        }

        if (bestPackingUpdated)
            recoverToBestPacking();
        timingOptimizer->conductStaticTimingAnalysis();

        for (int i = 0; i < 5; i++)
        {
            recordBestPacking();
            updatePUCell2PackingSite();
            replaceCnt = timingDrivenDetailedPlacement_swap(0.5, 100, true, pathCriticalRatio);
            if (replaceCnt == 0)
                pathCriticalRatio = 0.8;
            setPULocationToPackedSite();
            timingOptimizer->conductStaticTimingAnalysis();

            newCP = timingOptimizer->getMaxDelay();
            if (newCP < minCP + 0.015)
            {
                minCP = newCP;
                recordBestPacking();
                bestPackingUpdated = true;
            }
            print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                       " clock period=" + std::to_string(clockPeriod));
        }

        if (!replaceCnt)
        {
            if (bestPackingUpdated)
                recoverToBestPacking();
            timingOptimizer->conductStaticTimingAnalysis();
            useLocalFineTune = true;
            for (int i = 0; i < 5; i++)
            {
                recordBestPacking();
                updatePUCell2PackingSite();
                replaceCnt = timingDrivenDetailedPlacement_swap(0.5, 100, true, pathCriticalRatio);
                if (replaceCnt == 0)
                    pathCriticalRatio = 0.8;
                setPULocationToPackedSite();
                timingOptimizer->conductStaticTimingAnalysis();

                newCP = timingOptimizer->getMaxDelay();
                if (newCP < minCP + 0.015)
                {
                    minCP = newCP;
                    recordBestPacking();
                    bestPackingUpdated = true;
                }
                print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
                           " clock period=" + std::to_string(clockPeriod));
            }
            useLocalFineTune = false;
        }
        // if (fineTuneCandidateNum < 100)
        // {
        //     if (bestPackingUpdated)
        //         recoverToBestPacking();
        //     timingOptimizer->conductStaticTimingAnalysis();
        //     useLocalFineTune = true;
        //     for (int i = 0; i < fineTuneCandidateNum / 2; i++)
        //     {
        //         recordBestPacking();
        //         updatePUCell2PackingSite();
        //         replaceCnt = timingDrivenDetailedPlacement_swap(0.5, 100, true, pathCriticalRatio);
        //         if (replaceCnt == 0)
        //             pathCriticalRatio = 0.8;
        //         setPULocationToPackedSite();
        //         timingOptimizer->conductStaticTimingAnalysis();

        //         newCP = timingOptimizer->getMaxDelay();
        //         if (newCP < minCP + 0.015)
        //         {
        //             minCP = newCP;
        //             recordBestPacking();
        //             bestPackingUpdated = true;
        //         }
        //         print_info("ParallelCLBPacker: critical path delay=" + std::to_string(newCP) +
        //                    " clock period=" + std::to_string(clockPeriod));
        //     }
        //     useLocalFineTune = false;
        // }

        recoverToBestPacking();
        updatePUCell2PackingSite();
        setPULocationToPackedSite();
        timingOptimizer->conductStaticTimingAnalysis();

#endif
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < packNum; i++)
            packingSites[i]->finalMapToSlots();
#ifdef TIMINGDP
        timingOptimizer->conductStaticTimingAnalysis();
        timingDrivenDetailedPlacement_LUTFFPairReloacationAfterSlotMapping();
#pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < packNum; i++)
            packingSites[i]->finalMapToSlots();
#endif
    }

    // ensure the packing is legal
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                assert(packingSite->getDeterminedClusterInSite()->checkCellCorrectness(nullptr, true));
            }
        }
    }

    updatePUCell2PackingSite();
    setPULocationToPackedSite();
    placementInfo->updateElementBinGrid();
    dumpFinalPacking();
}

inline float getAngle(float v1x, float v1y, float v2x, float v2y)
{
    float angle = atan2(v2y, v2x) - atan2(v1y, v1x);
    if (angle > M_PI)
    {
        angle -= 2 * M_PI;
    }
    else if (angle <= -M_PI)
    {
        angle += 2 * M_PI;
    }
    return angle;
}

int ParallelCLBPacker::timingDrivenDetailedPlacement_shortestPath(int iterId, float displacementRatio,
                                                                  float pathCriticalRatio)
{
    AMF_PROFILE_FUNCTION("detailed_placement");
    std::vector<std::unique_ptr<std::vector<DeviceInfo::DeviceSite *>>> neighborOwners;
    auto ownNeighbors = [&](std::vector<DeviceInfo::DeviceSite *> *p) {
        neighborOwners.emplace_back(p); return p;
    };
    print_status("ParallelCLBPacker: conducting timing-driven detailed placement based on shortest path.");
    auto oriCellIdsInCriticalPaths = timingOptimizer->findCriticalPaths(pathCriticalRatio, true, 1 + iterId * 20);
    std::set<PlacementInfo::PlacementUnit *> PUsTouched;
    PUsTouched.clear();
    std::ofstream outfileTcl("./DetailedPlacementRecord");
    int replaceCnt = 0;
    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        neighborOwners.clear();
        // std::set<PackingCLBSite *> changedPackingSites;
        // std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> movedPUs;
        // movedPUs.clear();
        // changedPackingSites.clear();
        std::map<int, std::vector<PackingCLBSite *>> cellId2CandidateSites;
        std::set<PackingCLBSite *> sitesCandidates;
        std::map<PackingCLBSite *, std::unique_ptr<PackingCLBSite::PackingCLBCluster>> site2TrialCluster;
        cellId2CandidateSites.clear();

        std::vector<int> cellIdsInCriticalPath;
        PlacementInfo::PlacementUnit *lastPU = nullptr;
        std::set<PlacementInfo::PlacementUnit *> PUsInCriticalPathSet;
        for (auto cellId : oriCellIdsInCriticalPath)
        {
            if (PUsInCriticalPathSet.find(placementInfo->getPlacementUnitByCellId(cellId)) ==
                PUsInCriticalPathSet.end())
            {
                PUsInCriticalPathSet.insert(placementInfo->getPlacementUnitByCellId(cellId));
                cellIdsInCriticalPath.push_back(cellId);
            }
        }
        // std::cout << "processing endpoint [" << designInfo->getCells()[cellIdsInCriticalPath[0]] << "]  with "
        //           << cellIdsInCriticalPath.size() << " nodes in path.\n";

        bool CellUnmapped = false;
        for (auto cellId : cellIdsInCriticalPath)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            auto curPackingSite = cellId2PackingSite[cellId];
            auto curCell = designInfo->getCells()[cellId];
            if (!curPackingSite)
            {
                CellUnmapped = true;
                // std::cout << "a cell of the PU does not map to sites: " << curCell << "\n";
                break;
            }
            assert(curPackingSite);
            cellId2CandidateSites[cellId] = std::vector<PackingCLBSite *>(1, curPackingSite);
            sitesCandidates.insert(curPackingSite);
            if (!curPackingSite->checkIsNonCLBSite())
                site2TrialCluster[curPackingSite].reset(new PackingCLBSite::PackingCLBCluster(curPackingSite->getDeterminedClusterInSite()));
        }
        if (CellUnmapped)
        {
            continue;
        }
        // find candidate sites for each possible PU

        std::map<PlacementInfo::PlacementUnit *, std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>>
            PU2SiteConeOffsetPairs;
        std::map<PlacementInfo::PlacementUnit *, std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>>
            PU2SiteNeighborOffsetPairs;

        float displacementThr = 5.0 * displacementRatio;
        if (displacementThr < 1)
            displacementThr = 1;

        int numCellInPath = cellIdsInCriticalPath.size();

        for (int orderI = 0; orderI < numCellInPath; orderI++)
        {
            auto cellId = cellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            if (PUsTouched.find(curPU) == PUsTouched.end())
            {
                PU2SiteConeOffsetPairs[curPU] = std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(nullptr, 0);
                PU2SiteNeighborOffsetPairs[curPU] = std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(nullptr, 0);
            }
        }

        //#pragma omp parallel for
        if (!noSectorGuided)
        {
            for (int orderI = 0; orderI < numCellInPath; orderI++)
            {
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                if (PUsTouched.find(curPU) == PUsTouched.end())
                {
                    auto curCell = designInfo->getCells()[cellId];
                    // std::cout << curCell << " has following candidates: \n";
                    if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() &&
                        !curPU->checkHasBRAM() && !(curPU->checkHasDSP() || curPU->checkHasURAM()))
                    {
                        float v1x = 0, v1y = 0, v2x = 0, v2y = 0;
                        PackingCLBSite *succSite = nullptr, *predSite = nullptr;
                        if (orderI > 0 && orderI < cellIdsInCriticalPath.size() - 1)
                        {
                            predSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                            int predSiteOrderId = orderI - 1;
                            auto curSite = cellId2PackingSite[cellId];
                            succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                            int succSiteOrderId = orderI + 1;
                            assert(predSite && curSite && succSite);
                            // while (curSite == predSite && predSiteOrderId - 1 >= 0)
                            // {
                            //     predSiteOrderId--;
                            //     predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                            // }
                            // while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                            // {
                            //     succSiteOrderId++;
                            //     succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                            // }
                            v1x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                            v1y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                            v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                            v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                        }
                        else if (orderI == 0)
                        {
                            auto curSite = cellId2PackingSite[cellId];
                            predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                            assert(curSite && succSite);
                            int succSiteOrderId = orderI + 1;
                            while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                            {
                                succSiteOrderId++;
                                succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                            }
                            v1x = v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                            v1y = v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                        }
                        else
                        {
                            predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                            auto curSite = cellId2PackingSite[cellId];
                            assert(predSite && curSite);
                            int predSiteOrderId = orderI - 1;
                            while (curSite == predSite && predSiteOrderId - 1 >= 0)
                            {
                                predSiteOrderId--;
                                predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                            }
                            v1x = v2x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                            v1y = v2y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                        }
                        float neighborDis =
                            std::fabs(v1x) + y2xRatio * std::fabs(v1y) + std::fabs(v2x) + y2xRatio * std::fabs(v2y);
                        // if (std::fabs(v1x) + std::fabs(v1y) + std::fabs(v2x) + std::fabs(v2y) < 0.1)
                        //     continue;

                        int clockRegionX0, clockRegionY0;
                        deviceInfo->getClockRegionByLocation(PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                                             PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(),
                                                             clockRegionX0, clockRegionY0);
                        int clockRegionX1, clockRegionY1;
                        deviceInfo->getClockRegionByLocation(predSite->getCLBSite()->X(), predSite->getCLBSite()->Y(),
                                                             clockRegionX1, clockRegionY1);
                        int clockRegionX2, clockRegionY2;
                        deviceInfo->getClockRegionByLocation(succSite->getCLBSite()->X(), succSite->getCLBSite()->Y(),
                                                             clockRegionX2, clockRegionY2);

                        float disRange = displacementThr;
                        if (orderI == cellIdsInCriticalPath.size() - 1 || orderI == 0)
                        {
                            disRange = 2;
                            if (neighborDis > 5)
                                disRange = 3;
                        }
                        else if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                              PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                                 timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                              PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                                 disRange < 2.5)
                            disRange = 2.5;

                        if (std::fabs(getAngle(v1x, v1y, v2x, v2y)) < M_PI / 2)
                        {
                            assert(PUId2PackingCLBSite[curPU->getId()]);
                            assert(!PU2SiteConeOffsetPairs[curPU].first);
                            auto tmp_cone = ownNeighbors(findNeiborSitesFromBinGrid(
                                DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0,
                                (neighborDis >= 0.1 || disRange > 1) ? disRange : 1, y2xRatio, false, v1x, v1y, v2x,
                                v2y, 20, false));
                            PU2SiteConeOffsetPairs[curPU] =
                                std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_cone, 0);
                        }
                    }
                }
            }
        }

        //#pragma omp parallel for
        for (int orderI = 0; orderI < numCellInPath; orderI++)
        {
            auto cellId = cellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            auto curCell = designInfo->getCells()[cellId];
            if (PUsTouched.find(curPU) != PUsTouched.end())
            {
                continue;
            }
            // std::cout << curCell << " has following candidates: \n";
            if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() && !curPU->checkHasBRAM() &&
                !(curPU->checkHasDSP() || curPU->checkHasURAM()))
            {

                float disRange = 0.8 + displacementRatio;

                float v1x = 0, v1y = 0, v2x = 0, v2y = 0;
                PackingCLBSite *succSite = nullptr, *predSite = nullptr;
                if (orderI > 0 && orderI < cellIdsInCriticalPath.size() - 1)
                {
                    predSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                    int predSiteOrderId = orderI - 1;
                    auto curSite = cellId2PackingSite[cellId];
                    succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                    int succSiteOrderId = orderI + 1;
                    assert(predSite && curSite && succSite);
                    // while (curSite == predSite && predSiteOrderId - 1 >= 0)
                    // {
                    //     predSiteOrderId--;
                    //     predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                    // }
                    // while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() -
                    // 1)
                    // {
                    //     succSiteOrderId++;
                    //     succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                    // }
                    v1x = predSite->getCLBSite()->X();
                    v1y = predSite->getCLBSite()->Y();
                    v2x = succSite->getCLBSite()->X();
                    v2y = succSite->getCLBSite()->Y();
                }
                else if (orderI == 0)
                {
                    auto curSite = cellId2PackingSite[cellId];
                    predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                    assert(curSite && succSite);
                    int succSiteOrderId = orderI + 1;
                    while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                    {
                        succSiteOrderId++;
                        succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                    }
                    v1x = v2x = succSite->getCLBSite()->X();
                    v1y = v2y = succSite->getCLBSite()->Y();
                }
                else
                {
                    predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                    auto curSite = cellId2PackingSite[cellId];
                    assert(predSite && curSite);
                    int predSiteOrderId = orderI - 1;
                    while (curSite == predSite && predSiteOrderId - 1 >= 0)
                    {
                        predSiteOrderId--;
                        predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                    }
                    v1x = v2x = predSite->getCLBSite()->X();
                    v1y = v2y = predSite->getCLBSite()->Y();
                }

                int clockRegionX0, clockRegionY0;
                deviceInfo->getClockRegionByLocation(PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                                     PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(),
                                                     clockRegionX0, clockRegionY0);
                int clockRegionX1, clockRegionY1;
                deviceInfo->getClockRegionByLocation(v1x, v1y, clockRegionX1, clockRegionY1);
                int clockRegionX2, clockRegionY2;
                deviceInfo->getClockRegionByLocation(v2x, v2y, clockRegionX2, clockRegionY2);

                if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                 PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                    timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                 PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()))
                    disRange = 3.5;
                else if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                      PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) ||
                         timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                      PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()))
                    disRange = 3;

                if (useBigWindow)
                    disRange = std::max(displacementThr, disRange);

                assert(!PU2SiteNeighborOffsetPairs[curPU].first);
                auto tmp_region = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange, y2xRatio, false));
                PU2SiteNeighborOffsetPairs[curPU] =
                    std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_region, 0);
            }
            else if (!curPU->isLocked() && !curPU->checkHasCARRY() && (curPU->checkHasDSP() || curPU->checkHasURAM()) && curPU->getDSPNum() == 1)
            {
                float disRange = 2.25;
                if (useBigWindow)
                    disRange = std::max(displacementThr, disRange);

                assert(!PU2SiteNeighborOffsetPairs[curPU].first);
                auto tmp_region = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_DSP48E2, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange, y2xRatio, false));
                PU2SiteNeighborOffsetPairs[curPU] =
                    std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_region, 0);
            }
        }

        // std::cout << "siteCandidateLimit=" << (useBigWindow ? 25 : 10) << "\n";
        for (int siteCandidateLimit = 1; siteCandidateLimit <= (useBigWindow ? 25 : 10); siteCandidateLimit++)
        {

            for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
            {
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                if (PUsTouched.find(curPU) != PUsTouched.end())
                {
                    continue;
                }
                auto curCell = designInfo->getCells()[cellId];
                // std::cout << curCell << " has following candidates: \n";
                if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() &&
                    !curPU->checkHasBRAM() && !(curPU->checkHasDSP() || curPU->checkHasURAM()))
                {
                    assert(PUId2PackingCLBSite[curPU->getId()]);
                    if (!PU2SiteConeOffsetPairs[curPU].first)
                        continue;

                    std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell_cone =
                        PU2SiteConeOffsetPairs[curPU].first;
                    int candidateOffsetIndex = PU2SiteConeOffsetPairs[curPU].second;
                    for (int siteIndex = candidateOffsetIndex; siteIndex < candidateSitesToPlaceTheCell_cone->size();
                         siteIndex++)
                    {
                        auto curDeviceSite = (*candidateSitesToPlaceTheCell_cone)[siteIndex];
                        PU2SiteConeOffsetPairs[curPU].second = candidateOffsetIndex;
                        if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                            continue;
                        PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                        if (cellId2CandidateSites[cellId].size() < siteCandidateLimit)
                        {
                            bool duplicate = false;
                            for (auto existCandidate : cellId2CandidateSites[cellId])
                            {
                                if (existCandidate == candidatePackingSite)
                                    duplicate = true;
                            }
                            if (duplicate)
                                continue;
                            PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                            if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                            {
                                if (!candidatePackingSite->getDeterminedClusterInSite())
                                {
                                    auto determinedClusterInSite =
                                        new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                                    candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                                }
                                trialCluster = new PackingCLBSite::PackingCLBCluster(
                                    candidatePackingSite->getDeterminedClusterInSite());
                                site2TrialCluster[candidatePackingSite].reset(trialCluster);
                            }
                            else
                            {
                                trialCluster = site2TrialCluster[candidatePackingSite].get();
                            }

                            if (trialCluster->checkAddPU(curPU))
                            {
                                assert(trialCluster->addPU(curPU));
                                cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                                sitesCandidates.insert(candidatePackingSite);
                            }
                        }

                        // }
                    }
                }
            }

            for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
            {
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                auto curCell = designInfo->getCells()[cellId];
                if (PUsTouched.find(curPU) != PUsTouched.end())
                {
                    continue;
                }
                // std::cout << curCell << " has following candidates: \n";
                if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() &&
                    !curPU->checkHasBRAM() && !(curPU->checkHasDSP() || curPU->checkHasURAM()))
                {
                    if (cellId2CandidateSites[cellId].size() < siteCandidateLimit + 1)
                    {
                        if (!PU2SiteNeighborOffsetPairs[curPU].first)
                            continue;

                        std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell =
                            PU2SiteNeighborOffsetPairs[curPU].first;
                        int candidateOffsetIndex = PU2SiteNeighborOffsetPairs[curPU].second;
                        for (int siteIndex = candidateOffsetIndex; siteIndex < candidateSitesToPlaceTheCell->size();
                             siteIndex++)
                        {
                            auto curDeviceSite = (*candidateSitesToPlaceTheCell)[siteIndex];
                            PU2SiteNeighborOffsetPairs[curPU].second = candidateOffsetIndex;
                            if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                                continue;
                            PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                            bool duplicate = false;
                            for (auto existCandidate : cellId2CandidateSites[cellId])
                            {
                                if (existCandidate == candidatePackingSite)
                                    duplicate = true;
                            }
                            if (duplicate)
                                continue;
                            PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                            if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                            {
                                if (!candidatePackingSite->getDeterminedClusterInSite())
                                {
                                    auto determinedClusterInSite =
                                        new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                                    candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                                }
                                trialCluster = new PackingCLBSite::PackingCLBCluster(
                                    candidatePackingSite->getDeterminedClusterInSite());
                                site2TrialCluster[candidatePackingSite].reset(trialCluster);
                            }
                            else
                            {
                                trialCluster = site2TrialCluster[candidatePackingSite].get();
                            }

                            if (trialCluster->checkAddPU(curPU))
                            {
                                assert(trialCluster->addPU(curPU));
                                cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                                sitesCandidates.insert(candidatePackingSite);
                            }
                        }
                    }
                }
                else if (!curPU->isLocked() && !curPU->checkHasCARRY() && (curPU->checkHasDSP() || curPU->checkHasURAM()) &&
                         curPU->getDSPNum() == 1)
                {
                    if (cellId2CandidateSites[cellId].size() < siteCandidateLimit + 1)
                    {
                        if (!PU2SiteNeighborOffsetPairs[curPU].first)
                            continue;

                        std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell =
                            PU2SiteNeighborOffsetPairs[curPU].first;
                        int candidateOffsetIndex = PU2SiteNeighborOffsetPairs[curPU].second;
                        for (int siteIndex = candidateOffsetIndex; siteIndex < candidateSitesToPlaceTheCell->size();
                             siteIndex++)
                        {
                            auto curDeviceSite = (*candidateSitesToPlaceTheCell)[siteIndex];
                            PU2SiteNeighborOffsetPairs[curPU].second = candidateOffsetIndex;

                            assert(deviceSite2PackingSite.find(curDeviceSite) != deviceSite2PackingSite.end());
                            PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                            bool duplicate = false;
                            for (auto existCandidate : cellId2CandidateSites[cellId])
                            {
                                if (existCandidate == candidatePackingSite)
                                    duplicate = true;
                            }
                            if (duplicate)
                                continue;

                            if (candidatePackingSite->getNonCLBCell())
                                continue;

                            if (sitesCandidates.find(candidatePackingSite) == sitesCandidates.end())
                                cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                            sitesCandidates.insert(candidatePackingSite);
                        }
                    }
                }

                // for (auto packingSite : cellId2CandidateSites[cellId])
                // {
                //     std::cout << "      " << packingSite->getCLBSite()->getName() << "\n";
                // }
            }
        }


        // calculate the shortest paths
        std::vector<std::vector<float>> shortestPath_LayerSite;
        std::vector<std::vector<int>> shortestPath_LayerSite_backtrace;
        shortestPath_LayerSite.push_back(
            std::vector<float>(cellId2CandidateSites[cellIdsInCriticalPath[0]].size(), 0.0));
        shortestPath_LayerSite_backtrace.push_back(
            std::vector<int>(cellId2CandidateSites[cellIdsInCriticalPath[0]].size(), -1));

        if (iterId > 100)
        {
            outfileTcl << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>\nCellsCandidate:\n";
            for (int i = 0; i < cellIdsInCriticalPath.size(); i++)
            {
                auto curCellId = cellIdsInCriticalPath[i];
                auto curCell = designInfo->getCells()[curCellId];
                auto &curCandidates = cellId2CandidateSites[curCellId];
                outfileTcl << "cell: " << curCell << "\n";
                for (int k = 0; k < curCandidates.size(); k++)
                {
                    outfileTcl << "      " << cellId2CandidateSites[curCellId][k]->getCLBSite()->getName() << "\n";
                }
            }
        }

        int bestEndChoice = -1;
        float bestChoiceDelay = 100000;
        for (int i = 1; i < cellIdsInCriticalPath.size(); i++)
        {
            auto predCell = cellIdsInCriticalPath[i - 1];
            auto curCell = cellIdsInCriticalPath[i];
            auto &predCandidates = cellId2CandidateSites[predCell];
            auto &curCandidates = cellId2CandidateSites[curCell];
            shortestPath_LayerSite.push_back(std::vector<float>(cellId2CandidateSites[curCell].size(), 100000.0));
            shortestPath_LayerSite_backtrace.push_back(std::vector<int>(cellId2CandidateSites[curCell].size(), -1));

            for (int j = 0; j < predCandidates.size(); j++)
            {
                for (int k = 0; k < curCandidates.size(); k++)
                {
                    float delay = timingOptimizer->getDelayByModel(
                        predCandidates[j]->getCLBSite()->X(), predCandidates[j]->getCLBSite()->Y(),
                        curCandidates[k]->getCLBSite()->X(), curCandidates[k]->getCLBSite()->Y());

                    if (shortestPath_LayerSite[i][k] > shortestPath_LayerSite[i - 1][j] + delay)
                    {
                        shortestPath_LayerSite[i][k] = shortestPath_LayerSite[i - 1][j] + delay;
                        shortestPath_LayerSite_backtrace[i][k] = j;

                        if (i == cellIdsInCriticalPath.size() - 1)
                        {
                            if (shortestPath_LayerSite[i][k] < bestChoiceDelay)
                            {
                                bestChoiceDelay = shortestPath_LayerSite[i][k];
                                bestEndChoice = k;
                            }
                        }
                    }
                }
            }
        }

        for (int i = shortestPath_LayerSite.size() - 1; i >= 0; i--)
        {
            assert(bestEndChoice >= 0);
            if (bestEndChoice > 0)
            {
                auto curCellId = cellIdsInCriticalPath[i];
                auto curPU = placementInfo->getPlacementUnitByCellId(curCellId);

                auto &curCandidates = cellId2CandidateSites[curCellId];

                auto targetPackingSite = cellId2CandidateSites[curCellId][bestEndChoice];

                // movedPUs.insert(curPU);
                // changedPackingSites.insert(targetPackingSite);
                // changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);

                if (curPU->getDSPNum() == 1)
                {
                    cellId2CandidateSites[curCellId][0]->clearNonCLBCell();
                    assert(bestEndChoice < cellId2CandidateSites[curCellId].size());
                    assert(!targetPackingSite->getNonCLBCell());
                    targetPackingSite->setNonCLBCell(designInfo->getCells()[curCellId]);
                    PUId2PackingCLBSite[curPU->getId()] = targetPackingSite;
                    cellId2PackingSite[curCellId] = targetPackingSite;
                    placementInfo->getPULegalSite()[curPU] =
                        std::vector<DeviceInfo::DeviceSite *>(1, targetPackingSite->getCLBSite());
                    // curPU->setAnchorLocationAndForgetTheOriginalOne(targetPackingSite->getCLBSite()->X(),
                    //                                                 targetPackingSite->getCLBSite()->Y());
                    // placementInfo->addPUIntoClockColumn(curPU, targetPackingSite->getCLBSite());
                }
                else
                {
                    if (!targetPackingSite->getDeterminedClusterInSite()->checkAddPU(curPU))
                    {
                        bestEndChoice = shortestPath_LayerSite_backtrace[i][bestEndChoice];
                        continue;
                    }

                    cellId2CandidateSites[curCellId][0]->getDeterminedClusterInSite()->removePUToConstructDetCluster(
                        curPU);

                    assert(bestEndChoice < cellId2CandidateSites[curCellId].size());
                    assert(targetPackingSite->getDeterminedClusterInSite()->addPU(curPU));
                    PUId2PackingCLBSite[curPU->getId()] = targetPackingSite;
                    placementInfo->addPUIntoClockColumn(curPU, targetPackingSite->getCLBSite());
                    auto cellSet = targetPackingSite->getDeterminedClusterInSite()->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = targetPackingSite;
                    }
                    // curPU->setAnchorLocationAndForgetTheOriginalOne(targetPackingSite->getCLBSite()->X(),
                    //                                                 targetPackingSite->getCLBSite()->Y());
                }

                replaceCnt++;
            }
            bestEndChoice = shortestPath_LayerSite_backtrace[i][bestEndChoice];
        }

        for (int i = 0; i < shortestPath_LayerSite.size() - 1; i++)
        {
            auto curCellId = cellIdsInCriticalPath[i];
            auto curPU = placementInfo->getPlacementUnitByCellId(curCellId);
            auto nextCellId = cellIdsInCriticalPath[i + 1];
            auto nextPU = placementInfo->getPlacementUnitByCellId(nextCellId);
            if (curPU->checkHasCARRY() || curPU->checkHasLUTRAM() || curPU->checkHasBRAM() || (curPU->checkHasDSP() || curPU->checkHasURAM()) ||
                nextPU->checkHasCARRY() || nextPU->checkHasLUTRAM() || nextPU->checkHasBRAM() ||
                (nextPU->checkHasDSP() || nextPU->checkHasURAM()) || PUsTouched.find(curPU) != PUsTouched.end() ||
                PUsTouched.find(nextPU) != PUsTouched.end() || curPU == nextPU || curPU->isLocked() ||
                nextPU->isLocked())
            {
                continue;
            }
            if (PUId2PackingCLBSite[curPU->getId()] == PUId2PackingCLBSite[nextPU->getId()])
                continue;

            float oriDis = 0;
            float newDis = 0;
            auto curPackingSite = PUId2PackingCLBSite[curPU->getId()];
            auto nextPackingSite = PUId2PackingCLBSite[nextPU->getId()];
            float curX = PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X();
            float curY = PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y();
            float nextX = PUId2PackingCLBSite[nextPU->getId()]->getCLBSite()->X();
            float nextY = PUId2PackingCLBSite[nextPU->getId()]->getCLBSite()->Y();

            if (timingOptimizer->getDelayByModel(nextX, nextY, curX, curY) > 0.5)
                continue;

            if (i > 0)
            {
                auto prevCellId = cellIdsInCriticalPath[i - 1];
                assert(cellId2PackingSite[prevCellId]);
                float headX = cellId2PackingSite[prevCellId]->getCLBSite()->X();
                float headY = cellId2PackingSite[prevCellId]->getCLBSite()->Y();
                oriDis += timingOptimizer->getDelayByModel(headX, headY, curX, curY);
                newDis += timingOptimizer->getDelayByModel(headX, headY, nextX, nextY);
            }

            if (i < shortestPath_LayerSite.size() - 1)
            {
                auto succCellId = cellIdsInCriticalPath[i + 1];
                assert(cellId2PackingSite[succCellId]);
                float tailX = cellId2PackingSite[succCellId]->getCLBSite()->X();
                float tailY = cellId2PackingSite[succCellId]->getCLBSite()->Y();
                newDis += timingOptimizer->getDelayByModel(tailX, tailY, curX, curY);
                oriDis += timingOptimizer->getDelayByModel(tailX, tailY, nextX, nextY);
            }

            if (newDis < oriDis)
            {
                auto trialClusterWithCurPU =
                    new PackingCLBSite::PackingCLBCluster(curPackingSite->getDeterminedClusterInSite());
                auto trialClusterWithNextPU =
                    new PackingCLBSite::PackingCLBCluster(nextPackingSite->getDeterminedClusterInSite());
                assert(trialClusterWithCurPU->contains(curPU));
                trialClusterWithCurPU->removePUToConstructDetCluster(curPU);
                assert(trialClusterWithNextPU->contains(nextPU));
                trialClusterWithNextPU->removePUToConstructDetCluster(nextPU);
                if (trialClusterWithCurPU->addPU(nextPU))
                {
                    if (trialClusterWithNextPU->addPU(curPU))
                    {
                        // movedPUs.insert(curPU);
                        // movedPUs.insert(nextPU);
                        // changedPackingSites.insert(curPackingSite);
                        // changedPackingSites.insert(nextPackingSite);
                        // curPU->setAnchorLocationAndForgetTheOriginalOne(nextPackingSite->getCLBSite()->X(),
                        //                                                 nextPackingSite->getCLBSite()->Y());
                        // nextPU->setAnchorLocationAndForgetTheOriginalOne(curPackingSite->getCLBSite()->X(),
                        //                                                  curPackingSite->getCLBSite()->Y());

                        curPackingSite->getDeterminedClusterInSite()->removePUToConstructDetCluster(curPU);
                        assert(curPackingSite->getDeterminedClusterInSite()->addPU(nextPU));
                        nextPackingSite->getDeterminedClusterInSite()->removePUToConstructDetCluster(nextPU);
                        assert(nextPackingSite->getDeterminedClusterInSite()->addPU(curPU));

                        PUId2PackingCLBSite[curPU->getId()] = nextPackingSite;
                        placementInfo->addPUIntoClockColumn(curPU, nextPackingSite->getCLBSite());
                        auto cellSet = nextPackingSite->getDeterminedClusterInSite()->getCellSet();
                        for (auto cell : cellSet)
                        {
                            cellId2PackingSite[cell->getCellId()] = nextPackingSite;
                        }

                        PUId2PackingCLBSite[nextPU->getId()] = curPackingSite;
                        placementInfo->addPUIntoClockColumn(nextPU, curPackingSite->getCLBSite());
                        cellSet = curPackingSite->getDeterminedClusterInSite()->getCellSet();
                        for (auto cell : cellSet)
                        {
                            cellId2PackingSite[cell->getCellId()] = curPackingSite;
                        }
                    }
                }

                delete trialClusterWithCurPU;
                delete trialClusterWithNextPU;
            }
        }

        // float oriMaxFanoutDelay = timingOptimizer->getMaxDelay();
        // timingOptimizer->incrementalStaticTimingAnalysis(movedPUs, true);
        // if (timingOptimizer->getMaxIncrementalDelay() > oriMaxFanoutDelay)
        // {
        //     // print_warning("ParallelCLBPacker: critical path delay is increased to " +
        //     //               std::to_string(timingOptimizer->getMaxIncrementalDelay()) + " and oriMaxFanoutDelay=" +
        //     //               std::to_string(oriMaxFanoutDelay) + " . Give up this path optimization and recover");
        //     partiallyRecoverToBestPacking(changedPackingSites);
        //     timingOptimizer->recoverArrivalDelayFromLastIncrementalSTA();
        //     movedPUs.clear();
        // }
        // else
        // {
        //     for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
        //     {
        //         auto cellId = cellIdsInCriticalPath[orderI];
        //         auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
        //         PUsTouched.insert(curPU);
        //     }

        //     // print_warning("ParallelCLBPacker: critical path delay is " +
        //     //               std::to_string(timingOptimizer->getMaxIncrementalDelay()));
        //     partiallyRecordBestPacking(changedPackingSites);
        //     movedPUs.clear();
        // }

        for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
        {
            auto cellId = cellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            PUsTouched.insert(curPU);
        }
    }

    print_status("ParallelCLBPacker: conducted timing-driven detailed placement (shortest path) and " +
                 std::to_string(replaceCnt) + " PlacementUnits are replaced.");
    outfileTcl.close();
    return replaceCnt;
}

int ParallelCLBPacker::timingDrivenDetailedPlacement_shortestPath(int iterId, float displacementRatio,
                                                                  float pathCriticalRatio, int lowIndex, int highIndex)
{
    AMF_PROFILE_FUNCTION("detailed_placement");
    std::vector<std::unique_ptr<std::vector<DeviceInfo::DeviceSite *>>> neighborOwners;
    auto ownNeighbors = [&](std::vector<DeviceInfo::DeviceSite *> *p) {
        neighborOwners.emplace_back(p); return p;
    };
    print_status("ParallelCLBPacker: conducting timing-driven detailed placement based on shortest path (partial).");
    auto oriCellIdsInCriticalPaths = timingOptimizer->findCriticalPaths(pathCriticalRatio, true, 1 + iterId * 20);
    std::set<PlacementInfo::PlacementUnit *> PUsTouched;
    PUsTouched.clear();
    std::ofstream outfileTcl("./DetailedPlacementRecord");
    int replaceCnt = 0;
    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        neighborOwners.clear();
        std::map<int, std::vector<PackingCLBSite *>> cellId2CandidateSites;
        std::set<PackingCLBSite *> sitesCandidates;
        std::map<PackingCLBSite *, std::unique_ptr<PackingCLBSite::PackingCLBCluster>> site2TrialCluster;
        cellId2CandidateSites.clear();

        std::vector<int> cellIdsInCriticalPath;
        PlacementInfo::PlacementUnit *lastPU = nullptr;
        std::set<PlacementInfo::PlacementUnit *> PUsInCriticalPathSet;
        for (auto cellId : oriCellIdsInCriticalPath)
        {
            if (PUsInCriticalPathSet.find(placementInfo->getPlacementUnitByCellId(cellId)) ==
                PUsInCriticalPathSet.end())
            {
                PUsInCriticalPathSet.insert(placementInfo->getPlacementUnitByCellId(cellId));
                cellIdsInCriticalPath.push_back(cellId);
            }
        }
        // std::cout << "processing endpoint [" << designInfo->getCells()[cellIdsInCriticalPath[0]] << "]  with "
        //           << cellIdsInCriticalPath.size() << " nodes in path.\n";

        bool CellUnmapped = false;
        for (auto cellId : cellIdsInCriticalPath)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            auto curPackingSite = cellId2PackingSite[cellId];
            auto curCell = designInfo->getCells()[cellId];
            if (!curPackingSite)
            {
                CellUnmapped = true;
                // std::cout << "a cell of the PU does not map to sites: " << curCell << "\n";
                break;
            }
            assert(curPackingSite);
            cellId2CandidateSites[cellId] = std::vector<PackingCLBSite *>(1, curPackingSite);
            sitesCandidates.insert(curPackingSite);
            if (!curPackingSite->checkIsNonCLBSite())
                site2TrialCluster[curPackingSite].reset(new PackingCLBSite::PackingCLBCluster(curPackingSite->getDeterminedClusterInSite()));
        }
        if (CellUnmapped)
        {
            continue;
        }
        // find candidate sites for each possible PU

        std::map<PlacementInfo::PlacementUnit *, std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>>
            PU2SiteConeOffsetPairs;
        std::map<PlacementInfo::PlacementUnit *, std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>>
            PU2SiteNeighborOffsetPairs;

        // std::cout << "siteCandidateLimit=" << (useBigWindow ? 25 : 10) << "\n";

        for (int siteCandidateLimit = 1; siteCandidateLimit <= (useBigWindow ? 25 : 10); siteCandidateLimit++)
        {
            float displacementThr = 5.0 * displacementRatio;
            if (displacementThr < 1)
                displacementThr = 1;
            for (int orderI = lowIndex; orderI <= highIndex; orderI++)
            {
                if (orderI < 0 || orderI > cellIdsInCriticalPath.size() - 1)
                    continue;
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                if (PUsTouched.find(curPU) != PUsTouched.end())
                {
                    continue;
                }
                auto curCell = designInfo->getCells()[cellId];
                // std::cout << curCell << " has following candidates: \n";
                if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() &&
                    !curPU->checkHasBRAM() && !(curPU->checkHasDSP() || curPU->checkHasURAM()))
                {
                    float v1x = 0, v1y = 0, v2x = 0, v2y = 0;
                    PackingCLBSite *succSite = nullptr, *predSite = nullptr;
                    if (orderI > 0 && orderI < cellIdsInCriticalPath.size() - 1)
                    {
                        predSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                        int predSiteOrderId = orderI - 1;
                        auto curSite = cellId2PackingSite[cellId];
                        succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                        int succSiteOrderId = orderI + 1;
                        assert(predSite && curSite && succSite);
                        // while (curSite == predSite && predSiteOrderId - 1 >= 0)
                        // {
                        //     predSiteOrderId--;
                        //     predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                        // }
                        // while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                        // {
                        //     succSiteOrderId++;
                        //     succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                        // }
                        v1x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                        v1y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                        v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                        v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    }
                    else if (orderI == 0)
                    {
                        auto curSite = cellId2PackingSite[cellId];
                        predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                        assert(curSite && succSite);
                        int succSiteOrderId = orderI + 1;
                        while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                        {
                            succSiteOrderId++;
                            succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                        }
                        v1x = v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                        v1y = v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    }
                    else
                    {
                        predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                        auto curSite = cellId2PackingSite[cellId];
                        assert(predSite && curSite);
                        int predSiteOrderId = orderI - 1;
                        while (curSite == predSite && predSiteOrderId - 1 >= 0)
                        {
                            predSiteOrderId--;
                            predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                        }
                        v1x = v2x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                        v1y = v2y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    }
                    float neighborDis =
                        std::fabs(v1x) + y2xRatio * std::fabs(v1y) + std::fabs(v2x) + y2xRatio * std::fabs(v2y);
                    // if (std::fabs(v1x) + std::fabs(v1y) + std::fabs(v2x) + std::fabs(v2y) < 0.1)
                    //     continue;

                    int clockRegionX0, clockRegionY0;
                    deviceInfo->getClockRegionByLocation(PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                                         PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(),
                                                         clockRegionX0, clockRegionY0);
                    int clockRegionX1, clockRegionY1;
                    deviceInfo->getClockRegionByLocation(predSite->getCLBSite()->X(), predSite->getCLBSite()->Y(),
                                                         clockRegionX1, clockRegionY1);
                    int clockRegionX2, clockRegionY2;
                    deviceInfo->getClockRegionByLocation(succSite->getCLBSite()->X(), succSite->getCLBSite()->Y(),
                                                         clockRegionX2, clockRegionY2);

                    float disRange = displacementThr;
                    if (orderI == cellIdsInCriticalPath.size() - 1 || orderI == 0)
                    {
                        disRange = 2;
                        if (neighborDis > 5)
                            disRange = 3;
                    }
                    else if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                          PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                             timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                          PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                             disRange < 2.5)
                        disRange = 2.5;

                    if (std::fabs(getAngle(v1x, v1y, v2x, v2y)) < M_PI / 2)
                    {
                        assert(PUId2PackingCLBSite[curPU->getId()]);

                        if (PU2SiteConeOffsetPairs.find(curPU) == PU2SiteConeOffsetPairs.end())
                        {
                            auto tmp_cone = ownNeighbors(findNeiborSitesFromBinGrid(
                                DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0,
                                (neighborDis >= 0.1 || disRange > 1) ? disRange : 1, y2xRatio, false, v1x, v1y, v2x,
                                v2y, 20, false));
                            PU2SiteConeOffsetPairs[curPU] =
                                std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_cone, 0);
                        }

                        std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell_cone =
                            PU2SiteConeOffsetPairs[curPU].first;
                        int candidateOffsetIndex = PU2SiteConeOffsetPairs[curPU].second;
                        for (int siteIndex = candidateOffsetIndex;
                             siteIndex < candidateSitesToPlaceTheCell_cone->size(); siteIndex++)
                        {
                            auto curDeviceSite = (*candidateSitesToPlaceTheCell_cone)[siteIndex];
                            PU2SiteConeOffsetPairs[curPU].second = candidateOffsetIndex;
                            if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                                continue;
                            PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                            if (cellId2CandidateSites[cellId].size() < siteCandidateLimit)
                            {
                                bool duplicate = false;
                                for (auto existCandidate : cellId2CandidateSites[cellId])
                                {
                                    if (existCandidate == candidatePackingSite)
                                        duplicate = true;
                                }
                                if (duplicate)
                                    continue;
                                PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                                if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                                {
                                    if (!candidatePackingSite->getDeterminedClusterInSite())
                                    {
                                        auto determinedClusterInSite =
                                            new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                                        candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                                    }
                                    trialCluster = new PackingCLBSite::PackingCLBCluster(
                                        candidatePackingSite->getDeterminedClusterInSite());
                                    site2TrialCluster[candidatePackingSite].reset(trialCluster);
                                }
                                else
                                {
                                    trialCluster = site2TrialCluster[candidatePackingSite].get();
                                }

                                if (trialCluster->checkAddPU(curPU))
                                {
                                    assert(trialCluster->addPU(curPU));
                                    cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                                    sitesCandidates.insert(candidatePackingSite);
                                }
                            }

                            // }
                        }
                    }
                }
            }

            for (int orderI = lowIndex; orderI <= highIndex; orderI++)
            {
                if (orderI < 0 || orderI > cellIdsInCriticalPath.size() - 1)
                    continue;
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                auto curCell = designInfo->getCells()[cellId];
                if (PUsTouched.find(curPU) != PUsTouched.end())
                {
                    continue;
                }
                // std::cout << curCell << " has following candidates: \n";
                if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() &&
                    !curPU->checkHasBRAM() && !(curPU->checkHasDSP() || curPU->checkHasURAM()))
                {
                    if (cellId2CandidateSites[cellId].size() < siteCandidateLimit + 1)
                    {
                        float disRange = 0.8 + displacementRatio;

                        float v1x = 0, v1y = 0, v2x = 0, v2y = 0;
                        PackingCLBSite *succSite = nullptr, *predSite = nullptr;
                        if (orderI > 0 && orderI < cellIdsInCriticalPath.size() - 1)
                        {
                            predSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                            int predSiteOrderId = orderI - 1;
                            auto curSite = cellId2PackingSite[cellId];
                            succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                            int succSiteOrderId = orderI + 1;
                            assert(predSite && curSite && succSite);
                            // while (curSite == predSite && predSiteOrderId - 1 >= 0)
                            // {
                            //     predSiteOrderId--;
                            //     predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                            // }
                            // while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                            // {
                            //     succSiteOrderId++;
                            //     succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                            // }
                            v1x = predSite->getCLBSite()->X();
                            v1y = predSite->getCLBSite()->Y();
                            v2x = succSite->getCLBSite()->X();
                            v2y = succSite->getCLBSite()->Y();
                        }
                        else if (orderI == 0)
                        {
                            auto curSite = cellId2PackingSite[cellId];
                            predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                            assert(curSite && succSite);
                            int succSiteOrderId = orderI + 1;
                            while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
                            {
                                succSiteOrderId++;
                                succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
                            }
                            v1x = v2x = succSite->getCLBSite()->X();
                            v1y = v2y = succSite->getCLBSite()->Y();
                        }
                        else
                        {
                            predSite = succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                            auto curSite = cellId2PackingSite[cellId];
                            assert(predSite && curSite);
                            int predSiteOrderId = orderI - 1;
                            while (curSite == predSite && predSiteOrderId - 1 >= 0)
                            {
                                predSiteOrderId--;
                                predSite = cellId2PackingSite[cellIdsInCriticalPath[predSiteOrderId]];
                            }
                            v1x = v2x = predSite->getCLBSite()->X();
                            v1y = v2y = predSite->getCLBSite()->Y();
                        }

                        int clockRegionX0, clockRegionY0;
                        deviceInfo->getClockRegionByLocation(PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                                             PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(),
                                                             clockRegionX0, clockRegionY0);
                        int clockRegionX1, clockRegionY1;
                        deviceInfo->getClockRegionByLocation(v1x, v1y, clockRegionX1, clockRegionY1);
                        int clockRegionX2, clockRegionY2;
                        deviceInfo->getClockRegionByLocation(v2x, v2y, clockRegionX2, clockRegionY2);

                        if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                         PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                            timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                         PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()))
                            disRange = 3.5;
                        else if (timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                              PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) ||
                                 timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                              PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()))
                            disRange = 3;
                        // std::cout << curCell << " has " << candidateSitesToPlaceTheCell->size()
                        //           << " neighbors and candidates are:\n";
                        if (PU2SiteNeighborOffsetPairs.find(curPU) == PU2SiteNeighborOffsetPairs.end())
                        {
                            auto tmp_region = ownNeighbors(findNeiborSitesFromBinGrid(
                                DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange, y2xRatio, false));
                            PU2SiteNeighborOffsetPairs[curPU] =
                                std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_region, 0);
                        }

                        std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell =
                            PU2SiteNeighborOffsetPairs[curPU].first;
                        int candidateOffsetIndex = PU2SiteNeighborOffsetPairs[curPU].second;
                        for (int siteIndex = candidateOffsetIndex; siteIndex < candidateSitesToPlaceTheCell->size();
                             siteIndex++)
                        {
                            auto curDeviceSite = (*candidateSitesToPlaceTheCell)[siteIndex];
                            PU2SiteNeighborOffsetPairs[curPU].second = candidateOffsetIndex;
                            if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                                continue;
                            PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                            bool duplicate = false;
                            for (auto existCandidate : cellId2CandidateSites[cellId])
                            {
                                if (existCandidate == candidatePackingSite)
                                    duplicate = true;
                            }
                            if (duplicate)
                                continue;
                            PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                            if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                            {
                                if (!candidatePackingSite->getDeterminedClusterInSite())
                                {
                                    auto determinedClusterInSite =
                                        new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                                    candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                                }
                                trialCluster = new PackingCLBSite::PackingCLBCluster(
                                    candidatePackingSite->getDeterminedClusterInSite());
                                site2TrialCluster[candidatePackingSite].reset(trialCluster);
                            }
                            else
                            {
                                trialCluster = site2TrialCluster[candidatePackingSite].get();
                            }

                            if (trialCluster->checkAddPU(curPU))
                            {
                                assert(trialCluster->addPU(curPU));
                                cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                                sitesCandidates.insert(candidatePackingSite);
                            }
                        }
                    }
                }
                else if (!curPU->isLocked() && !curPU->checkHasCARRY() && (curPU->checkHasDSP() || curPU->checkHasURAM()) &&
                         curPU->getDSPNum() == 1)
                {
                    if (cellId2CandidateSites[cellId].size() < siteCandidateLimit + 1)
                    {
                        float disRange = 2.25;

                        // std::cout << curCell << " has " << candidateSitesToPlaceTheCell->size()
                        //           << " neighbors and candidates are:\n";
                        if (PU2SiteNeighborOffsetPairs.find(curPU) == PU2SiteNeighborOffsetPairs.end())
                        {
                            auto tmp_region = ownNeighbors(findNeiborSitesFromBinGrid(
                                DesignInfo::CellType_DSP48E2, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange, y2xRatio, false));
                            PU2SiteNeighborOffsetPairs[curPU] =
                                std::pair<std::vector<DeviceInfo::DeviceSite *> *, int>(tmp_region, 0);
                        }

                        std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell =
                            PU2SiteNeighborOffsetPairs[curPU].first;
                        int candidateOffsetIndex = PU2SiteNeighborOffsetPairs[curPU].second;
                        for (int siteIndex = candidateOffsetIndex; siteIndex < candidateSitesToPlaceTheCell->size();
                             siteIndex++)
                        {
                            auto curDeviceSite = (*candidateSitesToPlaceTheCell)[siteIndex];
                            PU2SiteNeighborOffsetPairs[curPU].second = candidateOffsetIndex;

                            assert(deviceSite2PackingSite.find(curDeviceSite) != deviceSite2PackingSite.end());
                            PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                            bool duplicate = false;
                            for (auto existCandidate : cellId2CandidateSites[cellId])
                            {
                                if (existCandidate == candidatePackingSite)
                                    duplicate = true;
                            }
                            if (duplicate)
                                continue;

                            if (candidatePackingSite->getNonCLBCell())
                                continue;

                            if (sitesCandidates.find(candidatePackingSite) == sitesCandidates.end())
                                cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                            sitesCandidates.insert(candidatePackingSite);
                        }
                    }
                }

                // for (auto packingSite : cellId2CandidateSites[cellId])
                // {
                //     std::cout << "      " << packingSite->getCLBSite()->getName() << "\n";
                // }
            }
        }


        // calculate the shortest paths
        std::vector<std::vector<float>> shortestPath_LayerSite;
        std::vector<std::vector<int>> shortestPath_LayerSite_backtrace;
        shortestPath_LayerSite.push_back(
            std::vector<float>(cellId2CandidateSites[cellIdsInCriticalPath[0]].size(), 0.0));
        shortestPath_LayerSite_backtrace.push_back(
            std::vector<int>(cellId2CandidateSites[cellIdsInCriticalPath[0]].size(), -1));

        if (iterId > 100)
        {
            outfileTcl << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>>>\nCellsCandidate:\n";
            for (int i = 0; i < cellIdsInCriticalPath.size(); i++)
            {
                auto curCellId = cellIdsInCriticalPath[i];
                auto curCell = designInfo->getCells()[curCellId];
                auto &curCandidates = cellId2CandidateSites[curCellId];
                outfileTcl << "cell: " << curCell << "\n";
                for (int k = 0; k < curCandidates.size(); k++)
                {
                    outfileTcl << "      " << cellId2CandidateSites[curCellId][k]->getCLBSite()->getName() << "\n";
                }
            }
        }

        int bestEndChoice = -1;
        float bestChoiceDelay = 100000;
        for (int i = 1; i < cellIdsInCriticalPath.size(); i++)
        {
            auto predCell = cellIdsInCriticalPath[i - 1];
            auto curCell = cellIdsInCriticalPath[i];
            auto &predCandidates = cellId2CandidateSites[predCell];
            auto &curCandidates = cellId2CandidateSites[curCell];
            shortestPath_LayerSite.push_back(std::vector<float>(cellId2CandidateSites[curCell].size(), 100000.0));
            shortestPath_LayerSite_backtrace.push_back(std::vector<int>(cellId2CandidateSites[curCell].size(), -1));

            for (int j = 0; j < predCandidates.size(); j++)
            {
                for (int k = 0; k < curCandidates.size(); k++)
                {
                    float delay = timingOptimizer->getDelayByModel(
                        predCandidates[j]->getCLBSite()->X(), predCandidates[j]->getCLBSite()->Y(),
                        curCandidates[k]->getCLBSite()->X(), curCandidates[k]->getCLBSite()->Y());

                    if (shortestPath_LayerSite[i][k] > shortestPath_LayerSite[i - 1][j] + delay)
                    {
                        shortestPath_LayerSite[i][k] = shortestPath_LayerSite[i - 1][j] + delay;
                        shortestPath_LayerSite_backtrace[i][k] = j;

                        if (i == cellIdsInCriticalPath.size() - 1)
                        {
                            if (shortestPath_LayerSite[i][k] < bestChoiceDelay)
                            {
                                bestChoiceDelay = shortestPath_LayerSite[i][k];
                                bestEndChoice = k;
                            }
                        }
                    }
                }
            }
        }

        for (int i = shortestPath_LayerSite.size() - 1; i >= 0; i--)
        {
            assert(bestEndChoice >= 0);
            if (bestEndChoice > 0)
            {
                auto curCellId = cellIdsInCriticalPath[i];
                auto curPU = placementInfo->getPlacementUnitByCellId(curCellId);

                auto &curCandidates = cellId2CandidateSites[curCellId];

                auto targetPackingSite = cellId2CandidateSites[curCellId][bestEndChoice];

                if (curPU->getDSPNum() == 1)
                {
                    cellId2CandidateSites[curCellId][0]->clearNonCLBCell();
                    assert(bestEndChoice < cellId2CandidateSites[curCellId].size());
                    assert(!targetPackingSite->getNonCLBCell());
                    targetPackingSite->setNonCLBCell(designInfo->getCells()[curCellId]);
                    PUId2PackingCLBSite[curPU->getId()] = targetPackingSite;
                    cellId2PackingSite[curCellId] = targetPackingSite;
                    placementInfo->getPULegalSite()[curPU] =
                        std::vector<DeviceInfo::DeviceSite *>(1, targetPackingSite->getCLBSite());
                    // placementInfo->addPUIntoClockColumn(curPU, targetPackingSite->getCLBSite());
                }
                else
                {
                    if (!targetPackingSite->getDeterminedClusterInSite()->checkAddPU(curPU))
                    {
                        bestEndChoice = shortestPath_LayerSite_backtrace[i][bestEndChoice];
                        continue;
                    }

                    cellId2CandidateSites[curCellId][0]->getDeterminedClusterInSite()->removePUToConstructDetCluster(
                        curPU);

                    assert(bestEndChoice < cellId2CandidateSites[curCellId].size());
                    assert(targetPackingSite->getDeterminedClusterInSite()->addPU(curPU));
                    PUId2PackingCLBSite[curPU->getId()] = targetPackingSite;
                    placementInfo->addPUIntoClockColumn(curPU, targetPackingSite->getCLBSite());
                    auto cellSet = targetPackingSite->getDeterminedClusterInSite()->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = targetPackingSite;
                    }
                }

                replaceCnt++;
            }
            bestEndChoice = shortestPath_LayerSite_backtrace[i][bestEndChoice];
        }

        for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
        {
            auto cellId = cellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            PUsTouched.insert(curPU);
        }
    }

    print_status("ParallelCLBPacker: conducted timing-driven detailed placement (shortest path) and " +
                 std::to_string(replaceCnt) + " PlacementUnits are replaced.");
    outfileTcl.close();
    return replaceCnt;
}

int ParallelCLBPacker::timingDrivenDetailedPlacement_LUTFFPairReloacationAfterSlotMapping()
{
    AMF_PROFILE_FUNCTION("detailed_placement");
    std::vector<std::unique_ptr<std::vector<DeviceInfo::DeviceSite *>>> neighborOwners;
    auto ownNeighbors = [&](std::vector<DeviceInfo::DeviceSite *> *p) {
        neighborOwners.emplace_back(p); return p;
    };
    print_status("ParallelCLBPacker: re-place some LUT-FF pairs since they are not connected via internal nets.");
    // A retained static interface cell participates in STA but is not packed
    // inside the active RM. Use its verified immutable physical site as an
    // anchor; never invent a packing cluster or relax the fixed placement.
    int staticNeighborAnchorLookups = 0;
    auto packedOrStaticAnchor = [&](int anchorCellId) -> std::pair<float, float> {
        auto packed = cellId2PackingSite[anchorCellId];
        if (packed)
            return {packed->getCLBSite()->X(), packed->getCLBSite()->Y()};
        const auto &dfx = placementInfo->getIncrementalContext();
        auto cell = designInfo->getCells()[anchorCellId];
        auto fixed = dfx.fixed.find(cell->getName());
        auto pu = placementInfo->getPlacementUnitByCellId(anchorCellId);
        if(dfx.enabled&&cell->isInterfaceEndpoint()&&pu->isFixed()&&pu->isLocked()) {
            const auto &p=dfx.endpoints.at(cell->getName());
            if(std::fabs(pu->X()-p.x)<1e-5f&&std::fabs(pu->Y()-p.y)<1e-5f)return {p.x,p.y};
            throw std::runtime_error("Native interface endpoint moved: "+cell->getName());
        }
        if (dfx.enabled && fixed != dfx.fixed.end() && pu->isFixed() && pu->isLocked())
        {
            auto siteName = fixed->second.site;
            auto site = deviceInfo->getSite(siteName);
            if (site && std::fabs(pu->X() - site->X()) <= 1e-5f &&
                std::fabs(pu->Y() - site->Y()) <= 1e-5f)
            {
                ++staticNeighborAnchorLookups;
                return {site->X(), site->Y()};
            }
        }
        throw std::runtime_error("LUT-FF relocation has no packing site or verified DFX static anchor: " +
                                 cell->getName());
    };
    auto oriCellIdsInCriticalPaths = timingOptimizer->findCriticalPaths(0.9, false, 1000, 30, false);

    // auto &sortedTimingNodes = timingOptimizer->getSortedTimingNodes();
    // std::vector<bool> FFDirectlyDrivenButNotInOneSlot;
    // FFDirectlyDrivenButNotInOneSlot.clear();
    // FFDirectlyDrivenButNotInOneSlot.resize(designInfo->getCells().size(), 0);
    // for (auto node : sortedTimingNodes)
    // {
    //     int cellId = node->getDesignNode()->getCellId();
    //     bool shouldRelocateLUTFFPair = false;
    //     PackingCLBSite *srcSite = nullptr;
    //     auto LUTFFPair = dynamic_cast<PlacementInfo::PlacementMacro
    //     *>(placementInfo->getPlacementUnitByCellId(cellId)); DesignInfo::DesignCell *targetLUT = nullptr;
    //     DesignInfo::DesignCell *targetFF = nullptr;
    //     if (LUTFFPair)
    //     {
    //         if (LUTFFPair->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
    //         {
    //             auto curPackingSite = cellId2PackingSite[cellId];
    //             targetLUT = LUTFFPair->getCells()[0];
    //             targetFF = LUTFFPair->getCells()[1];
    //             auto LUTLoc = curPackingSite->getLUTSlot(targetLUT);
    //             auto FFLoc = curPackingSite->getFFSlot(targetFF);
    //             shouldRelocateLUTFFPair = !(LUTLoc[0] == FFLoc[0] && LUTLoc[1] == FFLoc[1] && LUTLoc[2] == FFLoc[2]);
    //             srcSite = curPackingSite;
    //         }
    //         FFDirectlyDrivenButNotInOneSlot[cellId] = shouldRelocateLUTFFPair;
    //     }
    // }

    // auto oriCellIdsInCriticalPaths = timingOptimizer->findCriticalPaths(1, FFDirectlyDrivenButNotInOneSlot);
    // oriCellIdsInCriticalPaths.resize(200);
    std::set<PlacementInfo::PlacementUnit *> PUsTouched;
    PUsTouched.clear();
    int replaceCnt = 0;
    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        neighborOwners.clear();
        std::map<int, std::vector<PackingCLBSite *>> cellId2CandidateSites;
        std::set<PackingCLBSite *> sitesCandidates;
        std::map<PackingCLBSite *, std::unique_ptr<PackingCLBSite::PackingCLBCluster>> site2TrialCluster;
        cellId2CandidateSites.clear();

        std::vector<int> cellIdsInCriticalPath;
        PlacementInfo::PlacementUnit *lastPU = nullptr;
        std::set<PlacementInfo::PlacementUnit *> PUsInCriticalPathSet;
        for (auto cellId : oriCellIdsInCriticalPath)
        {
            if (PUsInCriticalPathSet.find(placementInfo->getPlacementUnitByCellId(cellId)) ==
                PUsInCriticalPathSet.end())
            {
                PUsInCriticalPathSet.insert(placementInfo->getPlacementUnitByCellId(cellId));
                cellIdsInCriticalPath.push_back(cellId);
            }
        }

        bool shouldRelocateLUTFFPair = false;
        PackingCLBSite *srcSite = nullptr;
        auto LUTFFPair = dynamic_cast<PlacementInfo::PlacementMacro *>(
            placementInfo->getPlacementUnitByCellId(oriCellIdsInCriticalPath[0]));
        DesignInfo::DesignCell *targetLUT = nullptr;
        DesignInfo::DesignCell *targetFF = nullptr;
        if (LUTFFPair)
        {
            if (LUTFFPair->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
            {
                auto curPackingSite = cellId2PackingSite[oriCellIdsInCriticalPath[0]];
                targetLUT = LUTFFPair->getCells()[0];
                targetFF = LUTFFPair->getCells()[1];
                assert(curPackingSite->getDeterminedClusterInSite());
                if (curPackingSite->getDeterminedClusterInSite()->getPUs().find(LUTFFPair) ==
                    curPackingSite->getDeterminedClusterInSite()->getPUs().end())
                {
                    std::cout << "cluster:" << curPackingSite->getDeterminedClusterInSite() << "\n";
                    std::cout << "LUTFFPair:" << LUTFFPair << "\n";
                }
                assert(curPackingSite->getDeterminedClusterInSite()->getPUs().find(LUTFFPair) !=
                       curPackingSite->getDeterminedClusterInSite()->getPUs().end());
                // curPackingSite->finalMapToSlots();

                auto LUTLoc = curPackingSite->getLUTSlot(targetLUT);
                auto FFLoc = curPackingSite->getFFSlot(targetFF);
                shouldRelocateLUTFFPair = !(LUTLoc[0] == FFLoc[0] && LUTLoc[1] == FFLoc[1] && LUTLoc[2] == FFLoc[2]);
                srcSite = curPackingSite;
            }
        }

        if (!shouldRelocateLUTFFPair)
            continue;

        // std::cout << "handling " << LUTFFPair << "\n";
        int siteCandidateLimit = 20;

        float displacementThr = 1.6;
        int orderI = 0;
        auto cellId = cellIdsInCriticalPath[orderI];
        auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
        if (PUsTouched.find(curPU) != PUsTouched.end())
        {
            continue;
        }

        PUsTouched.insert(curPU);
        if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() && !curPU->checkHasBRAM() &&
            !(curPU->checkHasDSP() || curPU->checkHasURAM()))
        {
            float v1x = 0, v1y = 0, v2x = 0, v2y = 0;

            auto curSite = cellId2PackingSite[cellId];
            auto succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
            assert(curSite);
            int succSiteOrderId = orderI + 1;
            while (curSite == succSite && succSiteOrderId + 1 <= cellIdsInCriticalPath.size() - 1)
            {
                succSiteOrderId++;
                succSite = cellId2PackingSite[cellIdsInCriticalPath[succSiteOrderId]];
            }
            const auto succAnchor = packedOrStaticAnchor(cellIdsInCriticalPath[succSiteOrderId]);
            v1x = v2x = succAnchor.first - curSite->getCLBSite()->X();
            v1y = v2y = succAnchor.second - curSite->getCLBSite()->Y();

            // if (std::fabs(v1x) + std::fabs(v1y) + std::fabs(v2x) + std::fabs(v2y) < 0.1)
            //     continue;
            if (std::fabs(getAngle(v1x, v1y, v2x, v2y)) < M_PI / 2)
            {
                assert(PUId2PackingCLBSite[curPU->getId()]);
                auto candidateSitesToPlaceTheCell_cone = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, displacementThr, y2xRatio, false, v1x,
                    v1y, v2x, v2y, 20));


                for (auto curDeviceSite : *candidateSitesToPlaceTheCell_cone)
                {
                    if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                        continue;
                    PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                    if (cellId2CandidateSites[cellId].size() < siteCandidateLimit)
                    {
                        bool duplicate = false;
                        for (auto existCandidate : cellId2CandidateSites[cellId])
                        {
                            if (existCandidate == candidatePackingSite)
                                duplicate = true;
                        }
                        if (duplicate)
                            continue;
                        PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                        if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                        {
                            if (!candidatePackingSite->getDeterminedClusterInSite())
                            {
                                auto determinedClusterInSite =
                                    new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                                candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                            }
                            trialCluster = new PackingCLBSite::PackingCLBCluster(
                                candidatePackingSite->getDeterminedClusterInSite());
                            site2TrialCluster[candidatePackingSite].reset(trialCluster);
                        }
                        else
                        {
                            trialCluster = site2TrialCluster[candidatePackingSite].get();
                        }

                        if (trialCluster->checkAddPU(curPU))
                        {
                            assert(trialCluster->addPU(curPU));
                            cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                            sitesCandidates.insert(candidatePackingSite);
                        }
                    }

                    // }
                }

            }
        }

        // std::cout << curCell << " has following candidates: \n";
        if (!curPU->isLocked() && !curPU->checkHasCARRY() && !curPU->checkHasLUTRAM() && !curPU->checkHasBRAM() &&
            !(curPU->checkHasDSP() || curPU->checkHasURAM()))
        {
            std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell = ownNeighbors(findNeiborSitesFromBinGrid(
                DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, displacementThr, y2xRatio, false));
            if (cellId2CandidateSites[cellId].size() < siteCandidateLimit + 1)
            {
                for (auto curDeviceSite : *candidateSitesToPlaceTheCell)
                {
                    if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                        continue;
                    PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                    bool duplicate = false;
                    for (auto existCandidate : cellId2CandidateSites[cellId])
                    {
                        if (existCandidate == candidatePackingSite)
                            duplicate = true;
                    }
                    if (duplicate)
                        continue;
                    PackingCLBSite::PackingCLBCluster *trialCluster = nullptr;
                    if (site2TrialCluster.find(candidatePackingSite) == site2TrialCluster.end())
                    {
                        if (!candidatePackingSite->getDeterminedClusterInSite())
                        {
                            auto determinedClusterInSite = new PackingCLBSite::PackingCLBCluster(candidatePackingSite);
                            candidatePackingSite->setDeterminedClusterInSite(determinedClusterInSite);
                        }
                        trialCluster =
                            new PackingCLBSite::PackingCLBCluster(candidatePackingSite->getDeterminedClusterInSite());
                        site2TrialCluster[candidatePackingSite].reset(trialCluster);
                    }
                    else
                    {
                        trialCluster = site2TrialCluster[candidatePackingSite].get();
                    }

                    if (trialCluster->checkAddPU(curPU))
                    {
                        assert(trialCluster->addPU(curPU));
                        cellId2CandidateSites[cellId].push_back(candidatePackingSite);
                        sitesCandidates.insert(candidatePackingSite);
                    }
                }
            }

        }

        // calculate the shortest paths
        int bestEndChoice = -1;
        float bestChoiceDelay = 100000;

        int predCell = cellIdsInCriticalPath[1];
        auto &curCandidates = cellId2CandidateSites[cellId];

        for (int k = 0; k < curCandidates.size(); k++)
        {
            // std::cout << "considering " << curCandidates[k]->getCLBSite()->getName() << "\n";
            if (curCandidates[k] == srcSite)
                continue;
            const auto predAnchor = packedOrStaticAnchor(predCell);
            float delay = timingOptimizer->getDelayByModel(
                predAnchor.first, predAnchor.second, curCandidates[k]->getCLBSite()->X(),
                curCandidates[k]->getCLBSite()->Y());

            if (bestChoiceDelay > delay)
            {
                // std::cout << "      better delay\n";
                if (curCandidates[k]->getSlotMapping().canDirectConnectInSlot(targetLUT, targetFF))
                {
                    bestChoiceDelay = delay;
                    bestEndChoice = k;
                    //  std::cout << "      has direct connect\n";
                }
            }
        }

        if (bestEndChoice > 0)
        {
            srcSite->getDeterminedClusterInSite()->removePUToConstructDetCluster(LUTFFPair);
            srcSite->getSlotMappingRef().removeLUTFFPair(targetLUT, targetFF);
            assert(curCandidates[bestEndChoice]->getDeterminedClusterInSite()->addPU(LUTFFPair));
            curCandidates[bestEndChoice]->getSlotMappingRef().addLUTFFPair(targetLUT, targetFF);
            PUId2PackingCLBSite[LUTFFPair->getId()] = curCandidates[bestEndChoice];
            placementInfo->addPUIntoClockColumn(LUTFFPair, curCandidates[bestEndChoice]->getCLBSite());

            auto cellSet = srcSite->getDeterminedClusterInSite()->getCellSet();
            for (auto cell : cellSet)
            {
                cellId2PackingSite[cell->getCellId()] = srcSite;
            }
            cellSet = curCandidates[bestEndChoice]->getDeterminedClusterInSite()->getCellSet();
            for (auto cell : cellSet)
            {
                cellId2PackingSite[cell->getCellId()] = curCandidates[bestEndChoice];
            }
            replaceCnt++;
        }
    }

    print_status("ParallelCLBPacker: conducted timing-driven detailed placement (LUT-FF pairs) and " +
                 std::to_string(replaceCnt) + " PlacementUnits are replaced.");
    if (placementInfo->getIncrementalContext().enabled)
        print_info("DFX_DETAIL_STATIC_NEIGHBOR_ANCHOR_LOOKUPS=" + std::to_string(staticNeighborAnchorLookups));

    return replaceCnt;
}

int ParallelCLBPacker::timingDrivenDetailedPlacement_swap(float disOffset, int iterId, bool preserverCriticalPath,
                                                          float pathCriticalRatio)
{
    AMF_PROFILE_FUNCTION("detailed_placement");
    std::vector<std::unique_ptr<std::vector<DeviceInfo::DeviceSite *>>> neighborOwners;
    auto ownNeighbors = [&](std::vector<DeviceInfo::DeviceSite *> *p) {
        neighborOwners.emplace_back(p); return p;
    };
    // int pathNumThr = preserverCriticalPath ? (std::min(40 * placementInfo->getClusterNum(), 1000)) : (2 + iterId *
    // 20); if (pathNumThr * placementInfo->getMediumPathThresholdLevel() > 1000)
    // {
    //     pathNumThr = 1000 / placementInfo->getMediumPathThresholdLevel();
    // }
    // print_status("ParallelCLBPacker: conducting timing-driven detailed placement based on swaping.");
    // auto oriCellIdsInCriticalPaths =
    //     timingOptimizer->findCriticalPaths(0.8, true, pathNumThr, preserverCriticalPath ? 20 : 30);

    int pathNumThr = preserverCriticalPath ? (std::min(80 * placementInfo->getClusterNum(), 1000)) : (2 + iterId * 20);
    // if (iterId != 100 && pathNumThr * placementInfo->getMediumPathThresholdLevel() > 1000)
    // {
    //     pathNumThr = 1000 / placementInfo->getMediumPathThresholdLevel();
    // }
    print_status("ParallelCLBPacker: conducting timing-driven detailed placement based on swaping.");
    auto oriCellIdsInCriticalPaths =
        timingOptimizer->findCriticalPaths(pathCriticalRatio, true, pathNumThr, preserverCriticalPath ? 50 : 30);
    fineTuneCandidateNum = oriCellIdsInCriticalPaths.size();
    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo();
    std::set<PlacementInfo::PlacementUnit *> PUsTouched;
    std::set<PlacementInfo::PlacementUnit *> PUsDontTouch;
    PUsTouched.clear();
    PUsDontTouch.clear();
    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        neighborOwners.clear();
        for (int orderI = oriCellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
        {
            auto cellId = oriCellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            PUsDontTouch.insert(curPU);
        }
    }

    int replaceCnt = 0;

    for (auto oriCellIdsInCriticalPath : oriCellIdsInCriticalPaths)
    {
        neighborOwners.clear();
        std::set<PackingCLBSite *> changedPackingSites;
        std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> movedPUs;
        movedPUs.clear();
        changedPackingSites.clear();
        std::map<int, std::vector<PackingCLBSite *>> cellId2CandidateSites;
        std::set<PackingCLBSite *> sitesCandidates;
        std::map<PackingCLBSite *, std::unique_ptr<PackingCLBSite::PackingCLBCluster>> site2TrialCluster;
        cellId2CandidateSites.clear();

        std::vector<int> cellIdsInCriticalPath;
        std::vector<int> headCellIdsInCriticalPath;
        PlacementInfo::PlacementUnit *lastPU = nullptr;
        std::set<PlacementInfo::PlacementUnit *> PUsInCriticalPathSet;
        for (auto cellId : oriCellIdsInCriticalPath)
        {
            if (PUsInCriticalPathSet.find(placementInfo->getPlacementUnitByCellId(cellId)) ==
                PUsInCriticalPathSet.end())
            {
                PUsInCriticalPathSet.insert(placementInfo->getPlacementUnitByCellId(cellId));
                cellIdsInCriticalPath.push_back(cellId);
                headCellIdsInCriticalPath.push_back(cellId);
            }
            else
            {
                headCellIdsInCriticalPath[headCellIdsInCriticalPath.size() - 1] = cellId;
            }
        }

        bool CellUnmapped = false;
        for (auto cellId : cellIdsInCriticalPath)
        {
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            auto curPackingSite = cellId2PackingSite[cellId];
            if (!curPackingSite)
            {
                CellUnmapped = true;
                break;
            }
            assert(curPackingSite);
            if (PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite() &&
                !curPackingSite->checkIsCarrySite() && !curPackingSite->checkIsNonCLBSite() &&
                !curPackingSite->checkIsLUTRAMSite())
            {
                if (!PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite()->contains(curPU))
                {
                    std::cout << PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite() << "\n";
                    std::cout << "Does not contain " << curPU << "\n";
                    assert(false && "target packing site should contain a specific PU");
                }
            }
        }

        if (CellUnmapped)
            continue;

        // std::cout << "processing endpoint [" << designInfo->getCells()[cellIdsInCriticalPath[0]] << "]  with "
        //           << cellIdsInCriticalPath.size() << " nodes in path.\n";

        // find candidate sites for each possible PU
        for (int orderI = 0; orderI <= cellIdsInCriticalPath.size() - 1; orderI++)
        {
            if (orderI < lowIndex || orderI > highIndex)
                continue;
            auto cellId = cellIdsInCriticalPath[orderI];
            auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
            auto curCell = designInfo->getCells()[cellId];

            // std::cout << "------ " << curPU << "  \n"
            //           << " cell: " << curCell << "\n";
            auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU);
            if (curMacro)
            {
                if (curMacro->getBRAMNum() != 1 && curMacro->getDSPNum() != 1 &&
                    curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)
                    continue;
            }

            if (curPU->checkHasCARRY() || curPU->checkHasLUTRAM() || PUsTouched.find(curPU) != PUsTouched.end() ||
                curPU->isLocked())
            {
                continue;
            }

            if (cellId2PackingSite[curCell->getCellId()]->getDeterminedClusterInSite() !=
                PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite())
            {
                if (PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite())
                    std::cout << " PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite():\n"
                              << PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite() << "\n";
                if (cellId2PackingSite[curCell->getCellId()]->getDeterminedClusterInSite())
                    std::cout << " cellId2PackingSite[curCell->getCellId()]->getDeterminedClusterInSite():\n"
                              << cellId2PackingSite[curCell->getCellId()]->getDeterminedClusterInSite() << "\n";
                std::cout << "Does not contain " << curPU << "\n";
                assert(false && "cellId2PackingSite and PUId2PackingCLBSite should be coupled");
            }

            if (PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite())
            {
                if (!PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite()->contains(curPU))
                {
                    std::cout << PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite() << "\n";
                    std::cout << "Does not contain " << curPU << "\n";
                    assert(false && "target packing site should contain a specific PU");
                }
            }

            if ((curCell->isFF() || (curMacro && curMacro->getMacroType() ==
                                                     PlacementInfo::PlacementMacro::PlacementMacroType_LUTFFPair)) &&
                (orderI == 0 || orderI == cellIdsInCriticalPath.size() - 1))
            {
                PackingCLBSite *nextSite = nullptr;
                if (orderI == 0)
                {
                    nextSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                }
                else if (orderI == cellIdsInCriticalPath.size() - 1)
                {
                    nextSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                }

                auto curSite = cellId2PackingSite[cellId];
                assert(nextSite && curSite);

                float oriDelay =
                    timingOptimizer->getDelayByModel(curSite->getCLBSite()->X(), curSite->getCLBSite()->Y(),
                                                     nextSite->getCLBSite()->X(), nextSite->getCLBSite()->Y());

                if (oriDelay < 0.1)
                    continue;

                // std::cout << curCell << " oriDelay=" << oriDelay << " has following candidates: \n";

                float neighborDis = std::fabs(curSite->getCLBSite()->X() - nextSite->getCLBSite()->X()) +
                                    y2xRatio * std::fabs(curSite->getCLBSite()->Y() - nextSite->getCLBSite()->Y());

                float disRange = 2.1 - iterId * 0.03;
                if (neighborDis > 3)
                {
                    disRange = 2;
                    if (neighborDis > 5)
                        disRange = 3;
                }

                if (disRange < 1.6)
                    disRange = 1.6;
                assert(PUId2PackingCLBSite[curPU->getId()]);
                if ((timingOptimizer->crossIOBank(nextSite->getCLBSite()->X(),
                                                  PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X())) &&
                    disRange < 2)
                    disRange = 2;

                auto candidateSitesToPlaceTheCell_cone = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange + disOffset, y2xRatio, false));

                PackingCLBSite *bestCandidatePackingSite = nullptr;
                PlacementInfo::PlacementUnit *bestSwapCandidatePU = nullptr;
                float bestOverheadSlack = -1000000;

                for (auto curDeviceSite : *candidateSitesToPlaceTheCell_cone)
                {
                    if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                        continue;
                    PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                    if (curSite == candidatePackingSite)
                        continue;

                    if (!candidatePackingSite->getDeterminedClusterInSite())
                    {
                        continue;
                    }

                    float newDelay = timingOptimizer->getDelayByModel(
                        candidatePackingSite->getCLBSite()->X(), candidatePackingSite->getCLBSite()->Y(),
                        nextSite->getCLBSite()->X(), nextSite->getCLBSite()->Y());

                    // std::cout << "      " << candidatePackingSite->getCLBSite()->getName() << " newDelay=" <<
                    // newDelay
                    //           << "\n";
                    if (newDelay > oriDelay)
                        continue;

                    if (candidatePackingSite->getDeterminedClusterInSite()->checkAddPU(curPU))
                    {
                        if (1000 - newDelay > bestOverheadSlack)
                        {
                            bestOverheadSlack = 1000 - newDelay;
                            bestCandidatePackingSite = candidatePackingSite;
                            bestSwapCandidatePU = nullptr;
                        }
                    }
                }

                if (bestOverheadSlack > -10000)
                {

                    std::set<PackingCLBSite *> local_changedPackingSites;
                    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> local_movedPUs;

                    changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                    changedPackingSites.insert(bestCandidatePackingSite);
                    local_changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                    local_changedPackingSites.insert(bestCandidatePackingSite);

                    auto trialTargetCluster = bestCandidatePackingSite->getDeterminedClusterInSite();
                    assert(trialTargetCluster->addPU(curPU));
                    auto trialCurrentCluster = PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite();
                    trialCurrentCluster->removePUToConstructDetCluster(curPU);

                    auto cellSet = bestCandidatePackingSite->getDeterminedClusterInSite()->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = bestCandidatePackingSite;
                    }
                    cellSet = trialCurrentCluster->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = PUId2PackingCLBSite[curPU->getId()];
                    }

                    auto tmpSwapPackingSite = bestCandidatePackingSite;
                    PUId2PackingCLBSite[curPU->getId()] = bestCandidatePackingSite;
                    curPU->setAnchorLocationAndForgetTheOriginalOne(bestCandidatePackingSite->getCLBSite()->X(),
                                                                    bestCandidatePackingSite->getCLBSite()->Y());

                    movedPUs.insert(curPU);
                    local_movedPUs.insert(curPU);

                    if (preserverCriticalPath && useLocalFineTune &&
                        !tryAcceptLocalTimingDetailedPlacement(local_movedPUs, local_changedPackingSites))
                    {
                        for (auto aPU : local_movedPUs)
                        {
                            movedPUs.erase(aPU);
                        }
                    }
                }

            }
            else if ((curCell->isLUT() || curCell->isMux()) && orderI != 0 &&
                     orderI != cellIdsInCriticalPath.size() - 1)
            {
                auto predSite = cellId2PackingSite[headCellIdsInCriticalPath[orderI - 1]];
                int predSiteOrderId = orderI - 1;
                auto curSite = cellId2PackingSite[cellId];
                auto succSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                int succSiteOrderId = orderI + 1;
                assert(predSite && curSite && succSite);

                float oriDelay =
                    timingOptimizer->getDelayByModel(curSite->getCLBSite()->X(), curSite->getCLBSite()->Y(),
                                                     predSite->getCLBSite()->X(), predSite->getCLBSite()->Y()) +
                    timingOptimizer->getDelayByModel(curSite->getCLBSite()->X(), curSite->getCLBSite()->Y(),
                                                     succSite->getCLBSite()->X(), succSite->getCLBSite()->Y());

                if (oriDelay < 0.1)
                    continue;

                int clockRegionX0, clockRegionY0;
                deviceInfo->getClockRegionByLocation(PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                                                     PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(),
                                                     clockRegionX0, clockRegionY0);
                int clockRegionX1, clockRegionY1;
                deviceInfo->getClockRegionByLocation(predSite->getCLBSite()->X(), predSite->getCLBSite()->Y(),
                                                     clockRegionX1, clockRegionY1);
                int clockRegionX2, clockRegionY2;
                deviceInfo->getClockRegionByLocation(succSite->getCLBSite()->X(), succSite->getCLBSite()->Y(),
                                                     clockRegionX2, clockRegionY2);

                float neighborDis =
                    std::min(std::fabs(curSite->getCLBSite()->X() - succSite->getCLBSite()->X()) +
                                 y2xRatio * std::fabs(curSite->getCLBSite()->Y() - succSite->getCLBSite()->Y()),
                             std::fabs(curSite->getCLBSite()->X() - predSite->getCLBSite()->X()) +
                                 y2xRatio * std::fabs(curSite->getCLBSite()->Y() - predSite->getCLBSite()->Y()));

                float disRange = 2.2 - iterId * 0.03;
                if ((timingOptimizer->crossIOBank(predSite->getCLBSite()->X(),
                                                  PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X()) &&
                     timingOptimizer->crossIOBank(succSite->getCLBSite()->X(),
                                                  PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X())) &&
                    disRange < std::min(neighborDis + 1, (float)4.0))
                    disRange = std::min(neighborDis + 1, (float)4.0);
                if (disRange < 1.6)
                    disRange = 1.6;

                if (!noSectorGuided && orderI > 0 && orderI < cellIdsInCriticalPath.size() - 1)
                {
                    float v1x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                    float v1y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    float v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                    float v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    if (std::fabs(getAngle(v1x, v1y, v2x, v2y)) < M_PI / 2)
                    {
                        float disTmpRange = ((std::fabs(v1x) + y2xRatio * std::fabs(v1y)) +
                                             (std::fabs(v2x) + y2xRatio * std::fabs(v2y))) /
                                            2;
                        if (disTmpRange > disRange)
                            disRange = disTmpRange;
                        if (disRange > 4)
                            disRange = 4;
                    }
                }
                if (useBigWindow)
                {
                    float v1x = predSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                    float v1y = predSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();
                    float v2x = succSite->getCLBSite()->X() - curSite->getCLBSite()->X();
                    float v2y = succSite->getCLBSite()->Y() - curSite->getCLBSite()->Y();

                    float disTmpRange =
                        ((std::fabs(v1x) + y2xRatio * std::fabs(v1y)) + (std::fabs(v2x) + y2xRatio * std::fabs(v2y))) /
                        2;
                    if (disTmpRange > disRange)
                        disRange = disTmpRange;
                    if (disRange > 4)
                        disRange = 4;
                }

                assert(PUId2PackingCLBSite[curPU->getId()]);
                auto candidateSitesToPlaceTheCell_cone = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_LUT4, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange + disOffset, y2xRatio, false));

                PackingCLBSite *bestCandidatePackingSite = nullptr;
                PlacementInfo::PlacementUnit *bestSwapCandidatePU = nullptr;
                float bestOverheadSlack = -1000000;
                float oriOverhead = timingOptimizer->getWorstSlackOfCell(curCell);
                // std::cout << curCell << " oriOverhead=" << oriOverhead << " has following candidates: \n";
                for (auto curDeviceSite : *candidateSitesToPlaceTheCell_cone)
                {
                    if (deviceSite2PackingSite.find(curDeviceSite) == deviceSite2PackingSite.end())
                        continue;
                    PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                    if (curSite == candidatePackingSite)
                        continue;

                    if (!candidatePackingSite->getDeterminedClusterInSite())
                    {
                        continue;
                    }

                    float newDelay =
                        timingOptimizer->getDelayByModel(candidatePackingSite->getCLBSite()->X(),
                                                         candidatePackingSite->getCLBSite()->Y(),
                                                         predSite->getCLBSite()->X(), predSite->getCLBSite()->Y()) +
                        timingOptimizer->getDelayByModel(candidatePackingSite->getCLBSite()->X(),
                                                         candidatePackingSite->getCLBSite()->Y(),
                                                         succSite->getCLBSite()->X(), succSite->getCLBSite()->Y());

                    // std::cout << "      " << candidatePackingSite->getCLBSite()->getName() << " newDelay=" <<
                    // newDelay
                    //           << "\n";
                    if (newDelay > oriDelay)
                        continue;

                    if (candidatePackingSite->getDeterminedClusterInSite()->checkAddPU(curPU))
                    {
                        if (1000 - newDelay > bestOverheadSlack)
                        {
                            bestOverheadSlack = 1000 - newDelay;
                            bestCandidatePackingSite = candidatePackingSite;
                            bestSwapCandidatePU = nullptr;
                            if (newDelay < oriDelay - 0.2)
                            {
                                break;
                            }
                        }
                    }
                    else
                    {
                        bool findExchange = false;
                        // find lowest slack LUT
                        for (auto tmpPU : candidatePackingSite->getDeterminedClusterInSite()->getPUs())
                        {
                            if (tmpPU->isLocked() || PUsDontTouch.find(tmpPU) != PUsDontTouch.end())
                                continue;
                            if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                            {
                                auto targetCell = unpackedCell->getCell();

                                // try to swap LUTs
                                if (targetCell->isLUT())
                                {
                                    if (timingNodes[targetCell->getCellId()]->getOutEdges().size() > 200)
                                    {
                                        continue; // don touch high fanout node
                                    }
                                    auto trialTargetCluster = new PackingCLBSite::PackingCLBCluster(
                                        candidatePackingSite->getDeterminedClusterInSite());
                                    trialTargetCluster->removePUToConstructDetCluster(tmpPU);
                                    if (trialTargetCluster->addPU(curPU))
                                    {
                                        auto trialCurrentCluster = new PackingCLBSite::PackingCLBCluster(
                                            PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite());
                                        trialCurrentCluster->removePUToConstructDetCluster(curPU);
                                        if (trialCurrentCluster->addPU(tmpPU))
                                        {
                                            float overhead = timingOptimizer->getWorstSlackOfCell(targetCell);
                                            if (overhead > 3 && iterId == 100)
                                            {
                                                if (1000 - newDelay > bestOverheadSlack)
                                                {
                                                    bestOverheadSlack = 1000 - newDelay;
                                                    bestCandidatePackingSite = candidatePackingSite;
                                                    bestSwapCandidatePU = tmpPU;
                                                }
                                            }
                                            else if (overhead > oriOverhead + 0.3)
                                            {
                                                if (overhead > bestOverheadSlack)
                                                {
                                                    bestOverheadSlack = overhead;
                                                    bestCandidatePackingSite = candidatePackingSite;
                                                    bestSwapCandidatePU = tmpPU;
                                                }
                                            }
                                        }
                                        delete trialCurrentCluster;
                                    }
                                    delete trialTargetCluster;
                                }
                            }
                        }
                        if (bestSwapCandidatePU && (newDelay < oriDelay - 0.2))
                        {
                            break;
                        }
                    }
                }

                if (bestOverheadSlack > -10000)
                {

                    std::set<PackingCLBSite *> local_changedPackingSites;
                    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> local_movedPUs;

                    changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                    changedPackingSites.insert(bestCandidatePackingSite);
                    local_changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                    local_changedPackingSites.insert(bestCandidatePackingSite);

                    auto trialTargetCluster = bestCandidatePackingSite->getDeterminedClusterInSite();
                    if (bestSwapCandidatePU)
                        trialTargetCluster->removePUToConstructDetCluster(bestSwapCandidatePU);
                    assert(trialTargetCluster->addPU(curPU));
                    auto trialCurrentCluster = PUId2PackingCLBSite[curPU->getId()]->getDeterminedClusterInSite();
                    trialCurrentCluster->removePUToConstructDetCluster(curPU);
                    if (bestSwapCandidatePU)
                        assert(trialCurrentCluster->addPU(bestSwapCandidatePU));
                    auto unpackedCell_bestSwapCandidatePU =
                        dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(bestSwapCandidatePU);

                    auto cellSet = bestCandidatePackingSite->getDeterminedClusterInSite()->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = bestCandidatePackingSite;
                    }
                    cellSet = trialCurrentCluster->getCellSet();
                    for (auto cell : cellSet)
                    {
                        cellId2PackingSite[cell->getCellId()] = PUId2PackingCLBSite[curPU->getId()];
                    }

                    if (bestSwapCandidatePU)
                    {
                        cellId2PackingSite[unpackedCell_bestSwapCandidatePU->getCell()->getCellId()] =
                            PUId2PackingCLBSite[curPU->getId()];
                    }
                    if (bestSwapCandidatePU)
                    {
                        bestSwapCandidatePU->setAnchorLocationAndForgetTheOriginalOne(
                            PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                            PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y());
                        PUId2PackingCLBSite[bestSwapCandidatePU->getId()] = PUId2PackingCLBSite[curPU->getId()];
                    }
                    PUId2PackingCLBSite[curPU->getId()] = bestCandidatePackingSite;
                    curPU->setAnchorLocationAndForgetTheOriginalOne(bestCandidatePackingSite->getCLBSite()->X(),
                                                                    bestCandidatePackingSite->getCLBSite()->Y());

                    if (bestSwapCandidatePU &&
                        PUsInCriticalPathSet.find(bestSwapCandidatePU) == PUsInCriticalPathSet.end())
                        PUsTouched.insert(bestSwapCandidatePU);

                    movedPUs.insert(curPU);
                    local_movedPUs.insert(curPU);
                    if (bestSwapCandidatePU)
                    {
                        movedPUs.insert(bestSwapCandidatePU);
                        local_movedPUs.insert(bestSwapCandidatePU);
                    }

                    if (preserverCriticalPath && useLocalFineTune &&
                        !tryAcceptLocalTimingDetailedPlacement(local_movedPUs, local_changedPackingSites))
                    {
                        for (auto aPU : local_movedPUs)
                        {
                            movedPUs.erase(aPU);
                        }
                    }
                }

            }
            else if (!curPU->isLocked() && !curPU->checkHasCARRY() && curPU->getDSPNum() == 1 &&
                     (orderI == 0 || orderI == cellIdsInCriticalPath.size() - 1))
            {
                float disRange = 1.5;

                PackingCLBSite *nextSite = nullptr;
                if (orderI == 0)
                {
                    nextSite = cellId2PackingSite[cellIdsInCriticalPath[orderI + 1]];
                }
                else if (orderI == cellIdsInCriticalPath.size() - 1)
                {
                    nextSite = cellId2PackingSite[cellIdsInCriticalPath[orderI - 1]];
                }
                else
                {
                    assert(false);
                }
                auto curSite = cellId2PackingSite[cellId];
                assert(nextSite && curSite);

                float oriDelay =
                    timingOptimizer->getDelayByModel(curSite->getCLBSite()->X(), curSite->getCLBSite()->Y(),
                                                     nextSite->getCLBSite()->X(), nextSite->getCLBSite()->Y());

                std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceTheCell = ownNeighbors(findNeiborSitesFromBinGrid(
                    DesignInfo::CellType_DSP48E2, PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X(),
                    PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y(), 0, disRange, y2xRatio, false));
                // std::cout << curCell << " has " << candidateSitesToPlaceTheCell->size()
                //           << " neighbors and candidates are:\n";
                for (auto curDeviceSite : *candidateSitesToPlaceTheCell)
                {
                    assert(deviceSite2PackingSite.find(curDeviceSite) != deviceSite2PackingSite.end());
                    PackingCLBSite *candidatePackingSite = deviceSite2PackingSite[curDeviceSite];
                    bool duplicate = false;
                    for (auto existCandidate : cellId2CandidateSites[cellId])
                    {
                        if (existCandidate == candidatePackingSite)
                            duplicate = true;
                    }
                    if (duplicate)
                        continue;

                    float newDelay = timingOptimizer->getDelayByModel(
                        candidatePackingSite->getCLBSite()->X(), candidatePackingSite->getCLBSite()->Y(),
                        nextSite->getCLBSite()->X(), nextSite->getCLBSite()->Y());

                    if (newDelay > oriDelay)
                        continue;

                    if (candidatePackingSite->getNonCLBCell()) // swap
                    {
                        auto curTargeDSPCell = candidatePackingSite->getNonCLBCell();
                        int curTargeDSPCellId = curTargeDSPCell->getCellId();
                        auto curTargetPU = placementInfo->getPlacementUnitByCell(curTargeDSPCell);
                        if (PUsDontTouch.find(curTargetPU) != PUsDontTouch.end() || curTargetPU->getDSPNum() != 1)
                            continue;
                        if (curPU->getDSPNum() == 1)
                        {

                            std::set<PackingCLBSite *> local_changedPackingSites;
                            std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> local_movedPUs;

                            changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                            changedPackingSites.insert(candidatePackingSite);
                            local_changedPackingSites.insert(PUId2PackingCLBSite[curPU->getId()]);
                            local_changedPackingSites.insert(candidatePackingSite);

                            curSite->setNonCLBCell(curTargeDSPCell);
                            candidatePackingSite->setNonCLBCell(curCell);

                            PUId2PackingCLBSite[curPU->getId()] = candidatePackingSite;
                            cellId2PackingSite[curCell->getCellId()] = candidatePackingSite;
                            placementInfo->getPULegalSite()[curPU] =
                                std::vector<DeviceInfo::DeviceSite *>(1, candidatePackingSite->getCLBSite());
                            curPU->setAnchorLocationAndForgetTheOriginalOne(candidatePackingSite->getCLBSite()->X(),
                                                                            candidatePackingSite->getCLBSite()->Y());

                            PUId2PackingCLBSite[curTargetPU->getId()] = curSite;
                            cellId2PackingSite[curTargeDSPCell->getCellId()] = curSite;
                            placementInfo->getPULegalSite()[curTargetPU] =
                                std::vector<DeviceInfo::DeviceSite *>(1, curSite->getCLBSite());
                            // placementInfo->addPUIntoClockColumn(curPU, targetPackingSite->getCLBSite());
                            curTargetPU->setAnchorLocationAndForgetTheOriginalOne(curSite->getCLBSite()->X(),
                                                                                  curSite->getCLBSite()->Y());

                            movedPUs.insert(curPU);
                            movedPUs.insert(curTargetPU);

                            local_movedPUs.insert(curPU);
                            local_movedPUs.insert(curTargetPU);

                            if (preserverCriticalPath && useLocalFineTune &&
                                !tryAcceptLocalTimingDetailedPlacement(local_movedPUs, local_changedPackingSites))
                            {
                                for (auto aPU : local_movedPUs)
                                {
                                    movedPUs.erase(aPU);
                                }
                            }
                        }
                    }

                    break;
                }

            }
        }

        for (int i = 0; i < cellIdsInCriticalPath.size() - 1; i++)
        {
            if (i < lowIndex || i > highIndex)
                continue;
            auto curCellId = cellIdsInCriticalPath[i];
            auto curPU = placementInfo->getPlacementUnitByCellId(curCellId);
            auto nextCellId = cellIdsInCriticalPath[i + 1];
            auto nextPU = placementInfo->getPlacementUnitByCellId(nextCellId);
            if (curPU->checkHasCARRY() || curPU->checkHasLUTRAM() || curPU->checkHasBRAM() || (curPU->checkHasDSP() || curPU->checkHasURAM()) ||
                nextPU->checkHasCARRY() || nextPU->checkHasLUTRAM() || nextPU->checkHasBRAM() ||
                (nextPU->checkHasDSP() || nextPU->checkHasURAM()) || PUsTouched.find(curPU) != PUsTouched.end() ||
                PUsTouched.find(nextPU) != PUsTouched.end() || curPU == nextPU || curPU->isLocked() ||
                nextPU->isLocked())
            {
                continue;
            }
            if (PUId2PackingCLBSite[curPU->getId()] == PUId2PackingCLBSite[nextPU->getId()])
                continue;

            float oriDis = 0;
            float newDis = 0;
            auto curPackingSite = PUId2PackingCLBSite[curPU->getId()];
            auto nextPackingSite = PUId2PackingCLBSite[nextPU->getId()];
            float curX = PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->X();
            float curY = PUId2PackingCLBSite[curPU->getId()]->getCLBSite()->Y();
            float nextX = PUId2PackingCLBSite[nextPU->getId()]->getCLBSite()->X();
            float nextY = PUId2PackingCLBSite[nextPU->getId()]->getCLBSite()->Y();

            if (timingOptimizer->getDelayByModel(nextX, nextY, curX, curY) > 0.5)
                continue;

            if (i > 0)
            {
                auto prevCellId = headCellIdsInCriticalPath[i - 1];
                assert(cellId2PackingSite[prevCellId]);
                float headX = cellId2PackingSite[prevCellId]->getCLBSite()->X();
                float headY = cellId2PackingSite[prevCellId]->getCLBSite()->Y();
                oriDis += timingOptimizer->getDelayByModel(headX, headY, curX, curY);
                newDis += timingOptimizer->getDelayByModel(headX, headY, nextX, nextY);
            }

            if (i < cellIdsInCriticalPath.size() - 1)
            {
                auto succCellId = cellIdsInCriticalPath[i + 1];
                assert(cellId2PackingSite[succCellId]);
                float tailX = cellId2PackingSite[succCellId]->getCLBSite()->X();
                float tailY = cellId2PackingSite[succCellId]->getCLBSite()->Y();
                newDis += timingOptimizer->getDelayByModel(tailX, tailY, curX, curY);
                oriDis += timingOptimizer->getDelayByModel(tailX, tailY, nextX, nextY);
            }

            if (newDis < oriDis)
            {
                auto trialClusterWithCurPU =
                    new PackingCLBSite::PackingCLBCluster(curPackingSite->getDeterminedClusterInSite());
                auto trialClusterWithNextPU =
                    new PackingCLBSite::PackingCLBCluster(nextPackingSite->getDeterminedClusterInSite());
                assert(trialClusterWithCurPU->contains(curPU));
                trialClusterWithCurPU->removePUToConstructDetCluster(curPU);

                assert(trialClusterWithNextPU->contains(nextPU));
                trialClusterWithNextPU->removePUToConstructDetCluster(nextPU);
                if (trialClusterWithCurPU->addPU(nextPU))
                {
                    if (trialClusterWithNextPU->addPU(curPU))
                    {
                        std::set<PackingCLBSite *> local_changedPackingSites;
                        std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> local_movedPUs;

                        changedPackingSites.insert(curPackingSite);
                        changedPackingSites.insert(nextPackingSite);
                        local_changedPackingSites.insert(curPackingSite);
                        local_changedPackingSites.insert(nextPackingSite);

                        curPackingSite->getDeterminedClusterInSite()->removePUToConstructDetCluster(curPU);
                        assert(curPackingSite->getDeterminedClusterInSite()->addPU(nextPU));
                        nextPackingSite->getDeterminedClusterInSite()->removePUToConstructDetCluster(nextPU);
                        assert(nextPackingSite->getDeterminedClusterInSite()->addPU(curPU));

                        PUId2PackingCLBSite[curPU->getId()] = nextPackingSite;
                        curPU->setAnchorLocationAndForgetTheOriginalOne(nextPackingSite->getCLBSite()->X(),
                                                                        nextPackingSite->getCLBSite()->Y());
                        placementInfo->addPUIntoClockColumn(curPU, nextPackingSite->getCLBSite());
                        auto cellSet = nextPackingSite->getDeterminedClusterInSite()->getCellSet();
                        for (auto cell : cellSet)
                        {
                            cellId2PackingSite[cell->getCellId()] = nextPackingSite;
                        }

                        PUId2PackingCLBSite[nextPU->getId()] = curPackingSite;
                        nextPU->setAnchorLocationAndForgetTheOriginalOne(curPackingSite->getCLBSite()->X(),
                                                                         curPackingSite->getCLBSite()->Y());
                        placementInfo->addPUIntoClockColumn(nextPU, curPackingSite->getCLBSite());
                        cellSet = curPackingSite->getDeterminedClusterInSite()->getCellSet();
                        for (auto cell : cellSet)
                        {
                            cellId2PackingSite[cell->getCellId()] = curPackingSite;
                        }

                        movedPUs.insert(curPU);
                        movedPUs.insert(nextPU);
                        local_movedPUs.insert(curPU);
                        local_movedPUs.insert(nextPU);

                        if (preserverCriticalPath && useLocalFineTune &&
                            !tryAcceptLocalTimingDetailedPlacement(local_movedPUs, local_changedPackingSites))
                        {
                            for (auto aPU : local_movedPUs)
                            {
                                movedPUs.erase(aPU);
                            }
                        }
                    }
                }

                delete trialClusterWithCurPU;
                delete trialClusterWithNextPU;
            }
        }

        std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
        for (auto tmpPU : movedPUs)
        {
            if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
            {
                cellLoc[unpackCell->getCell()->getCellId()].X = tmpPU->X();
                cellLoc[unpackCell->getCell()->getCellId()].Y = tmpPU->Y();
            }
            else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
            {
                assert(curMacro->getCARRYNum() + curMacro->getDSPNum() + curMacro->getBRAMNum() <= 1);
                assert(!curMacro->isPacked());
                int cellIdInMacro = 0;
                for (auto tmpCell : curMacro->getCells())
                {
                    cellLoc[tmpCell->getCellId()].X = tmpPU->X();
                    cellLoc[tmpCell->getCellId()].Y = tmpPU->Y();
                    cellIdInMacro++;
                }
            }

            if (PUId2PackingCLBSite[tmpPU->getId()]->getDeterminedClusterInSite())
            {
                if (!PUId2PackingCLBSite[tmpPU->getId()]->getDeterminedClusterInSite()->contains(tmpPU))
                {
                    std::cout << PUId2PackingCLBSite[tmpPU->getId()]->getDeterminedClusterInSite() << "\n";
                    std::cout << "Does not contain " << tmpPU << "\n";
                    assert(false && "target packing site should contain a specific PU");
                }
            }
        }

        if (preserverCriticalPath && !useLocalFineTune && movedPUs.size())
        {
            float oriMaxFanoutDelay = timingOptimizer->getMaxDelay();
            timingOptimizer->incrementalStaticTimingAnalysis(movedPUs, true);
            if (timingNodes[cellIdsInCriticalPath[0]]->checkIsRegister())
            {
                oriMaxFanoutDelay = timingOptimizer->getOriginalMaxIncrementalDelay();
            }

            if ((timingOptimizer->getMaxIncrementalDelay() > oriMaxFanoutDelay * 1.01) ||
                (-timingOptimizer->getIncrementalTotalTimingBenefit() >
                     (oriMaxFanoutDelay - timingOptimizer->getMaxIncrementalDelay()) * 0.8 &&
                 timingOptimizer->getIncrementalTotalTimingBenefit() < 0.05 &&
                 (oriMaxFanoutDelay - timingOptimizer->getMaxIncrementalDelay()) > 0.05))
            {
                // print_warning("ParallelCLBPacker: critical path delay is increased to " +
                //               std::to_string(timingOptimizer->getMaxIncrementalDelay()) + " and oriMaxFanoutDelay=" +
                //               std::to_string(oriMaxFanoutDelay) + " . Give up this path optimization and recover");
                partiallyRecoverToBestPacking(changedPackingSites);
                timingOptimizer->recoverArrivalDelayFromLastIncrementalSTA();
                movedPUs.clear();
            }
            else
            {
                for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
                {
                    auto cellId = cellIdsInCriticalPath[orderI];
                    auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                    PUsTouched.insert(curPU);
                    PUsDontTouch.insert(curPU);
                }

                // print_warning("ParallelCLBPacker: critical path delay is " +
                //               std::to_string(timingOptimizer->getMaxIncrementalDelay()));
                partiallyRecordBestPacking(changedPackingSites);
                // Keep accepted moves until replaceCnt is updated below.
                // movedPUs is scoped to this path and discarded afterwards.
            }
        }
        else
        {
            for (int orderI = cellIdsInCriticalPath.size() - 1; orderI >= 0; orderI--)
            {
                auto cellId = cellIdsInCriticalPath[orderI];
                auto curPU = placementInfo->getPlacementUnitByCellId(cellId);
                PUsTouched.insert(curPU);
                PUsDontTouch.insert(curPU);
            }
        }

        replaceCnt += movedPUs.size();
        if (replaceCnt > 2000 && preserverCriticalPath)
            break;
    }

    print_status("ParallelCLBPacker: conducted timing-driven detailed placement (swaping) and " +
                 std::to_string(replaceCnt) + " PlacementUnits are replaced.");
    return replaceCnt;
}

bool ParallelCLBPacker::tryAcceptLocalTimingDetailedPlacement(
    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> &movedPUs,
    std::set<ParallelCLBPacker::PackingCLBSite *> &changedPackingSites)
{
    timingOptimizer->incrementalStaticTimingAnalysis(movedPUs, true);
    float oriMaxFanoutDelay = timingOptimizer->getOriginalMaxIncrementalDelay();

    // bool res = !(timingOptimizer->getMaxIncrementalDelay() > oriMaxFanoutDelay * 1.005) ||
    //            (-timingOptimizer->getIncrementalTotalTimingBenefit() >
    //                 (oriMaxFanoutDelay - timingOptimizer->getMaxIncrementalDelay()) * 0.8 &&
    //             timingOptimizer->getIncrementalTotalTimingBenefit() < 0.05 &&
    //             (oriMaxFanoutDelay - timingOptimizer->getMaxIncrementalDelay()) > 0.05);

    bool res = !(timingOptimizer->getMaxIncrementalDelay() > oriMaxFanoutDelay * 1.005) ||
               ((timingOptimizer->getMaxIncrementalDelay() < oriMaxFanoutDelay + 0.025) &&
                (timingOptimizer->getIncrementalTotalTimingBenefit() >
                 (timingOptimizer->getMaxIncrementalDelay() - oriMaxFanoutDelay + 0.05)));

    if (!res)
    {
        // print_warning("ParallelCLBPacker: critical path delay is increased to " +
        //               std::to_string(timingOptimizer->getMaxIncrementalDelay()) + " and oriMaxFanoutDelay=" +
        //               std::to_string(oriMaxFanoutDelay) + " . Give up this path optimization and recover");
        partiallyRecoverToBestPacking(changedPackingSites);
        timingOptimizer->recoverArrivalDelayFromLastIncrementalSTA();
    }
    else
    {
        // print_warning("ParallelCLBPacker: critical path delay is " +
        //               std::to_string(timingOptimizer->getMaxIncrementalDelay()));
        partiallyRecordBestPacking(changedPackingSites);
    }

    return res;
}

void ParallelCLBPacker::checkPackedPUsAndUnpackedPUs()
{
    AMF_PROFILE_FUNCTION("final_packing");
    packedPUs.clear();
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                {
                    assert(packedPUs.find(tmpPU) == packedPUs.end());
                    packedPUs.insert(tmpPU);
                }
            }
        }
    }

    unpackedPUs.clear();
    unpackedPUsVec.clear();
    for (auto tmpCell : placementInfo->getCells())
    {
        if (tmpCell->isLUT() || tmpCell->isFF())
        {
            auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);
            if (packedPUs.find(tmpPU) != packedPUs.end())
                continue;
            if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
            {
                if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_CARRY ||
                    tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MCLB)
                {
                    continue;
                }
            }
            if (unpackedPUs.find(tmpPU) == unpackedPUs.end())
            {
                unpackedPUs.insert(tmpPU);
                unpackedPUsVec.push_back(tmpPU);
            }
        }
    }
}

void ParallelCLBPacker::exceptionHandling(bool verbose)
{
    AMF_PROFILE_FUNCTION("final_packing");
    checkPackedPUsAndUnpackedPUs();
    print_status("ParallelCLBPacker: start exceptionHandling.");

    std::vector<bool> isLegalizedPU(placementUnits.size(), false);

    float Dc = maxD * 0.5;

    auto inputUnpackedPUsVec = unpackedPUsVec;

    std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> processedPUs;
    processedPUs.clear();

    PUPoints.clear();
    for (auto PU : inputUnpackedPUsVec)
    {
        if (!PU->isLocked())
            PUPoints.emplace_back(PU);
    }

    size_t previousUnpacked = PUPoints.size();
    int stagnantRounds = 0;
    int tmpFFCnt = 0, tmpLUTCnt = 0;
    float avgFFWeight = 0, avgLUTWeight = 0;
    for (unsigned int i = 0; i < PUPoints.size(); i++)
    {
        auto tmpPU = PUPoints[i].getPU();
        if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
        {
            if (unpackCell->getCell()->isFF())
            {
                tmpFFCnt++;
                avgFFWeight += placementInfo->getActualOccupation(unpackCell->getCell());
            }
            else if (unpackCell->getCell()->isLUT())
            {
                tmpLUTCnt++;
                avgLUTWeight += placementInfo->getActualOccupation(unpackCell->getCell());
            }
        }
        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
        {
            assert(curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                   curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_MCLB);
            assert(!curMacro->isPacked());
            for (auto tmpCell : curMacro->getCells())
            {
                if (tmpCell->isFF())
                {
                    tmpFFCnt++;
                    avgFFWeight += placementInfo->getActualOccupation(tmpCell);
                }
                else if (tmpCell->isLUT())
                {
                    tmpLUTCnt++;
                    avgLUTWeight += placementInfo->getActualOccupation(tmpCell);
                }
            }
        }
    }
    if (tmpFFCnt) avgFFWeight /= tmpFFCnt;
    if (tmpLUTCnt) avgLUTWeight /= tmpLUTCnt;
    print_info("there are " + std::to_string(tmpFFCnt) + " FFs(avgW=" + std::to_string(avgFFWeight) + ") and " +
               std::to_string(tmpLUTCnt) + " LUTs(avgW=" + std::to_string(avgLUTWeight) + ") ");

    while (PUPoints.size())
    {
        timingOptimizer->getPUId2Slack(true); // update PU slack information
        // build k-d tree
        kdt::KDTree<PULocation> kdtree(PUPoints, y2xRatio);
        processedPUs.clear();
        print_status("ParallelCLBPacker: starting parallel ripping up for " + std::to_string(PUPoints.size()) +
                     " PUs and current displacement threshold for ripping up is " + std::to_string(Dc));
        // loop until there is no unprocessed PU
        while (processedPUs.size() < PUPoints.size())
        {
            std::vector<PlacementInfo::PlacementUnit *> noRipUpOverlapPUs;
            std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> coveredPUs;
            noRipUpOverlapPUs.clear();
            coveredPUs.clear();

            // find unprocessed PUs and try to rip up them with Dc
            for (auto &tmpPUPoint : PUPoints)
            {
                // avoid that the potential ripup site cause conflict so the PUs to be processed should far enough
                // between each othters.
                if (coveredPUs.find(tmpPUPoint.getPU()) != coveredPUs.end())
                    continue;
                if (processedPUs.find(tmpPUPoint.getPU()) != processedPUs.end())
                    continue;
                if (isLegalizedPU[tmpPUPoint.getPU()->getId()])
                    continue;
                noRipUpOverlapPUs.push_back(tmpPUPoint.getPU());
                coveredPUs.insert(tmpPUPoint.getPU());
                processedPUs.insert(tmpPUPoint.getPU());
                std::vector<int> indices = kdtree.radiusSearch(tmpPUPoint, 4 * Dc + 2);
                for (auto tmpInd : indices)
                {
                    assert(std::fabs(PUPoints[tmpInd].getPU()->X() - tmpPUPoint.getPU()->X()) +
                               y2xRatio * std::fabs(PUPoints[tmpInd].getPU()->Y() - tmpPUPoint.getPU()->Y()) <=
                           4 * Dc + 2);
                    coveredPUs.insert(PUPoints[tmpInd].getPU());
                }
            }

            // print_status("ParallelCLBPacker: find " + std::to_string(noRipUpOverlapPUs.size()) +
            //              " relatively independent PUs from " + std::to_string(PUPoints.size()) + " PUs. Total " +
            //              std::to_string(processedPUs.size()) + " have been tried.");

            unsigned int numNoRipUpOverlapPUs = noRipUpOverlapPUs.size();
            // involvedPackingSite2PU.clear();

#pragma omp parallel for schedule(dynamic)
            for (unsigned int i = 0; i < numNoRipUpOverlapPUs; i++)
            {
                isLegalizedPU[noRipUpOverlapPUs[i]->getId()] = exceptionPULegalize(noRipUpOverlapPUs[i], Dc, verbose);
            }

            int successCnt = 0;
            for (unsigned int i = 0; i < numNoRipUpOverlapPUs; i++)
            {
                successCnt += isLegalizedPU[noRipUpOverlapPUs[i]->getId()];
            }
            // print_status("ParallelCLBPacker: successfully rip up " + std::to_string(successCnt) + " PUs of the " +
            //              std::to_string(noRipUpOverlapPUs.size()) + " PUs.");
        }
        // remove those successfully ripup PU from PUPoints
        int unprocessedCnt = 0;
        for (unsigned int i = 0; i < PUPoints.size(); i++)
        {
            if (!isLegalizedPU[PUPoints[i].getPU()->getId()])
            {
                PUPoints[unprocessedCnt] = PUPoints[i];
                unprocessedCnt++;
            }
        }
        PUPoints.resize(unprocessedCnt);
        stagnantRounds = PUPoints.size() == previousUnpacked ? stagnantRounds + 1 : 0;
        previousUnpacked = PUPoints.size();
        if (stagnantRounds == 8 && !PUPoints.empty()) {
            print_warning("CLB packing made no progress for 8 sweeps; recording remaining PUs");
            if (JSONCfg.count("BoundaryReportDirectory")) {
                std::ofstream diagnostic(JSONCfg.at("BoundaryReportDirectory") + "/packing_stall.tsv", std::ios::app);
                if (diagnostic.tellp() == 0)
                    diagnostic << "radius\tpu_id\tpu_name\tx\ty\tcell_name\tcell_type\n";
                for (const auto &point : PUPoints) {
                    auto pu = point.getPU();
                    auto writeCell = [&](DesignInfo::DesignCell *cell) {
                        diagnostic << Dc << '\t' << pu->getId() << '\t' << pu->getName() << '\t' << pu->X() << '\t'
                                   << pu->Y() << '\t' << cell->getName() << '\t' << int(cell->getCellType()) << '\n';
                    };
                    if (auto single = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) writeCell(single->getCell());
                    else if (auto macro = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
                        for (auto cell : macro->getCells()) writeCell(cell);
                }
            }
        }
        if (stagnantRounds == 8 && clockRegionAware && !placementInfo->boundaryClusteringEnabled()) {
            // A clock-region column is an optimization preference, not a user fence.
            // Restart near the PU: continuing the outer annulus would miss nearby
            // legal sites in the newly admitted columns.
            clockRegionAware = false;
            Dc = std::max(1.0f, maxD * 0.5f);
            stagnantRounds = 0;
            print_warning("PACKING_COLUMN_FALLBACK remaining=" + std::to_string(PUPoints.size()) +
                          " restart_radius=" + std::to_string(Dc));
        } else {
            Dc += 0.3 * maxD;
        }

        if (placementInfo->isDensePlacement())
            clockRegionAware = false;

        if (Dc > 300)
        {
            for (unsigned int i = 0; i < PUPoints.size(); i++)
            {
                std::cout << PUPoints[i].getPU() << "\n=================================================\n";
            }
            throw std::runtime_error("CLB packing exceeded bounded search radius with " +
                                     std::to_string(PUPoints.size()) + " unpacked placement units");
        }
        if (Dc < 1)
            Dc = 1;
        print_status("ParallelCLBPacker: there are " + std::to_string(unprocessedCnt) +
                     " PUs left to be legalized and current displacement threshold for ripping up is " +
                     std::to_string(Dc));
        int tmpFFCnt = 0, tmpLUTCnt = 0;
        float avgFFWeight = 0, avgLUTWeight = 0;
        for (unsigned int i = 0; i < PUPoints.size(); i++)
        {
            auto tmpPU = PUPoints[i].getPU();
            if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
            {
                if (unpackCell->getCell()->isFF())
                {
                    tmpFFCnt++;
                    avgFFWeight += placementInfo->getActualOccupation(unpackCell->getCell());
                }
                else if (unpackCell->getCell()->isLUT())
                {
                    tmpLUTCnt++;
                    avgLUTWeight += placementInfo->getActualOccupation(unpackCell->getCell());
                }
            }
            else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
            {
                assert(curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                       curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_MCLB);
                assert(!curMacro->isPacked());
                for (auto tmpCell : curMacro->getCells())
                {
                    if (tmpCell->isFF())
                    {
                        tmpFFCnt++;
                        avgFFWeight += placementInfo->getActualOccupation(tmpCell);
                    }
                    else if (tmpCell->isLUT())
                    {
                        tmpLUTCnt++;
                        avgLUTWeight += placementInfo->getActualOccupation(tmpCell);
                    }
                }
            }
        }
        if (tmpFFCnt) avgFFWeight /= tmpFFCnt;
        if (tmpLUTCnt) avgLUTWeight /= tmpLUTCnt;
        print_info("there are " + std::to_string(tmpFFCnt) + " FFs(avgW=" + std::to_string(avgFFWeight) + ") and " +
                   std::to_string(tmpLUTCnt) + " LUTs(avgW=" + std::to_string(avgLUTWeight) + ") ");
        placementInfo->updateElementBinGrid(false);
        timingOptimizer->conductStaticTimingAnalysis();
    }

    if (verbose)
    {
        print_status("ParallelCLBPacker::exceptionHandling done!");
    }
}

bool ParallelCLBPacker::exceptionPULegalize(PlacementInfo::PlacementUnit *curPU, float displacementThreshold,
                                            bool verbose)
{
    AMF_PROFILE_FUNCTION("final_packing");

    std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceThePU = nullptr;
    if (displacementThreshold < 4)
        candidateSitesToPlaceThePU = findNeiborSitesFromBinGrid(DesignInfo::CellType_LUT4, curPU->X(), curPU->Y(), 0,
                                                                displacementThreshold, y2xRatio, clockRegionAware);
    else
        candidateSitesToPlaceThePU =
            findNeiborSitesFromBinGrid(DesignInfo::CellType_LUT4, curPU->X(), curPU->Y(), displacementThreshold - 3,
                                       displacementThreshold, y2xRatio, clockRegionAware);
    std::vector<siteWithScore> sitesToRipUp;
    sitesToRipUp.clear();
    for (auto tmpSite : *candidateSitesToPlaceThePU)
    {
        float lambda1 = 0.02;
        float lambda2 = 1.0;
        float lambda3 = 4.0;
        assert(deviceSite2PackingSite.find(tmpSite) != deviceSite2PackingSite.end());
        PackingCLBSite *packingSite = deviceSite2PackingSite[tmpSite];

        // if (involvedPackingSite2PU.find(packingSite) != involvedPackingSite2PU.end())
        // {
        //     if (involvedPackingSite2PU[packingSite] != curPU)
        //     {
        //         std::cout << "displacementThr: " << displacementThreshold << "\n";
        //         std::cout << "curPU: " << curPU << "\n";
        //         std::cout << "prevPU: " << involvedPackingSite2PU[packingSite] << "\n";
        //         std::cout << " packingSite: " << packingSite->getCLBSite()->getName()
        //                   << " X:" << packingSite->getCLBSite()->X() << " Y:" << packingSite->getCLBSite()->Y() <<
        //                   "\n";
        //         assert(false);
        //     }
        // }

        // involvedPackingSite2PU[packingSite] = curPU;

        float changeHPWL = 0;
        for (auto tmpNet : *(curPU->getNetsSetPtr()))
        {
            if (tmpNet->getUnits().size() > 64) // ignore large net
                continue;
            changeHPWL += tmpNet->getNewHPWLByTrying(curPU, tmpSite->X(), tmpSite->Y(), y2xRatio);
            changeHPWL -= tmpNet->getHPWL(y2xRatio);
            assert(!std::isnan(changeHPWL));
        }

        float packedScore = 0;
        float score = -lambda1 * changeHPWL;
        // std::cout << "site: " << packingSite->getCLBSite()->getName() << " changeHPWL:" << changeHPWL << "\n";
        if (packingSite->getDeterminedClusterInSite())
        {
            packingSite->getDeterminedClusterInSite()->updateScoreInSite();
            packedScore = packingSite->getDeterminedClusterInSite()->getScoreInSite();
            score += -lambda2 * packedScore - lambda3 * packingSite->getDeterminedClusterInSite()->getTotalCellWeight();
            // std::cout << "packedScore: " << packedScore << "\n"
            //           << " CellWeight:" << packingSite->getDeterminedClusterInSite()->getTotalCellWeight() << "\n"
            //           << "DeterminedClusterInSite: " << packingSite->getDeterminedClusterInSite() << "\n";
        }

        sitesToRipUp.emplace_back(packingSite, score);
    }
    std::sort(sitesToRipUp.begin(), sitesToRipUp.end(), [](const siteWithScore &a, const siteWithScore &b) -> bool {
        return a.score == b.score
                   ? (a.site->getCLBSite()->getElementIdInParent() > b.site->getCLBSite()->getElementIdInParent())
                   : (a.score > b.score);
    });

    // if (verbose)
    // {
    //     for (auto pair : sitesToRipUp)
    //     {
    //         std::cout << "site: " << pair.site->getCLBSite()->getName() << " X:" << pair.site->getCLBSite()->X()
    //                   << " Y:" << pair.site->getCLBSite()->Y() << " score:" << pair.score << "\n";
    //     }
    //     std::cout << "\n";
    //     std::cout.flush();
    // }
    delete candidateSitesToPlaceThePU;

    // std::cout << "start check sites\n";
    std::cout.flush();
    for (auto pair : sitesToRipUp)
    {

        PackingCLBSite *packingSite = pair.site;
        std::map<PackingCLBSite *, PackingCLBSite::PackingCLBCluster *> packingSite2DeterminedCluster;
        packingSite2DeterminedCluster.clear(); // record the original determined cluster for rolling back

        // std::cout << "start check sites: ripUpAndLegalizae\n";
        if (ripUpAndLegalizae(packingSite, curPU, displacementThreshold, packingSite2DeterminedCluster, verbose))
        {
            return true;
        }
        else
        {
            assert(packingSite->getDeterminedClusterInSite());
            for (auto pair : packingSite2DeterminedCluster)
            {
                if (pair.first->getDeterminedClusterInSite())
                    delete pair.first->getDeterminedClusterInSite();
                pair.first->setDeterminedClusterInSite(pair.second);
            }
        }
    }
    return false;
    // std::cout << "handled unpacked PU: " << curPU << "\n candidateSites:\n";
    // for (auto pair : sitesToRipUp)
    // {
    //     std::cout << "site: " << pair.site->getName() << " X:" << pair.site->X() << " Y:" << pair.site->Y()
    //               << " score:" << pair.score << "\n";
    // }

    // std::cout << "\n";
    // std::cout.flush();
    // assert(false);
}

bool ParallelCLBPacker::ripUpAndLegalizae(
    PackingCLBSite *curTargetPackingSite, PlacementInfo::PlacementUnit *curPU, float displacementThreshold,
    std::map<PackingCLBSite *, PackingCLBSite::PackingCLBCluster *> &packingSite2DeterminedCluster, bool verbose)
{
    AMF_PROFILE_FUNCTION("final_packing");
    PackingCLBSite::PackingCLBCluster *backup_determinedCluster = nullptr;

    if (curTargetPackingSite->getDeterminedClusterInSite())
    {

        backup_determinedCluster =
            new PackingCLBSite::PackingCLBCluster(curTargetPackingSite->getDeterminedClusterInSite());

        // if (verbose)
        // {
        //     std::cout << "ripUpAndLegalizae target site: " << curTargetPackingSite->getCLBSite()->getName()
        //               << " X:" << curTargetPackingSite->getCLBSite()->X()
        //               << " Y:" << curTargetPackingSite->getCLBSite()->Y() << "\n";
        //     std::cout << "and its determined cluster is :\n" << backup_determinedCluster << "\n";
        //     std::cout.flush();
        // }
        delete curTargetPackingSite->getDeterminedClusterInSite();
    }

    packingSite2DeterminedCluster[curTargetPackingSite] = backup_determinedCluster;
    curTargetPackingSite->setDeterminedClusterInSite(nullptr);

    if (curTargetPackingSite->checkIsCarrySite())
    {
        curTargetPackingSite->addCarry();
    }
    else if (curTargetPackingSite->checkIsLUTRAMSite())
    {
        curTargetPackingSite->addLUTRAMMacro();
    }
    else
    {
        curTargetPackingSite->setDeterminedClusterInSite(new PackingCLBSite::PackingCLBCluster(curTargetPackingSite));
    }

    // if (verbose)
    // {
    //     std::cout << "start ripUpAndLegalizae\n";
    //     std::cout.flush();
    // }

    assert(curTargetPackingSite->getDeterminedClusterInSite());
    assert(curTargetPackingSite->getDeterminedClusterInSite()->getPUs().size() == 0);
    if (curTargetPackingSite->getDeterminedClusterInSite()->addPU(curPU))
    {
        // if (verbose)
        // {
        //     std::cout << "successfully curTargetPackingSite->getDeterminedClusterInSite()->addPU(curPU)\n";
        //     std::cout.flush();
        // }

        if (backup_determinedCluster)
        {

            std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> evictedPUs = backup_determinedCluster->getPUs();
            std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> notEvictedPUs;
            notEvictedPUs.clear();

            auto &PUId2Slack = timingOptimizer->getPUId2Slack();
            std::vector<PUWithScore> PUsWithSlack;
            PUsWithSlack.clear();
            for (auto evictedPU : evictedPUs)
            {
                PUsWithSlack.emplace_back(evictedPU, PUId2Slack[evictedPU->getId()]);
            }
            std::sort(PUsWithSlack.begin(), PUsWithSlack.end(), [](const PUWithScore &a, const PUWithScore &b) -> bool {
                return a.score == b.score ? (a.PU->getId() > b.PU->getId()) : (a.score < b.score);
            });

            for (auto evictedPU_scorePair : PUsWithSlack)
            {
                auto evictedPU = evictedPU_scorePair.PU;
                assert(curTargetPackingSite->getDeterminedClusterInSite()->getPUs().find(evictedPU) ==
                       curTargetPackingSite->getDeterminedClusterInSite()->getPUs().end());
                // if (verbose)
                // {
                //     std::cout << " curTargetPackingSite->getDeterminedClusterInSite()->tryAddPU(evictedPU)\n";
                //     std::cout.flush();
                // }
                assert(notEvictedPUs.find(evictedPU) == notEvictedPUs.end());
                if (curTargetPackingSite->getDeterminedClusterInSite()->tryAddPU(evictedPU))
                {
                    // if (verbose)
                    // {
                    //     std::cout << " before
                    //     curTargetPackingSite->getDeterminedClusterInSite()->addPU(evictedPU)\n"; std::cout.flush();
                    // }
                    // if (!curTargetPackingSite->getDeterminedClusterInSite()->addPU(evictedPU))
                    // {
                    //     std::cout << "cluster:\n" << curTargetPackingSite->getDeterminedClusterInSite() << "\n";
                    //     curTargetPackingSite->getDeterminedClusterInSite()->addPUFailReason(evictedPU);
                    //     assert(false);
                    // }
                    curTargetPackingSite->getDeterminedClusterInSite()->incrementalUpdateScoreInSite(evictedPU);
                    curTargetPackingSite->getDeterminedClusterInSite()->getHash();
                    notEvictedPUs.insert(evictedPU);

                    // if (verbose)
                    // {
                    //     std::cout
                    //         << " successfully
                    //         curTargetPackingSite->getDeterminedClusterInSite()->addPU(evictedPU)\n";
                    //     std::cout.flush();
                    // }
                }
            }

            for (auto evictedPU : evictedPUs)
            {
                if (notEvictedPUs.find(evictedPU) != notEvictedPUs.end())
                    continue;
                // std::cout << "rip up the evicted PU\n";
                std::vector<DeviceInfo::DeviceSite *> *candidateSitesToPlaceThePU = nullptr;
                if (displacementThreshold < 4)
                    candidateSitesToPlaceThePU = findNeiborSitesFromBinGrid(
                        DesignInfo::CellType_LUT4, curTargetPackingSite->getCLBSite()->X(),
                        curTargetPackingSite->getCLBSite()->Y(), 0, displacementThreshold, y2xRatio, clockRegionAware);
                else
                    candidateSitesToPlaceThePU =
                        findNeiborSitesFromBinGrid(DesignInfo::CellType_LUT4, curTargetPackingSite->getCLBSite()->X(),
                                                   curTargetPackingSite->getCLBSite()->Y(), displacementThreshold - 3,
                                                   displacementThreshold, y2xRatio, clockRegionAware);

                float highestScoreIncrease = -1e5;
                PackingCLBSite::PackingCLBCluster *bestClusterToPack = nullptr;
                for (auto tmpSite : *candidateSitesToPlaceThePU)
                {
                    assert(deviceSite2PackingSite.find(tmpSite) != deviceSite2PackingSite.end());
                    PackingCLBSite *tmpPackingSiteForEvictim = deviceSite2PackingSite[tmpSite];

                    // if (involvedPackingSite2PU.find(tmpPackingSiteForEvictim) != involvedPackingSite2PU.end())
                    // {
                    //     if (involvedPackingSite2PU[tmpPackingSiteForEvictim] != curPU)
                    //     {
                    //         std::cout << "displacementThr: " << displacementThreshold << "\n";
                    //         std::cout << "curPU: " << curPU << "\n";
                    //         std::cout << "prevPU: " << involvedPackingSite2PU[tmpPackingSiteForEvictim] << "\n";
                    //         std::cout << " tmpPackingSiteForEvictim: "
                    //                   << tmpPackingSiteForEvictim->getCLBSite()->getName()
                    //                   << " X:" << tmpPackingSiteForEvictim->getCLBSite()->X()
                    //                   << " Y:" << tmpPackingSiteForEvictim->getCLBSite()->Y() << "\n";
                    //         assert(false);
                    //     }
                    // }
                    // involvedPackingSite2PU[tmpPackingSiteForEvictim] = curPU;

                    PackingCLBSite::PackingCLBCluster *tmpCluster = nullptr;
                    if (tmpPackingSiteForEvictim->getDeterminedClusterInSite())
                    {
                        tmpCluster = new PackingCLBSite::PackingCLBCluster(
                            tmpPackingSiteForEvictim->getDeterminedClusterInSite());
                    }
                    else
                    {
                        tmpCluster = new PackingCLBSite::PackingCLBCluster(tmpPackingSiteForEvictim);
                    }

                    tmpCluster->updateScoreInSite();
                    float oriScore = tmpCluster->getScoreInSite();
                    // std::cout << "adding into tmpCluster \n";
                    if (tmpCluster->addPU(evictedPU))
                    {

                        tmpCluster->incrementalUpdateScoreInSite(evictedPU);
                        tmpCluster->getHash();
                        float newScore = tmpCluster->getScoreInSite();
                        // std::cout << "successfully and oriScore=" << oriScore << " newScore=" << newScore << "\n";
                        if (!bestClusterToPack)
                        {
                            bestClusterToPack = tmpCluster;
                            highestScoreIncrease = newScore - oriScore;
                            // std::cout << "the cluster is better. Update bestClusterToPack\n";
                        }
                        else
                        {
                            if (highestScoreIncrease < newScore - oriScore)
                            {
                                delete bestClusterToPack;
                                bestClusterToPack = tmpCluster;
                                highestScoreIncrease = newScore - oriScore;
                                // std::cout << "the cluster is better. Update bestClusterToPack\n";
                            }
                            else
                            {
                                delete tmpCluster;
                                // std::cout << "the cluster is not good enough.\n";
                            }
                        }
                    }
                    else
                    {
                        // std::cout << "failed\n";
                        delete tmpCluster;
                    }
                }
                delete candidateSitesToPlaceThePU;
                if (bestClusterToPack)
                {
                    PackingCLBSite *evictPUToPackingSite = bestClusterToPack->getParentPackingCLB();
                    if (packingSite2DeterminedCluster.find(evictPUToPackingSite) ==
                        packingSite2DeterminedCluster.end()) // if the determined cluster has no backup yet, backup
                    // the determined cluster of the site might be changed by multiple PU but we only record the most
                    // original determined cluster
                    {
                        if (evictPUToPackingSite->getDeterminedClusterInSite())
                        {
                            auto backUp = new PackingCLBSite::PackingCLBCluster(
                                evictPUToPackingSite->getDeterminedClusterInSite());
                            packingSite2DeterminedCluster[evictPUToPackingSite] =
                                backUp; // back up for later rolling back.
                        }
                        else
                        {
                            // if the site has no determined cluster, it must be not site for CLB with CARRY
                            assert(!evictPUToPackingSite->checkIsPrePackedSite());
                            packingSite2DeterminedCluster[evictPUToPackingSite] =
                                nullptr; // back up for later rolling back.
                        }
                    }
                    if (evictPUToPackingSite->getDeterminedClusterInSite())
                        delete evictPUToPackingSite->getDeterminedClusterInSite();
                    evictPUToPackingSite->setDeterminedClusterInSite(bestClusterToPack);
                }
                else
                {
                    // std::cout << "failed to ripUpAndLegalizae target site: "
                    //           << curTargetPackingSite->getCLBSite()->getName()
                    //           << " X:" << curTargetPackingSite->getCLBSite()->X()
                    //           << " Y:" << curTargetPackingSite->getCLBSite()->Y() << "because evictedPU: " <<
                    //           evictedPU
                    //           << "\n cannot be added into it\n";
                    return false;
                }
            }
        }
    }
    else
    {
        // std::cout << "failed to ripUpAndLegalizae target site: " << curTargetPackingSite->getCLBSite()->getName()
        //           << " X:" << curTargetPackingSite->getCLBSite()->X()
        //           << " Y:" << curTargetPackingSite->getCLBSite()->Y() << "because curPU cannot be added into it\n";
        return false;
    }
    // std::cout << "successfully ripUpAndLegalizae target site: " << curTargetPackingSite->getCLBSite()->getName()
    //           << " X:" << curTargetPackingSite->getCLBSite()->X() << " Y:" << curTargetPackingSite->getCLBSite()->Y()
    //           << "\n";
    return true;
}

std::vector<DeviceInfo::DeviceSite *> *
ParallelCLBPacker::findNeiborSitesFromBinGrid(DesignInfo::DesignCellType curCellType, float targetX, float targetY,
                                              float displacementLowerbound, float displacementUpperbound,
                                              float y2xRatio, bool clockRegionAware, float v1x, float v1y, float v2x,
                                              float v2y, int numLimit, bool sortCandidate)
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(displacementLowerbound < displacementUpperbound);
    // Physical regions are soft preferences; ordinary clock-region columns are not placement fences.
    const bool physicalRegions = placementInfo->boundaryClusteringEnabled();
    if (physicalRegions) clockRegionAware = false;
    // please note that the input DesignCell is only used to find the corresponding binGrid for site search.
    std::vector<DeviceInfo::DeviceSite *> *res = new std::vector<DeviceInfo::DeviceSite *>();
    res->clear();

    int binIdX, binIdY;
    placementInfo->getGridXY(targetX, targetY, binIdX, binIdY);

    auto sharedTypeIds = placementInfo->getPotentialBELTypeIDs(curCellType);

    int clockRegionX, clockRegionY;
    placementInfo->getDeviceInfo()->getClockRegionByLocation(targetX, targetY, clockRegionX, clockRegionY);

    float vxCom, vyCom;
    vxCom = v1x + v2x;
    vyCom = v1y + v2y;
    float site1X = targetX + v1x;
    float site1Y = targetY + v1y;
    float site2X = targetX + v2x;
    float site2Y = targetY + v2y;

    std::vector<DeviceSiteWithScore> resToBeSorted;

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
            float bin2TargetXYDistance = curBin->getManhattanDistanceTo(targetX, targetY);
            if (bin2TargetXYDistance > displacementUpperbound)
                continue;
            for (auto tmpSite : curBin->getCorrespondingSites())
            {
                float vx = tmpSite->X() - targetX;
                float vy = tmpSite->Y() - targetY;
                float tmpPUDis = fabs(vx) + y2xRatio * fabs(vy);
                if (tmpPUDis > displacementLowerbound && tmpPUDis <= displacementUpperbound &&
                    std::fabs(getAngle(vx, vy, vxCom, vyCom)) < 0.42 * M_PI)
                {
                    int siteClockRegionX, siteClockRegionY;
                    placementInfo->getDeviceInfo()->getClockRegionByLocation(tmpSite->X(), targetY, siteClockRegionX,
                                                                             siteClockRegionY);
                    if (siteClockRegionX != clockRegionX && clockRegionAware)
                        continue;
                    // The bin grid can include reserved sites omitted by this packer.
                    if (!deviceSite2PackingSite.count(tmpSite))
                        continue;

                    if (sortCandidate)
                    {
                        float newDelay = timingOptimizer->getDelayByModel(tmpSite->X(), tmpSite->Y(), site1X, site1Y) +
                                         timingOptimizer->getDelayByModel(tmpSite->X(), tmpSite->Y(), site2X, site2Y);
                        resToBeSorted.emplace_back(tmpSite, newDelay);
                    }
                    else
                        res->push_back(tmpSite);
                }
            }

            if (resToBeSorted.size() > numLimit)
                break;
            if (res->size() > numLimit)
                break;

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
                    float nextBin2TargetXYDistance = nextBin->getManhattanDistanceTo(targetX, targetY);
                    if (nextBin2TargetXYDistance > displacementUpperbound)
                        continue;

                    std::pair<int, int> nextXY(nextX, nextY);
                    if (reachedBinXYs.find(nextXY) != reachedBinXYs.end())
                    {
                        continue;
                    }

                    std::vector<float> binXB;
                    binXB.push_back(nextBin->left());
                    binXB.push_back(nextBin->right());

                    std::vector<float> binYB;
                    binYB.push_back(nextBin->top());
                    binYB.push_back(nextBin->bottom());

                    bool overlap = false;
                    for (auto bX : binXB)
                    {
                        for (auto bY : binYB)
                        {
                            float vx = bX - targetX;
                            float vy = bY - targetY;
                            if (std::fabs(getAngle(vx, vy, vxCom, vyCom)) < 0.42 * M_PI)
                            {
                                overlap = true;
                            }
                        }
                    }

                    if (reachedBinXYs.find(nextXY) == reachedBinXYs.end())
                    {
                        reachedBinXYs.insert(nextXY);
                        if (overlap)
                            binXYqueue.push(nextXY);
                    }
                }
            }
        }
    }
    if (sortCandidate)
    {
        std::sort(resToBeSorted.begin(), resToBeSorted.end(),
                  [](const DeviceSiteWithScore &a, const DeviceSiteWithScore &b) -> bool {
                      return a.score == b.score ? (a.site->getElementIdInParent() > b.site->getElementIdInParent())
                                                : (a.score < b.score);
                  });

        for (auto site_delay : resToBeSorted)
            res->push_back(site_delay.site);
    }

    return res;
}

std::vector<DeviceInfo::DeviceSite *> *
ParallelCLBPacker::findNeiborSitesFromBinGrid(DesignInfo::DesignCellType curCellType, float targetX, float targetY,
                                              float displacementLowerbound, float displacementUpperbound,
                                              float y2xRatio, bool clockRegionAware)
{
    AMF_PROFILE_FUNCTION("final_packing");
    assert(displacementLowerbound < displacementUpperbound);
    // Physical regions are soft preferences; ordinary clock-region columns are not placement fences.
    const bool physicalRegions = placementInfo->boundaryClusteringEnabled();
    if (physicalRegions) clockRegionAware = false;
    // please note that the input DesignCell is only used to find the corresponding binGrid for site search.
    std::vector<DeviceInfo::DeviceSite *> *res = new std::vector<DeviceInfo::DeviceSite *>();
    res->clear();

    int binIdX, binIdY;
    placementInfo->getGridXY(targetX, targetY, binIdX, binIdY);

    auto sharedTypeIds = placementInfo->getPotentialBELTypeIDs(curCellType);

    int clockRegionX, clockRegionY;
    placementInfo->getDeviceInfo()->getClockRegionByLocation(targetX, targetY, clockRegionX, clockRegionY);

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
            float bin2TargetXYDistance = curBin->getManhattanDistanceTo(targetX, targetY);
            if (bin2TargetXYDistance > displacementUpperbound)
                continue;
            for (auto tmpSite : curBin->getCorrespondingSites())
            {
                float tmpPUDis = fabs(targetX - tmpSite->X()) + y2xRatio * fabs(targetY - tmpSite->Y());
                if (tmpPUDis > displacementLowerbound && tmpPUDis <= displacementUpperbound)
                {
                    int siteClockRegionX, siteClockRegionY;
                    placementInfo->getDeviceInfo()->getClockRegionByLocation(tmpSite->X(), targetY, siteClockRegionX,
                                                                             siteClockRegionY);
                    if (siteClockRegionX != clockRegionX && clockRegionAware)
                        continue;
                    // The bin grid can include reserved sites omitted by this packer.
                    if (!deviceSite2PackingSite.count(tmpSite))
                        continue;

                    res->push_back(tmpSite);
                }
            }

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
                    float nextBin2TargetXYDistance = nextBin->getManhattanDistanceTo(targetX, targetY);
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

    return res;
}

void ParallelCLBPacker::setPULocationToPackedSite()
{
    AMF_PROFILE_FUNCTION("final_packing");
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                if (packingSite->getDeterminedClusterInSite()->getPUs().size())
                {
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        tmpPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                        packingSite->getCLBSite()->Y());
                        tmpPU->setFixed();
                    }
                }
            }
            else if (packingSite->checkIsNonCLBSite())
            {
                if (packingSite->getNonCLBCell())
                {
                    auto curPU = placementInfo->getPlacementUnitByCellId(packingSite->getNonCLBCell()->getCellId());
                    if (curPU->isLocked())
                    {
                        continue;
                    }
                    curPU->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                    packingSite->getCLBSite()->Y());
                    curPU->setFixed();
                }
            }
        }
    }
    placementInfo->updateElementBinGrid(false);
    //  placementInfo->updateB2BAndGetTotalHPWL();
}

void ParallelCLBPacker::setPUsToBePacked()
{
    AMF_PROFILE_FUNCTION("final_packing");
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                if (packingSite->getDeterminedClusterInSite()->getPUs().size())
                {
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        tmpPU->setPacked();
                    }
                }
            }
        }
    }
}

void ParallelCLBPacker::updatePackedMacro(bool setPUPseudoNetToCLBSite, bool setCLBFixed)
{
    AMF_PROFILE_FUNCTION("final_packing");
    std::vector<PlacementInfo::PlacementUnit *> packedPUs;
    std::vector<PlacementInfo::Location> &cellLoc = placementInfo->getCellId2location();
    std::map<PlacementInfo::PlacementUnit *, float> PU2X, PU2Y;
    std::map<PlacementInfo::PlacementUnit *, std::vector<DeviceInfo::DeviceSite *>> PU2Sites;
    PU2Sites.clear();
    PU2X.clear();
    PU2Y.clear();
    for (auto packingSite : packingSites)
    {
        if (packingSite)
        {
            if (packingSite->getDeterminedClusterInSite())
            {
                if (packingSite->getDeterminedClusterInSite()->getPUs().size() >= 1)
                {
                    std::vector<DesignInfo::DesignCellType> cellsToAdd_cellType(0);
                    std::vector<DesignInfo::DesignCell *> cellsToAdd(0);
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                        {
                            cellsToAdd.push_back(unpackCell->getCell());
                            cellsToAdd_cellType.push_back(unpackCell->getCell()->getCellType());
                        }
                        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                        {
                            assert(curMacro->getMacroType() !=
                                       PlacementInfo::PlacementMacro::PlacementMacroType_CARRY &&
                                   curMacro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_MCLB);
                            assert(!curMacro->isPacked());
                            int cellIdInMacro = 0;
                            for (auto tmpCell : curMacro->getCells())
                            {
                                cellsToAdd.push_back(tmpCell);
                                cellsToAdd_cellType.push_back(curMacro->getVirtualCellType(cellIdInMacro));
                                cellIdInMacro++;
                            }
                        }
                        placementUnits[tmpPU->getId()] = nullptr;
                        placementInfo->deleteLegalizationInfoFor(tmpPU);
                        delete tmpPU;
                    }
                    assert(cellsToAdd.size() >= 1);
                    PlacementInfo::PlacementMacro *curMacro = new PlacementInfo::PlacementMacro(
                        cellsToAdd[0]->getName(), -1, PlacementInfo::PlacementMacro::PlacementMacroType_LCLB);
                    curMacro->setAnchorLocationAndForgetTheOriginalOne(packingSite->getCLBSite()->X(),
                                                                       packingSite->getCLBSite()->Y());
                    int totalWeight = 0;
                    for (unsigned int tmpId = 0; tmpId < cellsToAdd.size(); tmpId++)
                    {
                        curMacro->addCell(cellsToAdd[tmpId], cellsToAdd_cellType[tmpId], 0, 0.0);
                        cellLoc[cellsToAdd[tmpId]->getCellId()].X = packingSite->getCLBSite()->X();
                        cellLoc[cellsToAdd[tmpId]->getCellId()].Y = packingSite->getCLBSite()->Y();

                        cellId2PlacementUnit[cellsToAdd[tmpId]->getCellId()] = curMacro;
                        cellInMacros.insert(cellsToAdd[tmpId]);
                        totalWeight += placementInfo->getCompatiblePlacementTable()
                                           ->cellType2sharedBELTypeOccupation[cellsToAdd_cellType[tmpId]];
                    }
                    curMacro->setWeight(totalWeight);
                    curMacro->setPacked();
                    if (setCLBFixed)
                        curMacro->setFixed();
                    assert((unsigned int)curMacro->getWeight() >= curMacro->getCells().size());
                    curMacro->addOccupiedSite(0.0, 1);
                    packedPUs.push_back(curMacro);
                    placementUnits.push_back(curMacro);
                    if (setPUPseudoNetToCLBSite)
                    {
                        PU2X[curMacro] = packingSite->getCLBSite()->X();
                        PU2Y[curMacro] = packingSite->getCLBSite()->Y();
                        PU2Sites[curMacro] = std::vector<DeviceInfo::DeviceSite *>();
                        PU2Sites[curMacro].push_back(packingSite->getCLBSite());
                    }

                    for (auto LUTPair : packingSite->getDeterminedClusterInSite()->getPairedLUTs())
                    {
                        placementInfo->setDeterminedOccupation(LUTPair.first->getCellId(), 1);
                        placementInfo->setDeterminedOccupation(LUTPair.second->getCellId(), 1);
                    }

                    if (cellsToAdd.size() > 8)
                    {
                        for (auto &tmpControlSet : packingSite->getDeterminedClusterInSite()->getFFControlSets())
                        {
                            float estimatedFFUtil = 4.0 / (float)tmpControlSet.getSize();

                            for (auto tmpFF : tmpControlSet.getFFs())
                            {
                                placementInfo->setDeterminedOccupation(tmpFF->getCellId(), estimatedFFUtil);
                            }
                        }
                    }
                }
            }
        }
    }
    unsigned int validCnt = 0;
    placementMacros.clear();
    placementUnpackedCells.clear();
    for (unsigned int tmpPUId = 0; tmpPUId < placementUnits.size(); tmpPUId++)
    {
        if (placementUnits[tmpPUId])
        {
            placementUnits[validCnt] = placementUnits[tmpPUId];
            placementUnits[validCnt]->renewId(validCnt);
            if (auto unpackCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(placementUnits[validCnt]))
            {
                placementUnpackedCells.push_back(unpackCell);
            }
            else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(placementUnits[validCnt]))
            {
                placementMacros.push_back(curMacro);
                assert((unsigned int)curMacro->getWeight() >= curMacro->getCells().size());
            }
            validCnt++;
        }
    }
    placementUnits.resize(validCnt);

    print_status("ParallelCLBPacker Updating Cell-PlacementUnit Mapping");
    placementInfo->updateCells2PlacementUnits();

    // since new placement units might be generated, we need to update the net information
    print_status("ParallelCLBPacker Loading Nets");
    placementInfo->reloadNets();

    // ensure that each cell is only assigned into one placementUnit.
    std::map<DesignInfo::DesignCell *, PlacementInfo::PlacementUnit *> cell2PUMap;

    cell2PUMap.clear();
    for (auto unpackedCell : placementUnpackedCells)
    {
        DesignInfo::DesignCell *curCell = unpackedCell->getCell();
        assert(cell2PUMap.find(curCell) == cell2PUMap.end());
        cell2PUMap[curCell] = unpackedCell;
    }

    for (auto curMacro : placementMacros)
    {
        for (auto curCell : curMacro->getCells())
        {
            if (cell2PUMap.find(curCell) != cell2PUMap.end())
            {
                std::cout << "recorded PU:" << cell2PUMap[curCell] << "\n";
                std::cout << "another PU:" << curMacro << "\n";
                std::cout.flush();
            }
            assert(cell2PUMap.find(curCell) == cell2PUMap.end());
            cell2PUMap[curCell] = curMacro;
        }
    }

    placementInfo->setPULegalSite(PU2Sites);
    placementInfo->setPULegalXY(PU2X, PU2Y);
    placementInfo->updateB2BAndGetTotalHPWL();
}

bool hasLUT62(ParallelCLBPacker::PackingCLBSite::SiteBELMapping &slots)
{
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            for (int j = 0; j < 2; j++)
            {
                if (slots.LUTs[i][j][k])
                {
                    if (slots.LUTs[i][j][k]->isVirtualCell())
                        continue;
                    if (slots.LUTs[i][j][k]->getOriCellType() == DesignInfo::CellType_LUT6_2)
                        return true;
                }
            }
        }
    }
    return false;
}

void ParallelCLBPacker::addNonCLBPackingSites()
{
    AMF_PROFILE_FUNCTION("final_packing");
    print_status("ParallelCLBPacker: handling Non-CLB Packing Sites");
    for (auto &DSPBRAM_LegalSitePair : placementInfo->getPULegalSite())
    {
        if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(DSPBRAM_LegalSitePair.first))
        {
            if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
            {
                assert(tmpMacro->getCells().size() == DSPBRAM_LegalSitePair.second.size());
                for (unsigned int i = 0; i < tmpMacro->getCells().size(); i++)
                {
                    auto curCell = tmpMacro->getCells()[i];
                    auto targetSite = DSPBRAM_LegalSitePair.second[i];
                    if (!curCell->isVirtualCell())
                    {
                        if (curCell->getOriCellType() == DesignInfo::CellType_RAMB36E2 ||
                            curCell->getOriCellType() == DesignInfo::CellType_FIFO36E2)
                        {
                            assert(targetSite->getSiteY() % 2 == 0);
                            PackingCLBSite *tmpPackingSite = new PackingCLBSite(
                                placementInfo, targetSite, unchangedIterationThr, numNeighbor, deltaD, curD, maxD,
                                PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                            tmpPackingSite->setNonCLBCell(curCell);
                            deviceSite2PackingSite[targetSite] = tmpPackingSite;
                            packingSites.push_back(tmpPackingSite);
                        }
                        else if (curCell->getOriCellType() == DesignInfo::CellType_RAMB18E2 ||
                                 curCell->getOriCellType() == DesignInfo::CellType_FIFO18E2)
                        {
                            PackingCLBSite *tmpPackingSite = new PackingCLBSite(
                                placementInfo, targetSite, unchangedIterationThr, numNeighbor, deltaD, curD, maxD,
                                PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                            tmpPackingSite->setNonCLBCell(curCell);
                            deviceSite2PackingSite[targetSite] = tmpPackingSite;
                            packingSites.push_back(tmpPackingSite);
                        }
                        else
                        {
                            assert(false && "undefined situtation");
                        }
                    }
                    else
                    {
                        assert(targetSite->getSiteY() % 2 == 1);
                    }
                }
            }
            else if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_DSP)
            {
                assert(tmpMacro->getCells().size() == DSPBRAM_LegalSitePair.second.size());
                for (unsigned int i = 0; i < tmpMacro->getCells().size(); i++)
                {
                    auto curCell = tmpMacro->getCells()[i];
                    auto targetSite = DSPBRAM_LegalSitePair.second[i];
                    if (!curCell->isVirtualCell())
                    {
                        PackingCLBSite *tmpPackingSite =
                            new PackingCLBSite(placementInfo, targetSite, unchangedIterationThr, numNeighbor, deltaD,
                                               curD, maxD, PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                        tmpPackingSite->setNonCLBCell(curCell);
                        deviceSite2PackingSite[targetSite] = tmpPackingSite;
                        packingSites.push_back(tmpPackingSite);
                    }
                    else
                    {
                        assert(false && "there should be no virtual DSP");
                    }
                }
            }
            else
            {
                // assert(false && "undefined situtation");
            }
        }
        else if (auto tmpUnppackedCell =
                     dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(DSPBRAM_LegalSitePair.first))
        {
            assert(1 == DSPBRAM_LegalSitePair.second.size());
            auto curCell = tmpUnppackedCell->getCell();
            auto targetSite = DSPBRAM_LegalSitePair.second[0];
            if (curCell->getOriCellType() == DesignInfo::CellType_RAMB18E2)
            {
                PackingCLBSite *tmpPackingSite =
                    new PackingCLBSite(placementInfo, targetSite, unchangedIterationThr, numNeighbor, deltaD, curD,
                                       maxD, PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                tmpPackingSite->setNonCLBCell(curCell);
                deviceSite2PackingSite[targetSite] = tmpPackingSite;
                packingSites.push_back(tmpPackingSite);
            }
            else if (curCell->getOriCellType() == DesignInfo::CellType_DSP48E2 || curCell->isURAM())
            {
                PackingCLBSite *tmpPackingSite =
                    new PackingCLBSite(placementInfo, targetSite, unchangedIterationThr, numNeighbor, deltaD, curD,
                                       maxD, PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                tmpPackingSite->setNonCLBCell(curCell);
                deviceSite2PackingSite[targetSite] = tmpPackingSite;
                packingSites.push_back(tmpPackingSite);
            }
            else
            {
                // assert(false && "undefined situtation");
            }
        }
    }

    for (auto unpackedCell : placementInfo->getPlacementUnpackedCells())
    {
        auto curCell = unpackedCell->getCell();
        if (curCell->isIO() && unpackedCell->isLocked())
        {
            auto lockedSite = unpackedCell->getLockedSite();
            assert(lockedSite);
            PackingCLBSite *tmpPackingSite =
                new PackingCLBSite(placementInfo, lockedSite, unchangedIterationThr, numNeighbor, deltaD, curD, maxD,
                                   PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
            tmpPackingSite->setNonCLBCell(curCell);
            deviceSite2PackingSite[lockedSite] = tmpPackingSite;
            packingSites.push_back(tmpPackingSite);
        }
    }

    auto sharedTypeIds = placementInfo->getPotentialBELTypeIDs(DesignInfo::CellType_DSP48E2);

    for (auto sharedTypeId : sharedTypeIds)
    {
        std::vector<std::vector<PlacementInfo::PlacementBinInfo *>> &curBinGrid =
            placementInfo->getBinGrid(sharedTypeId);
        for (auto &row : curBinGrid)
        {
            for (auto curBin : row)
            {
                for (auto curSite : curBin->getCorrespondingSites())
                {
                    if (deviceSite2PackingSite.find(curSite) != deviceSite2PackingSite.end())
                        continue;
                    PackingCLBSite *tmpPackingSite =
                        new PackingCLBSite(placementInfo, curSite, unchangedIterationThr, numNeighbor, deltaD, curD,
                                           maxD, PQSize, y2xRatio, HPWLWeight, PUId2PackingCLBSite);
                    tmpPackingSite->clearNonCLBCell();
                    deviceSite2PackingSite[curSite] = tmpPackingSite;
                    packingSites.push_back(tmpPackingSite);
                }
            }
        }
    }
    print_status("ParallelCLBPacker: added Non-CLB Packing Sites");
}

void ParallelCLBPacker::dumpDSPBRAMPlacementTcl(std::ofstream &outfileTcl)
{
    AMF_PROFILE_FUNCTION("export_vivado");
    for (auto &DSPBRAM_LegalSitePair : placementInfo->getPULegalSite())
    {
        std::string placementStr = "";
        if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(DSPBRAM_LegalSitePair.first))
        {
            if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_BRAM)
            {
                assert(tmpMacro->getCells().size() == DSPBRAM_LegalSitePair.second.size());
                for (unsigned int i = 0; i < tmpMacro->getCells().size(); i++)
                {
                    auto curCell = tmpMacro->getCells()[i];
                    auto targetSite = DSPBRAM_LegalSitePair.second[i];
                    if (!curCell->isVirtualCell())
                    {
                        if (curCell->getOriCellType() == DesignInfo::CellType_RAMB36E2 ||
                            curCell->getOriCellType() == DesignInfo::CellType_FIFO36E2)
                        {
                            assert(targetSite->getSiteY() % 2 == 0);
                            placementStr += "  " + curCell->getName() + " RAMB36_X" +
                                            std::to_string(targetSite->getSiteX()) + "Y" +
                                            std::to_string(targetSite->getSiteY() / 2) + "\n";
                        }
                        else if (curCell->getOriCellType() == DesignInfo::CellType_RAMB18E2 ||
                                 curCell->getOriCellType() == DesignInfo::CellType_FIFO18E2)
                        {
                            if (targetSite->getSiteY() % 2)
                            {
                                placementStr +=
                                    "  " + curCell->getName() + " " + targetSite->getName() + "/RAMB18E2_U" + "\n";
                            }
                            else
                            {
                                placementStr +=
                                    "  " + curCell->getName() + " " + targetSite->getName() + "/RAMB18E2_L" + "\n";
                            }
                        }
                        else
                        {
                            assert(false && "undefined situtation");
                        }
                    }
                    else
                    {
                        assert(targetSite->getSiteY() % 2 == 1);
                    }
                }
            }
            else if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_DSP)
            {
                assert(tmpMacro->getCells().size() == DSPBRAM_LegalSitePair.second.size());
                for (unsigned int i = 0; i < tmpMacro->getCells().size(); i++)
                {
                    auto curCell = tmpMacro->getCells()[i];
                    auto targetSite = DSPBRAM_LegalSitePair.second[i];
                    if (!curCell->isVirtualCell())
                    {
                        placementStr += "  " + curCell->getName() + " " + targetSite->getName() + "/DSP_ALU" + "\n";
                    }
                    else
                    {
                        assert(false && "there should be no virtual DSP");
                    }
                }
            }
            else
            {
                // assert(false && "undefined situtation");
            }
        }
        else if (auto tmpUnppackedCell =
                     dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(DSPBRAM_LegalSitePair.first))
        {
            assert(1 == DSPBRAM_LegalSitePair.second.size());
            auto curCell = tmpUnppackedCell->getCell();
            auto targetSite = DSPBRAM_LegalSitePair.second[0];
            if (curCell->getOriCellType() == DesignInfo::CellType_RAMB18E2)
            {
                if (targetSite->getSiteY() % 2)
                {
                    placementStr += "  " + curCell->getName() + " " + targetSite->getName() + "/RAMB18E2_U" + "\n";
                }
                else
                {
                    placementStr += "  " + curCell->getName() + " " + targetSite->getName() + "/RAMB18E2_L" + "\n";
                }
            }
            else if (curCell->isURAM())
            {
                placementStr += "  " + curCell->getName() + " " + targetSite->getName() + "/URAM_288K_INST\n";
            }
            else if (curCell->getOriCellType() == DesignInfo::CellType_DSP48E2)
            {
                placementStr += "  " + curCell->getName() + " " + targetSite->getName() + "/DSP_ALU" + "\n";
            }
            else
            {
                // assert(false && "undefined situtation");
            }
        }
        if (placementStr != "")
        {
            std::string oriBrace = "[";
            std::string newBrace = "\\[";
            outfileTcl << "set result "
                       << "[catch {place_cell {" << placementStr << "}}]\n"
                       << "if {$result} {\n incr errorNum \n";
            std::vector<std::string> splitItems;
            splitItems.clear();
            strSplit(placementStr, splitItems, "\n");
            for (auto item : splitItems)
            {
                replaceAll(item, oriBrace, newBrace);
                outfileTcl << "puts $fo \"" << item << " \"\n";
            }
            outfileTcl << "\n}\n";
            placementStr = "";
        }
    }
}

bool containLUTRAMCells(PlacementInfo::PlacementUnit *curPU)
{
    if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
    {
        return unpackedCell->getCell()->originallyIsLUTRAM() || unpackedCell->getCell()->originallyIsShifter();
    }
    else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
    {
        for (auto tmpCell : curMacro->getCells())
        {
            if (tmpCell->originallyIsLUTRAM() || tmpCell->originallyIsShifter())
            {
                return true;
            }
        }
        return false;
    }
    return false;
}

void ParallelCLBPacker::dumpCLBPlacementTcl(std::ofstream &outfileTcl, bool packingRelatedToLUT6_2)
{
    AMF_PROFILE_FUNCTION("export_vivado");
    std::string placementStr = "";
    int cnt = 0;
    for (auto tmpPackingSite : packingSites)
    {
        auto slotMapping = tmpPackingSite->getSlotMapping();
        auto CLBSite = tmpPackingSite->getCLBSite();
        if ((packingRelatedToLUT6_2 && !hasLUT62(slotMapping)) || (!packingRelatedToLUT6_2 && hasLUT62(slotMapping)))
            continue;

        if (tmpPackingSite->getDeterminedClusterInSite())
        {
            if (!CLBSiteLegality::checkControlsAndLUTs(slotMapping))
                throw std::runtime_error("CLB slot control/LUT legality failed before export: " + CLBSite->getName());
            if (tmpPackingSite->checkIsCarrySite())
            {
                cnt++;
                placementStr += slotMapping.Carry->getName() + " " + CLBSite->getName() + "/CARRY8  \n";
            }
            else if (tmpPackingSite->checkIsMuxSite())
            {
                for (int i = 0; i < 2; i++)
                {
                    if (slotMapping.MuxF8[i])
                    {
                        if (slotMapping.MuxF8[i]->isVirtualCell())
                            continue;
                        assert(slotMapping.MuxF8[i]->getOriCellType() == DesignInfo::CellType_MUXF8);
                        cnt++;
                        placementStr += "  " + slotMapping.MuxF8[i]->getName() + "  " + CLBSite->getName() + "/" +
                                        slotMapping.MuxF8SlotNames[i] + "  \n";
                    }
                    for (int j = 0; j < 2; j++)
                    {
                        if (slotMapping.MuxF7[i][j])
                        {
                            if (slotMapping.MuxF7[i][j]->isVirtualCell())
                                continue;
                            cnt++;
                            placementStr += "  " + slotMapping.MuxF7[i][j]->getName() + "  " + CLBSite->getName() +
                                            "/" + slotMapping.MuxF7SlotNames[i][j] + "  \n";
                        }
                    }
                }
            }
            else if (tmpPackingSite->checkIsLUTRAMSite())
            {
                for(auto tmpMacro:tmpPackingSite->getLUTRAMMacros())
                if (containLUTRAMCells(tmpMacro))
                {
                    // A valid macro may have an odd number of cells (two SRLs
                    // and one F7, for example). Never silently omit its placement.
                    if (!tmpMacro->getFixedCellInfoVec().empty())
                    {
                        for (const auto &fixed : tmpMacro->getFixedCellInfoVec())
                        {
                            placementStr += "  " + fixed.cell->getName() + "  " + CLBSite->getName() + "/" +
                                            fixed.BELName + "  \n";
                            cnt++;
                        }
                    }
                    else
                    {
                        placementStr += "  " + tmpMacro->getName() + "  " + CLBSite->getName() + "/H6LUT  \n";
                        cnt++;
                    }
                }
            }

            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        if (slotMapping.LUTs[i][j][k])
                        {
                            if (slotMapping.LUTs[i][j][k]->isVirtualCell())
                                continue;
                            assert(slotMapping.LUTs[i][j][k]->isLUT());
                            char LUTCode = (i * 4 + k) + 'A';
                            if (j == 0)
                            {
                                std::string LUTSiteName = std::string(1, LUTCode) + "6LUT";
                                cnt++;
                                placementStr += "  " + slotMapping.LUTs[i][j][k]->getName() + " " + CLBSite->getName() +
                                                "/" + LUTSiteName + "\n";
                            }
                            else
                            {
                                if (slotMapping.LUTs[i][j][k]->getOriCellType() == DesignInfo::CellType_LUT6_2)
                                    continue;

                                std::string LUTSiteName = std::string(1, LUTCode) + "5LUT";
                                cnt++;
                                placementStr += "  " + slotMapping.LUTs[i][j][k]->getName() + " " + CLBSite->getName() +
                                                "/" + LUTSiteName + "\n";
                            }
                        }
                    }
                }
            }
            for (int i = 0; i < 2; i++)
            {
                for (int k = 0; k < 4; k++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        if (slotMapping.FFs[i][j][k])
                        {
                            if(!slotMapping.FFs[i][j][k]->isVirtualCell() &&
                               (placementInfo->getFfQBlockedMask(CLBSite->getName()) & FfQPinMask::bit(i,j,k)))
                                throw std::runtime_error("FF Q access violation at export: "+CLBSite->getName());
                            if (slotMapping.FFs[i][j][k]->isVirtualCell())
                                continue;
                            assert(slotMapping.FFs[i][j][k]->isFF());
                            auto PU = placementInfo->getPlacementUnitByCell(slotMapping.FFs[i][j][k]);
                            if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(PU))
                            {
                                if (tmpMacro->getMacroType() ==
                                    PlacementInfo::PlacementMacro::
                                        PlacementMacroType_LCLB) // currently we failed to predict the LCLB macros, so
                                                                 // leave it to Vivado placement.
                                    continue;
                            }
                            char FFCode = (i * 4 + k) + 'A';
                            if (j == 0)
                            {
                                std::string FFSiteName = std::string(1, FFCode) + "FF";
                                cnt++;
                                placementStr += "  " + slotMapping.FFs[i][j][k]->getName() + " " + CLBSite->getName() +
                                                "/" + FFSiteName + "\n";
                            }
                            else
                            {
                                std::string FFSiteName = std::string(1, FFCode) + "FF2";
                                cnt++;
                                placementStr += "  " + slotMapping.FFs[i][j][k]->getName() + " " + CLBSite->getName() +
                                                "/" + FFSiteName + "\n";
                            }
                        }
                    }
                }
            }
        }
        if (cnt > 100)
        {
            if (placementStr != "")
            {
                std::string oriBrace = "[";
                std::string newBrace = "\\[";
                outfileTcl << "set result "
                           << "[catch {place_cell {" << placementStr << "}}]\n"
                           << "if {$result} {\n incr errorNum \n";
                std::vector<std::string> splitItems;
                splitItems.clear();
                strSplit(placementStr, splitItems, "\n");
                for (auto item : splitItems)
                {
                    replaceAll(item, oriBrace, newBrace);
                    outfileTcl << "puts $fo \"" << item << " \"\n";
                }
                outfileTcl << "\n}\n";
                placementStr = "";
            }
            cnt = 0;
        }
    }
    if (placementStr != "")
    {
        outfileTcl << "set result "
                   << "[catch {place_cell {" << placementStr << "}}]\n"
                   << "if {$result} {\n incr errorNum \nputs $fo \"" << placementStr << "\"\n}\n";
    }
}

void ParallelCLBPacker::dumpPlacementTcl(std::string dumpTclFile)
{
    AMF_PROFILE_FUNCTION("export_vivado");
    std::ofstream outfileTcl(dumpTclFile);
    assert(outfileTcl.is_open() && outfileTcl.good() &&
           "The path for placement Tcl dumping does not exist and please check your path settings");
    if (placementInfo->getIncrementalContext().enabled || placementInfo->hasHardRegions())
    {
        const auto &dfx = placementInfo->getIncrementalContext();
        for (const auto &entry : dfx.fixed)
        {
            auto cellName = entry.first;
            auto cell = designInfo->getCell(cellName);
            auto pu = placementInfo->getPlacementUnitByCell(cell);
            auto siteName = entry.second.site;
            auto site = deviceInfo->getSite(siteName);
            if (!pu->isFixed() || !pu->isLocked() ||
                std::fabs(pu->X() - site->X()) > 1e-5f || std::fabs(pu->Y() - site->Y()) > 1e-5f)
                throw std::runtime_error("DFX static anchor moved inside AMF: " + entry.first);
        }
        for(const auto &entry:dfx.endpoints) {
            auto name=entry.first;auto cell=designInfo->getCell(name);auto pu=placementInfo->getPlacementUnitByCell(cell);
            if(!cell->isInterfaceEndpoint()||!pu->isFixed()||!pu->isLocked()||
               std::fabs(pu->X()-entry.second.x)>1e-5f||std::fabs(pu->Y()-entry.second.y)>1e-5f)
                throw std::runtime_error("Native interface endpoint moved: "+name);
        }
        if(dfx.enabled) print_info("DFX_STATIC_ANCHORS_VERIFIED count=" + std::to_string(dfx.fixed.size()));
        else print_info("DFX_FULL_DESIGN_PLACEMENT_EXPORT");
        // Data-only export: the DFX adapter parses place_cell lists. Never source
        // this file on a locked shell or execute whole-design unplace commands.
        outfileTcl << "error {DFX placement data: use scripts/run_dfx.py, do not source this file}\n";
        dumpDSPBRAMPlacementTcl(outfileTcl);
        dumpCLBPlacementTcl(outfileTcl, true);
        dumpCLBPlacementTcl(outfileTcl, false);
        outfileTcl.close();
        if(designInfo->nativeStaticCache) {
            AMF_PROFILE_SCOPE("export_native_cache", "persist final static fabric state");
            designInfo->nativeStaticCache->savePlacement(dumpTclFile,JSONCfg);
        }
        return;
    }
    outfileTcl << "set script_path [ file dirname [ file normalize [ info script ] ] ]\n";
    outfileTcl
        << "set fo [open \"${script_path}/initialPlacementError\" \"w\"]\nplace_design -unplace\nset errorNum 0\n";
    dumpDSPBRAMPlacementTcl(outfileTcl);
    dumpCLBPlacementTcl(outfileTcl, true);
    // Except LUT6_2 cells, remove the packing information in Vivado and use our packing
    outfileTcl << "foreach amf_prop {HLUTNM SOFT_HLUTNM} {\n"
                  "  set amf_grouped [get_cells -quiet -hierarchical -filter \"$amf_prop != \\\"\\\"\"]\n"
                  "  if {[llength $amf_grouped]} {set_property $amf_prop {} $amf_grouped}\n"
                  "}\n";
    dumpCLBPlacementTcl(outfileTcl, false);
    // outfileTcl << "place_design\n";
    outfileTcl << "$errorNum\n";
    outfileTcl << "close $fo \n"
                  "set a [open \"${script_path}/initialPlacementError\"]\n"
                  "set lines [split [read $a] \"\\n\"]\n"
                  "close $a;\n"
                  "exec rm \"${script_path}/initialPlacementError\"\n"
                  "set b [open \"${script_path}/placementError\" \"w\"]\n"
                  "set lineCnt 0\n"
                  "set placeBatch {}\n"
                  "foreach line $lines {\n"
                  "   incr lineCnt\n"
                  "   set placeBatch [concat $placeBatch $line]\n"
                  "   if {$lineCnt == 2 } {\n"
                  "       set result [catch {place_cell  $placeBatch }]\n"
                  "       if {$result} {\n"
                  "           puts $b $placeBatch\n"
                  "       }\n"
                  "       set lineCnt 0\n"
                  "       set placeBatch \"\"\n"
                  "    }\n"
                  "}\n"
                  "close $b\n";
    outfileTcl << "place_design\nroute_design\n";
    outfileTcl.close();
}

void ParallelCLBPacker::dumpFinalPacking()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    if (JSONCfg.find("DumpCLBPacking") != JSONCfg.end())
    {
        std::string dumpTclFile =
            JSONCfg["DumpCLBPacking"] + "-" + packerName + "-" + std::to_string(DumpCLBPackingCnt) + ".tcl";
        std::string dumpFile =
            JSONCfg["DumpCLBPacking"] + "-" + packerName + "-" + std::to_string(DumpCLBPackingCnt) + ".gz";

        print_status("ParallelCLBPacker: dumping CLBPacking archieve to: " + dumpFile);

        std::stringstream outfile0;

        for (auto tmpPackingSite : packingSites)
        {
            if (tmpPackingSite->getDeterminedClusterInSite())
            {
                outfile0 << tmpPackingSite->getDeterminedClusterInSite() << "\n";

                outfile0 << "LUTSlots:\n";
                for (int i = 0; i < 2; i++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        for (int k = 0; k < 4; k++)
                        {
                            if (tmpPackingSite->getSlotMapping().LUTs[i][j][k])
                            {
                                outfile0 << "i,j,k:" << i << "," << j << "," << k << "   "
                                         << tmpPackingSite->getSlotMapping().LUTs[i][j][k] << "\n";
                            }
                        }
                    }
                }
                outfile0 << "FFSlots:\n";
                for (int i = 0; i < 2; i++)
                {
                    for (int j = 0; j < 2; j++)
                    {
                        for (int k = 0; k < 4; k++)
                        {
                            if (tmpPackingSite->getSlotMapping().FFs[i][j][k])
                            {
                                outfile0 << "i,j,k:" << i << "," << j << "," << k << "   "
                                         << tmpPackingSite->getSlotMapping().FFs[i][j][k] << "\n";
                            }
                        }
                    }
                }
            }
        }

        writeStrToGZip(dumpFile, outfile0);
        print_status("ParallelCLBPacker: dumped CLBPacking archieve to: " + dumpFile);

        std::vector<PackingCLBSite *> PUId2PackingCLBSite;
        PUId2PackingCLBSite.clear();
        PUId2PackingCLBSite.resize(placementInfo->getPlacementUnits().size(), nullptr);

        std::vector<DeviceInfo::DeviceSite *> PUId2NeighborSite(placementInfo->getPlacementUnits().size(), nullptr);
        int mappedPUCnt = 0;
        for (auto packingSite : packingSites)
        {
            if (packingSite)
            {
                if (packingSite->getDeterminedClusterInSite())
                {
                    for (auto tmpPU : packingSite->getDeterminedClusterInSite()->getPUs())
                    {
                        assert(!PUId2PackingCLBSite[tmpPU->getId()]);
                        PUId2PackingCLBSite[tmpPU->getId()] = packingSite;
                        mappedPUCnt++;
                    }
                }
            }
        }
        for (auto packingSite : packingSites)
        {
            if (packingSite)
            {
                for (auto tmpPU : packingSite->getNeighborPUs())
                {
                    if (PUId2PackingCLBSite[tmpPU->getId()])
                        continue;
                    if (!PUId2NeighborSite[tmpPU->getId()])
                    {
                        PUId2NeighborSite[tmpPU->getId()] = packingSite->getCLBSite();
                    }
                    else
                    {
                        float oriDis = std::abs(PUId2NeighborSite[tmpPU->getId()]->X() - tmpPU->X()) +
                                       y2xRatio * std::abs(PUId2NeighborSite[tmpPU->getId()]->Y() - tmpPU->Y());
                        float newDis = std::abs(packingSite->getCLBSite()->X() - tmpPU->X()) +
                                       y2xRatio * std::abs(packingSite->getCLBSite()->Y() - tmpPU->Y());
                        if (oriDis > newDis)
                        {
                            PUId2NeighborSite[tmpPU->getId()] = packingSite->getCLBSite();
                        }
                    }
                }
            }
        }

        print_info("#mappedPUCnt = " + std::to_string(mappedPUCnt));

        dumpFile = JSONCfg["DumpCLBPacking"] + "-" + packerName + "-unpackablePUs-" +
                   std::to_string(DumpCLBPackingCnt) + ".gz";
        print_status("ParallelCLBPacker: dumping unpackable PUs archieve to: " + dumpFile);
        std::stringstream outfile1;

        int PUNeedMappingCnt = 0;
        std::set<PlacementInfo::PlacementUnit *, Packing_PUcompare> dumpedPUs;
        dumpedPUs.clear();
        for (auto tmpCell : placementInfo->getCells())
        {
            if (tmpCell->isLUT() || tmpCell->isFF())
            {
                auto tmpPU = placementInfo->getPlacementUnitByCell(tmpCell);
                if (tmpPU->isPacked())
                    continue;
                if (auto tmpMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                {
                    if (tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_CARRY ||
                        tmpMacro->getMacroType() == PlacementInfo::PlacementMacro::PlacementMacroType_MCLB)
                    {
                        continue;
                    }
                }
                if (dumpedPUs.find(tmpPU) != dumpedPUs.end())
                    continue;
                dumpedPUs.insert(tmpPU);
                PUNeedMappingCnt++;
                if (PUId2PackingCLBSite[tmpPU->getId()])
                    continue;
                outfile1 << tmpPU;
                if (PUId2NeighborSite[tmpPU->getId()])
                {
                    outfile1 << "candidate neighbor site: " << PUId2NeighborSite[tmpPU->getId()]->getName()
                             << " X:" << PUId2NeighborSite[tmpPU->getId()]->X()
                             << " Y:" << PUId2NeighborSite[tmpPU->getId()]->Y() << "\n";
                    outfile1 << tmpCell
                             << " estimated util:" << placementInfo->getActualOccupationByCellId(tmpCell->getCellId())
                             << "\n";
                }
                else
                {
                    std::vector<DeviceInfo::DeviceSite *> *candidateSites =
                        placementInfo->findNeiborSiteFromBinGrid(tmpCell, tmpPU->X(), tmpPU->Y(), 5.0, 10);
                    if (candidateSites->size())
                    {
                        for (auto tmpSite : *candidateSites)
                        {
                            if (!PUId2NeighborSite[tmpPU->getId()])
                            {
                                PUId2NeighborSite[tmpPU->getId()] = tmpSite;
                            }
                            else
                            {
                                float oriDis = std::abs(PUId2NeighborSite[tmpPU->getId()]->X() - tmpPU->X()) +
                                               y2xRatio * std::abs(PUId2NeighborSite[tmpPU->getId()]->Y() - tmpPU->Y());
                                float newDis = std::abs(tmpSite->X() - tmpPU->X()) +
                                               y2xRatio * std::abs(tmpSite->Y() - tmpPU->Y());
                                if (oriDis > newDis)
                                {
                                    PUId2NeighborSite[tmpPU->getId()] = tmpSite;
                                }
                            }
                        }
                    }
                    delete candidateSites;
                    outfile1 << "closed neighbor site: " << PUId2NeighborSite[tmpPU->getId()]->getName()
                             << " X:" << PUId2NeighborSite[tmpPU->getId()]->X()
                             << " Y:" << PUId2NeighborSite[tmpPU->getId()]->Y() << "\n";
                    outfile1 << tmpCell
                             << " estimated util:" << placementInfo->getActualOccupationByCellId(tmpCell->getCellId())
                             << "\n";
                }
            }
        }
        writeStrToGZip(dumpFile, outfile1);
        print_status("ParallelCLBPacker: dumped unpackable PUs archieve to: " + dumpFile);
        print_info("#PUNeedMappingCnt = " + std::to_string(PUNeedMappingCnt));

        print_status("ParallelCLBPacker: dumping placementTcl archieve to: " + dumpTclFile);
        dumpPlacementTcl(dumpTclFile);
        print_status("ParallelCLBPacker: dumped placementTcl archieve to: " + dumpTclFile);
        char buf[PATH_MAX]; /* PATH_MAX incudes the \0 so +1 is not required */
        char *res = realpath(dumpTclFile.c_str(), buf);
        assert(res && "this path should exist.");
        std::string absPath(res);
        if (placementInfo->getIncrementalContext().enabled)
            print_info("DFX placement data exported; use the dedicated dfx-run backend: " + absPath);
        else
            print_warning("try Tcl command to P&R in Vivado: source " + absPath);
        DumpCLBPackingCnt++;
    }
}

void ParallelCLBPacker::dumpAllCellsCoordinate()
{
    AMF_PROFILE_FUNCTION("diagnostic_output");
    if (JSONCfg.find("DumpAllCoordTrace") != JSONCfg.end())
    {
        std::string dumpFile =
            JSONCfg["DumpAllCoordTrace"] + "-Packing-" + std::to_string(allCoordinateDumpCnt) + ".gz";
        print_status("GlobalPlacer: dumping coordinate archieve to: " + dumpFile);
        allCoordinateDumpCnt++;
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
                    outfile0 << cellX << " " << cellY << " " << curCell->getName() << "\n";
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

                        if (curMacro->getCell(vId))
                            outfile0 << cellX << " " << cellY << " " << curMacro->getCell(vId)->getName() << "\n";
                        else
                            outfile0 << cellX << " " << cellY << "\n";
                    }
                }
            }
            writeStrToGZip(dumpFile, outfile0);
            print_status("GlobalPlacer: dumped coordinate archieve to: " + dumpFile);
        }
    }
}
