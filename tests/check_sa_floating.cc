#include "SAPlacer.h"
#include <cassert>
#include <set>
int main() {
    std::vector<std::vector<float>> edges{{0,1,0},{1,0,2},{0,2,0}};
    std::vector<float> weights{10,20,30}, fixedX, fixedY;
    std::vector<std::vector<float>> fixed(3);
    // Non-divisible restart count also exercises the last partial worker batch.
    SAPlacer solver("floating", edges, weights, fixed, fixedX, fixedY, 4, 3, 40, 30, 5, .8, 1000, 2, 3);
    solver.solve();
    assert(solver.getCluster2XY().size()==3);
    std::set<int> members;
    for(const auto &row:solver.getGrid2clusters()) for(const auto &slot:row) for(int id:slot) assert(members.insert(id).second);
    assert(members.size()==3);
    for(const auto &xy:solver.getCluster2XY()) assert(xy.first>=0 && xy.first<3 && xy.second>=0 && xy.second<4);
}
