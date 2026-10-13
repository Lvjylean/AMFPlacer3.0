/**
 * @file QPSolverWrapper.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#ifndef _QPSOLVER
#define _QPSOLVER

#include "Eigen/Eigen"
#include "Eigen/SparseCore"
#include "osqp++/osqp++.h"
#include "strPrint.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class QPSolverWrapper
{
  public:
    typedef struct
    {
        std::vector<Eigen::Triplet<float>> objectiveMatrixTripletList;
        std::vector<float> objectiveMatrixDiag;
        Eigen::VectorXd objectiveVector;
        Eigen::VectorXd solution;
        Eigen::VectorXd oriSolution;
    } solverDataType;

    solverDataType solverData;
    osqp::OsqpSolver osqpSolver;

    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper> CGSolver;
    typedef struct
    {
        bool useUnconstrainedCG = true;
        bool MKLorNot = false;
        float lowerbound;
        float upperbound;
        int maxIters = 500;
        float tolerence = 0.001;
        bool solutionForward = false;
        bool verbose = false;
        bool stabilityGuard = false;
    } solverSettingsType;
    solverSettingsType solverSettings;
    struct GuardDiagnostics {
        int repairedRows = 0, iterations = 0;
        double maxDiagonalDelta = 0, relativeError = 0;
        bool converged = false, rollback = false;
        std::string error;
    } guardDiagnostics;

    // Worker errors are returned to the joining thread, never thrown across std::thread.
    static void solveGuarded(QPSolverWrapper &solver);

    QPSolverWrapper(bool useUnconstrainedCG, bool MKLorNot, float lowerbound, float upperbound, int elementNum,
                    bool verbose)
    {
        solverSettings.useUnconstrainedCG = useUnconstrainedCG;
        solverSettings.MKLorNot = MKLorNot;
        solverSettings.lowerbound = lowerbound;
        solverSettings.upperbound = upperbound;
        solverSettings.verbose = verbose;
        solverData.solution.resize(elementNum);
        solverData.oriSolution.resize(elementNum);
        solverData.objectiveVector.resize(elementNum);
    }
    ~QPSolverWrapper()
    {
    }

    static void QPSolve(QPSolverWrapper *&curSolver);
};

#endif