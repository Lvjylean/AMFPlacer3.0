/**
 * @file QPSolverWrapper.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "QPSolverWrapper.h"

#include <cmath>
#include <algorithm>
#include <stdexcept>

void QPSolverWrapper::QPSolve(QPSolverWrapper *&curSolver)
{
    if (curSolver->solverSettings.stabilityGuard) {
        curSolver->guardDiagnostics = GuardDiagnostics{};
        try { solveGuarded(*curSolver); }
        catch (const std::exception &error) { curSolver->guardDiagnostics.error = error.what(); }
        return;
    }
    osqp::OsqpSolver &osqpSolver = curSolver->osqpSolver;

    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper> &CGSolver = curSolver->CGSolver;
    std::vector<Eigen::Triplet<float>> &objectiveMatrixTripletList = curSolver->solverData.objectiveMatrixTripletList;
    Eigen::VectorXd &objectiveVector = curSolver->solverData.objectiveVector;

    // min_x 0.5 * x'Px + q'x
    // s.t.  l <= Ax <= u

    // objective_matrix is P.
    // objective_vector is q.
    // constraint_matrix is A.
    // lower_bounds is l.
    // upper_bounds is u.

    Eigen::SparseMatrix<double> objective_matrix(objectiveVector.size(), objectiveVector.size());
    for (unsigned int i = 0; i < objectiveVector.size(); i++)
        objectiveMatrixTripletList.push_back(Eigen::Triplet<float>(i, i, curSolver->solverData.objectiveMatrixDiag[i]));
    objective_matrix.setFromTriplets(objectiveMatrixTripletList.begin(), objectiveMatrixTripletList.end());

    if (curSolver->solverSettings.useUnconstrainedCG)
    {
        /////////////////////////////////////////////////////////////////////////
        // Conjugate Gradient (does not support constraint yet.)
        if (curSolver->solverSettings.verbose)
            print_status("Unconstrained CG Solver Started.");
        CGSolver.setMaxIterations(curSolver->solverSettings.maxIters);
        CGSolver.setTolerance(curSolver->solverSettings.tolerence);
        CGSolver.compute(objective_matrix);
        if (curSolver->solverSettings.solutionForward)
            curSolver->solverData.oriSolution =
                CGSolver.solveWithGuess(-objectiveVector, curSolver->solverData.oriSolution);
        else
            curSolver->solverData.solution =
                CGSolver.solveWithGuess(-objectiveVector, curSolver->solverData.oriSolution);
        if (curSolver->solverSettings.verbose)
            print_status("Unconstrained CG Solver Done.");
    }
    else
    {
        /////////////////////////////////////////////////////////////////////////
        // OSQP (support constraints but runtime x2~3)
        Eigen::SparseMatrix<double> constraint_matrix(objectiveVector.size(), objectiveVector.size());
        std::vector<Eigen::Triplet<float>> constraints;
        for (unsigned int i = 0; i < objectiveVector.size(); i++)
        {
            constraints.push_back(Eigen::Triplet<float>(i, i, 1.0));
        }
        constraint_matrix.setFromTriplets(constraints.begin(), constraints.end());

        osqp::OsqpInstance instance;
        instance.objective_matrix = objective_matrix;
        instance.objective_vector = objectiveVector;
        instance.constraint_matrix = constraint_matrix;
        instance.lower_bounds.resize(objectiveVector.size());
        for (unsigned int i = 0; i < objectiveVector.size(); i++)
        {
            instance.lower_bounds[i] = curSolver->solverSettings.lowerbound;
        }
        instance.upper_bounds.resize(objectiveVector.size());
        for (unsigned int i = 0; i < objectiveVector.size(); i++)
        {
            instance.upper_bounds[i] = curSolver->solverSettings.upperbound;
        }

        osqp::OsqpSettings settings;
        settings.verbose = false;

        if (curSolver->solverSettings.verbose)
            print_status("OSQP Solver initializing.");

        auto status = osqpSolver.Init(instance, settings, curSolver->solverSettings.MKLorNot);
        assert(status.ok());

        if (curSolver->solverSettings.verbose)
            print_status("OSQP Solver Started.");

        status = osqpSolver.SetPrimalWarmStart(curSolver->solverData.oriSolution);
        assert(status.ok());

        osqp::OsqpExitCode exit_code = osqpSolver.Solve();
        assert(exit_code == osqp::OsqpExitCode::kOptimal);

        // if need to trace the objective, uncomment the line below
        // double optimal_objective = osqpSolver.objective_value();

        if (curSolver->solverSettings.solutionForward)
            curSolver->solverData.oriSolution = osqpSolver.primal_solution();
        else
            curSolver->solverData.solution = osqpSolver.primal_solution();

        if (curSolver->solverSettings.verbose)
            print_status("OSQP Solver Done.");
    }
}


void QPSolverWrapper::solveGuarded(QPSolverWrapper &solver)
{
    auto &data = solver.solverData;
    auto &settings = solver.solverSettings;
    auto &diagnostic = solver.guardDiagnostics;
    const int n = data.objectiveVector.size();
    if (!settings.useUnconstrainedCG || data.oriSolution.size() != n ||
        data.objectiveMatrixDiag.size() != size_t(n) ||
        !data.objectiveVector.allFinite() || !data.oriSolution.allFinite())
        throw std::runtime_error("Invalid QP dimensions, objective, warm start, or solver mode");
    for (const auto &entry : data.objectiveMatrixTripletList)
        if (entry.row() < 0 || entry.col() < 0 || entry.row() >= n || entry.col() >= n ||
            !std::isfinite(entry.value()))
            throw std::runtime_error("Invalid QP matrix entry");
    for (float diagonal : data.objectiveMatrixDiag)
        if (!std::isfinite(diagonal) || diagonal < 0)
            throw std::runtime_error("Invalid QP diagonal");

    // Sum duplicates in double, without changing the stored objective or triplets.
    Eigen::SparseMatrix<double> matrix(n, n);
    // Include diagonals before compression: inserting n absent entries into a compressed
    // FPGA-scale matrix repeatedly shifts its storage and can take quadratic time.
    std::vector<Eigen::Triplet<double>> entries;
    entries.reserve(data.objectiveMatrixTripletList.size() + n);
    for (const auto &entry : data.objectiveMatrixTripletList)
        entries.emplace_back(entry.row(), entry.col(), double(entry.value()));
    for (int i = 0; i < n; ++i) entries.emplace_back(i, i, double(data.objectiveMatrixDiag[i]));
    matrix.setFromTriplets(entries.begin(), entries.end());
    std::vector<Eigen::Triplet<double>>().swap(entries);
    Eigen::VectorXd offSum = Eigen::VectorXd::Zero(n);
    for (int k = 0; k < matrix.outerSize(); ++k)
        for (Eigen::SparseMatrix<double>::InnerIterator it(matrix, k); it; ++it) {
            if (!std::isfinite(it.value())) throw std::runtime_error("Non-finite assembled QP matrix");
            if (it.row() != it.col()) {
                if (it.value() > 0) throw std::runtime_error("QP is not a spring matrix");
                offSum[it.row()] += std::abs(it.value());
            }
        }
    // Builders emit paired symmetric springs. Reject any broken assembly before using CG.
    Eigen::SparseMatrix<double> asymmetry = matrix - Eigen::SparseMatrix<double>(matrix.transpose());
    if (asymmetry.norm() > 1e-12 * std::max(1.0, matrix.norm()))
        throw std::runtime_error("Asymmetric QP spring matrix");
    Eigen::VectorXd objective = data.objectiveVector;
    for (int i = 0; i < n; ++i) {
        const double diagonal = matrix.coeff(i, i);
        const double minimum = offSum[i] + std::max(1e-8, offSum[i] * 1e-7);
        if (diagonal < minimum) {
            const double delta = minimum - diagonal;
            matrix.coeffRef(i, i) = minimum;
            // An added spring to the previous location, not to coordinate zero.
            objective[i] -= delta * data.oriSolution[i];
            ++diagnostic.repairedRows;
            diagnostic.maxDiagonalDelta = std::max(diagnostic.maxDiagonalDelta, delta);
        }
    }
    if (!objective.allFinite()) throw std::runtime_error("Non-finite regularized QP objective");
    auto &cg = solver.CGSolver;
    cg.setMaxIterations(settings.maxIters);
    cg.setTolerance(settings.tolerence);
    cg.compute(matrix);
    Eigen::VectorXd candidate = cg.solveWithGuess(-objective, data.oriSolution);
    diagnostic.iterations = cg.iterations();
    diagnostic.relativeError = cg.error();
    diagnostic.converged = cg.info() == Eigen::Success;
    // Evaluate the objective difference in displacement form to avoid subtracting large energies.
    bool accept = candidate.allFinite();
    if (accept) {
        const Eigen::VectorXd displacement = candidate - data.oriSolution;
        const Eigen::VectorXd gradient = matrix * data.oriSolution + objective;
        const double linear = displacement.dot(gradient);
        const double quadratic = 0.5 * displacement.dot(matrix * displacement);
        const double change = linear + quadratic;
        accept = std::isfinite(change) && change <= 1e-10 * std::max(1.0, std::abs(linear) + std::abs(quadratic));
    }
    if (!accept) {
        diagnostic.rollback = true;
        diagnostic.converged = false;
        candidate = data.oriSolution;
    }
    if (settings.solutionForward) data.oriSolution = candidate;
    else data.solution = candidate;
}
