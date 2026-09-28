/**
 * @file SAPlacer.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#ifndef _SAPLACER
#define _SAPLACER

#include "strPrint.h"
#include "sysInfo.h"
#include <assert.h>
#include <boost/random.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

class SAPlacer
{
  public:
    // Explicit values are the effective SA ratio, without the legacy 0.8 factor.
    // Keeping this independent avoids retuning QP, legalizers and packing when
    // calibrating the coarse cluster-placement distance metric.
    static float configuredY2XRatio(const std::map<std::string, std::string> &cfg, float inheritedRatio)
    {
        const auto entry = cfg.find("Simulated Annealing y2xRatio");
        float ratio = static_cast<float>(inheritedRatio * 0.8);
        if (entry != cfg.end())
        {
            try
            {
                size_t consumed = 0;
                ratio = std::stof(entry->second, &consumed);
                if (consumed != entry->second.size())
                    throw std::invalid_argument("trailing characters");
            }
            catch (const std::exception &)
            {
                throw std::invalid_argument("Simulated Annealing y2xRatio must be a finite positive number");
            }
        }
        if (!std::isfinite(ratio) || ratio <= 0)
            throw std::invalid_argument("Simulated Annealing y2xRatio must be a finite positive number");
        return ratio;
    }

    SAPlacer(std::string placerName, std::vector<std::vector<float>> &clusterAdjMat, std::vector<float> &clusterWeights,
             std::vector<std::vector<float>> &cluster2FixedUnitMat, std::vector<float> &fixedX,
             std::vector<float> &fixedY, int gridH, int gridW, float deviceH, float deviceW,
             float connectionToFixedFactor = 5.0, float y2xRatio = 0.8, int Kmax = 100000, int nJobs = 1,
             int restartNum = 10, bool verbose = false)
        : placerName(placerName), clusterAdjMat(clusterAdjMat), clusterWeights(clusterWeights),
          cluster2FixedUnitMat(cluster2FixedUnitMat), fixedX(fixedX), fixedY(fixedY), gridH(gridH), gridW(gridW),
          deviceH(deviceH), deviceW(deviceW), connectionToFixedFactor(connectionToFixedFactor), y2xRatio(y2xRatio),
          Kmax(Kmax), nJobs(nJobs), restartNum(restartNum), verbose(verbose)
    {
    }
    ~SAPlacer()
    {
    }

    void solve();

    inline std::vector<std::pair<int, int>> &getCluster2XY()
    {
        return res_cluster2XY;
    }
    inline std::vector<std::vector<std::vector<int>>> &getGrid2clusters()
    {
        return res_grid2clusters;
    }

    double evaluateClusterPlacement(const std::vector<std::vector<std::vector<int>>> &grid2clusters,
                                    const std::vector<std::pair<int, int>> &cluster2XY);

    double incrementalEvaluateClusterPlacement(const std::vector<std::vector<std::vector<int>>> &grid2clusters,
                                               const std::vector<std::pair<int, int>> &cluster2XY);

  private:
    std::string placerName;

    std::vector<std::vector<float>> &clusterAdjMat;
    std::vector<float> &clusterWeights;

    std::vector<std::vector<float>> &cluster2FixedUnitMat;
    std::vector<float> &fixedX;
    std::vector<float> &fixedY;

    int gridH;
    int gridW;
    float deviceH;
    float deviceW;

    float connectionToFixedFactor;
    float y2xRatio;
    int Kmax;
    int nJobs;
    int restartNum;
    bool verbose;

    std::vector<std::pair<int, int>> res_cluster2XY;
    std::vector<std::vector<std::vector<int>>> res_grid2clusters;
    double resE;

    double SACalibrationOffset;

    void randomSwapInWideRange(const std::vector<std::vector<std::vector<int>>> &grid2clusters,
                               std::vector<std::vector<std::vector<int>>> &new_Grid2clusters,
                               const std::vector<std::pair<int, int>> &cluster2XY,
                               std::vector<std::pair<int, int>> &new_cluster2XY, float temperature,
                               boost::mt19937 &rng);

    void randomSwapInWideRangeWithNeighbors(const std::vector<std::vector<std::vector<int>>> &grid2clusters,
                                            std::vector<std::vector<std::vector<int>>> &new_Grid2clusters,
                                            const std::vector<std::pair<int, int>> &cluster2XY,
                                            std::vector<std::pair<int, int>> &new_cluster2XY, float temperature,
                                            boost::mt19937 &rng);

    void randomShuffleRowColumn(const std::vector<std::vector<std::vector<int>>> &grid2clusters,
                                std::vector<std::vector<std::vector<int>>> &new_grid2clusters,
                                const std::vector<std::pair<int, int>> &cluster2XY,
                                std::vector<std::pair<int, int>> &new_cluster2XY, boost::mt19937 &rng);

    float probabilituFunc(double oriE, double newE, float T);

    static void worker(SAPlacer *saPlacer, std::vector<std::vector<std::vector<int>>> &init_grid2clusters,
                       std::vector<std::pair<int, int>> &init_cluster2XY,
                       std::vector<std::vector<std::vector<int>>> &opt_grid2clusters,
                       std::vector<std::pair<int, int>> &opt_cluster2XY, int &totalIterNum, int &workers_randomSeed,
                       double &resE);

    void greedyInitialize(std::vector<std::pair<int, int>> &init_cluster2XY,
                          std::vector<std::vector<std::vector<int>>> &init_grid2clusters, int initOffset);

    void greedyPlaceACluster(const std::vector<std::pair<int, int>> &init_cluster2XY,
                             const std::vector<std::vector<std::vector<int>>> &init_grid2clusters,
                             std::vector<std::pair<int, int>> &new_cluster2XY,
                             std::vector<std::vector<std::vector<int>>> &new_grid2clusters, int clusterIdToPlace);

    int greedyFindNextClusterToPlace(std::vector<std::pair<int, int>> &tmp_cluster2XY,
                                     std::vector<std::vector<std::vector<int>>> &tmp_grid2clusters);
};

#endif
