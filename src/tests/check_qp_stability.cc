#include "QPSolverWrapper.h"
#include "TimingWeightGuard.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <thread>

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static void spring(QPSolverWrapper &s, float weight, float anchor) {
    s.solverData.objectiveMatrixTripletList = {{0,1,-weight},{1,0,-weight}};
    s.solverData.objectiveMatrixDiag = {weight+anchor,weight+anchor};
    s.solverData.oriSolution << 0,10;
    s.solverData.objectiveVector << 0,-10*anchor;
    s.solverSettings.stabilityGuard = true;
}
static void solve(QPSolverWrapper &s) {
    auto ptr = &s;
    std::thread worker(QPSolverWrapper::QPSolve, std::ref(ptr));
    worker.join();
}
int main() {
    try {
        using TimingWeightGuard::enhancement;
        for (float slack : {0.f,-.1f,-1.f,-9.f,-10.1f,-15.f}) {
            float legacy=enhancement(slack,10,1.1,-10,0);
            float bounded=enhancement(slack,10,1.1,-10,1000);
            require(std::abs(legacy-bounded) <= 2e-6*std::max(1.f,legacy), "ordinary weights changed");
        }
        float observed=enhancement(-108.598,10,1.1,-10,0);
        require(std::isfinite(observed) && observed>1e38, "failed-run weight reproduction");
        require(enhancement(-108.598,10,1.1,-10,1000)==1000, "observed slack cap");
        require(enhancement(-1000,10,1.1,-10,1000)==1000, "overflow prevention");
        require(enhancement(-.01,10,1.1,0,1000)==1000, "zero threshold protection");
        require(!std::isfinite(enhancement(-1,0,1.1,-10,1000)), "invalid clock accepted");
        float previous=0;
        for (int i=0;i<300;++i) {
            float value=enhancement(-float(i),10,1.1,-10,1000);
            require(std::isfinite(value) && value>=previous && value<=1000, "monotonic bounded weights");
            previous=value;
        }
        for (const auto &text : {"nan","inf","-1","0.5","1000001","1000x"}) {
            bool rejected=false;
            try { TimingWeightGuard::parseCap(text); } catch (const std::exception &) { rejected=true; }
            require(rejected, "invalid cap accepted");
        }
        require(TimingWeightGuard::parseCap("0")==0 && TimingWeightGuard::parseCap("1000")==1000,"cap config");

        QPSolverWrapper normal(true,false,0,20,2,false), old(true,false,0,20,2,false);
        spring(normal,2,1); spring(old,2,1);old.solverSettings.stabilityGuard=false;
        solve(normal);solve(old);
        require(normal.guardDiagnostics.error.empty() && normal.guardDiagnostics.repairedRows==0 &&
                !normal.guardDiagnostics.rollback, "normal QP unexpectedly changed");
        require((normal.solverData.solution-old.solverData.solution).norm()<1e-12, "normal/legacy QP mismatch");
        require((normal.solverData.solution-Eigen::Vector2d(4,6)).norm()<1e-8,"normal analytical QP");
        for (bool forward : {false,true}) {
            QPSolverWrapper huge(true,false,0,20,2,false);
            spring(huge,1e12,1);huge.solverSettings.solutionForward=forward;
            require(huge.solverData.objectiveMatrixDiag[0]==1e12f,"float anchor-loss fixture");
            solve(huge);
            const auto &x=forward?huge.solverData.oriSolution:huge.solverData.solution;
            require(huge.guardDiagnostics.error.empty() && huge.guardDiagnostics.repairedRows==2 &&
                    !huge.guardDiagnostics.rollback && x.allFinite(),"ill-conditioned QP guard failed");
            require(std::abs(x[0]-x[1])<1e-3 && std::abs(x.mean()-5)<.01,"guarded spring solution");
        }
        QPSolverWrapper bad(true,false,0,20,2,false);
        spring(bad,2,1);bad.solverData.objectiveVector[0]=std::numeric_limits<double>::quiet_NaN();
        solve(bad);require(!bad.guardDiagnostics.error.empty(),"invalid objective escaped worker");
        spring(bad,2,1);bad.solverData.objectiveMatrixDiag[0]=std::numeric_limits<float>::infinity();
        solve(bad);require(!bad.guardDiagnostics.error.empty(),"invalid matrix accepted");
        spring(bad,2,1);bad.solverData.objectiveMatrixTripletList.pop_back();
        solve(bad);require(!bad.guardDiagnostics.error.empty(),"asymmetric matrix accepted");
        // Finite inputs can still overflow CG arithmetic; rollback must keep a finite warm start.
        spring(bad,2,1);bad.solverData.oriSolution << 1e300,-1e300;
        solve(bad);require(bad.guardDiagnostics.error.empty() && bad.guardDiagnostics.rollback &&
                           bad.solverData.solution.allFinite(),"non-finite candidate propagated");
        // FPGA-scale assembly smoke check: every diagonal is initially absent from triplets.
        const int n=100000;
        QPSolverWrapper large(true,false,0,20,n,false);
        large.solverSettings.stabilityGuard=true;
        large.solverData.objectiveMatrixDiag.assign(n,3);
        large.solverData.objectiveMatrixDiag.front()=2;
        large.solverData.objectiveMatrixDiag.back()=2;
        large.solverData.objectiveVector.setConstant(-1);
        large.solverData.oriSolution.setOnes();
        for (int i=1;i<n;++i) {
            large.solverData.objectiveMatrixTripletList.emplace_back(i,i-1,-1);
            large.solverData.objectiveMatrixTripletList.emplace_back(i-1,i,-1);
        }
        solve(large);
        require(large.guardDiagnostics.error.empty() && large.guardDiagnostics.repairedRows==0 &&
                large.solverData.solution.allFinite() &&
                (large.solverData.solution-Eigen::VectorXd::Ones(n)).norm()<1e-8,"large sparse assembly");
        std::cout << "PASS bounded timing weights, legacy equivalence, float anchor repair, worker error, rollback\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';return 1;
    }
}
