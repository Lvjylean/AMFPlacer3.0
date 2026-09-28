#include "SAPlacer.h"
#include <cassert>
#include <limits>

int main()
{
    std::map<std::string, std::string> cfg;
    for (float inherited : {.4f, 1.f, .72f})
        assert(SAPlacer::configuredY2XRatio(cfg, inherited) == static_cast<float>(inherited * .8));
    cfg["Simulated Annealing y2xRatio"] = "0.7";
    const float eta = SAPlacer::configuredY2XRatio(cfg, .4f);
    assert(std::fabs(eta - .7f) < 1e-7);
    for (const auto &bad : {"0", "-1", "nan", "inf", "0.7ns", "", "1e999"})
    {
        cfg["Simulated Annealing y2xRatio"] = bad;
        bool rejected = false;
        try { SAPlacer::configuredY2XRatio(cfg, .4f); }
        catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
    std::vector<std::vector<float>> edges{{0,1},{1,0}}, fixed(2);
    std::vector<float> weights{11000,11000}, fx, fy;
    SAPlacer sa("ratio-test", edges, weights, fixed, fx, fy, 16, 8, 959, 316, 5, eta);
    auto check = [&](std::vector<std::pair<int,int>> xy, double expected) {
        std::vector<std::vector<std::vector<int>>> grid(16, std::vector<std::vector<int>>(8));
        for (unsigned i=0; i<xy.size(); ++i) grid[xy[i].second][xy[i].first].push_back(i);
        const double full=sa.evaluateClusterPlacement(grid, xy);
        assert(std::fabs(full-expected) < 1e-6 * std::max(1.0, expected));
        assert(std::fabs(full-sa.incrementalEvaluateClusterPlacement(grid, xy)) < 1e-6 * std::max(1.0, expected));
    };
    check({{0,0},{1,0}}, 39.5);
    check({{0,0},{0,1}}, eta * 59.9375);
    check({{0,0},{0,0}}, 6050 * (39.5 + eta * 59.9375));
    // End-to-end SA on the U250 grid: the cheapest orientation changes when
    // eta crosses 39.5/59.9375. Exercise actual optimization, not just parsing.
    for (float ratio : {.32f, eta})
    {
        SAPlacer solve("ratio-solve", edges, weights, fixed, fx, fy, 16, 8, 959, 316,
                       5, ratio, 10000, 1, 2);
        solve.solve();
        const auto xy = solve.getCluster2XY();
        const double cost = solve.evaluateClusterPlacement(solve.getGrid2clusters(), xy);
        assert(std::fabs(cost-std::min(39.5, ratio*59.9375)) < 1e-4);
    }
    std::cout << "SA ratio: legacy fallback, explicit value, invalid values, full/incremental objective, optimizer PASS\n";
}
