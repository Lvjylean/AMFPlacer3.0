#ifndef AMF_SLL_QUANTITY_OPTIMIZER_H
#define AMF_SLL_QUANTITY_OPTIMIZER_H
#include <map>
#include <memory>
#include <string>
class PlacementInfo;
class QPSolverWrapper;
// Optional GP-only adapter. No state or coordinate changes when quantity is off
// and region policy is preserve. Original placement/routing objects are untouched.
class SllQuantityOptimizer {
 public:
    SllQuantityOptimizer(PlacementInfo *, const std::map<std::string,std::string> &);
    ~SllQuantityOptimizer();
    void reload();
    void record(const char *phase, int iteration, const char *step);
    void endPhase();
    bool prepare(QPSolverWrapper &, QPSolverWrapper &);
    void accept(QPSolverWrapper &, QPSolverWrapper &, float displacementLimit);
 private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
#endif
