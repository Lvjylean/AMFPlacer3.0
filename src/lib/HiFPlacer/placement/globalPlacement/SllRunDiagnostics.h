#pragma once
#include <map>
#include <memory>
#include <string>
class PlacementInfo;
class QPSolverWrapper;
// Diagnostics never write placement, solver caches, anchors or QP matrices.
// The only optimization mutation is optional first-order addition to b_y.
class SllRunDiagnostics {
public:
    SllRunDiagnostics(PlacementInfo*, const std::map<std::string,std::string>&, double);
    ~SllRunDiagnostics();
    void reload();
    void stage(const char*, int, int, const char*);
    void endPhase();
    void beginModelAudit(QPSolverWrapper&, QPSolverWrapper&, float, float);
    bool auditTarget(int) const;
    void auditTimingAnchor(int,bool,const QPSolverWrapper&,const QPSolverWrapper&);
    void auditCheckpoint(const char*, const QPSolverWrapper&, const QPSolverWrapper&);
    void prepare(QPSolverWrapper&, QPSolverWrapper&, float, float);
    void candidate(QPSolverWrapper&, QPSolverWrapper&);
    void written(QPSolverWrapper&, QPSolverWrapper&);
private:
    struct Impl; std::unique_ptr<Impl> p;
};
