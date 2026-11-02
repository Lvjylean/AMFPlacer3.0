#include "DevicePlacementPolicy.h"
#include "SAPlacer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void near(double actual, double expected, const char *message)
{
    if (std::fabs(actual - expected) > 0.0001)
        throw std::runtime_error(std::string(message) + ": got " + std::to_string(actual) +
                                 ", expected " + std::to_string(expected));
}

int main()
{
    try
    {
        constexpr int width = 8, height = 16;
        std::vector<std::vector<float>> adj{{0, 3.5f}, {3.5f, 0}};
        std::vector<std::vector<int>> enhanced;
        std::vector<float> weights{10000, 10000}, fixedX, fixedY;
        std::vector<std::vector<float>> terminals(2);
        using Grid = std::vector<std::vector<std::vector<int>>>;
        auto gridFor = [&](const std::vector<std::pair<int, int>> &xy) {
            Grid grid(height, std::vector<std::vector<int>>(width));
            for (size_t i = 0; i < xy.size(); ++i)
                grid.at(xy[i].second).at(xy[i].first).push_back(i);
            return grid;
        };
        // The pre-change constructor remains a legacy-compatible entry point.
        SAPlacer legacyDefault("legacy", adj, enhanced, weights, terminals, fixedX, fixedY,
                               height, width, 959, 150.5f, 5, .32f, 100, 1, 1, false);
        int checks = 0;
        for (const auto *device : {"VCU108", "VU095", "U250"})
        {
            const auto policy = amf_device::resolve({}, device);
            SAPlacer placer(device, adj, enhanced, weights, terminals, fixedX, fixedY,
                            height, width, 959, 150.5f, 5, .32f, 100, 1, 1, false,
                            policy.saLegacyColumnPenalty());
            const bool u250 = policy.kind == amf_device::Kind::U250;
            // Base distance: 3.5 * (2 * 18.8125 + .32 * 59.9375) = 198.8175.
            // The old, unweighted surcharge is 18.8125, only for an endpoint in column 2.
            for (const auto xy : {
                     std::vector<std::pair<int, int>>{{2, 4}, {4, 5}},
                     std::vector<std::pair<int, int>>{{4, 5}, {2, 4}},
                     std::vector<std::pair<int, int>>{{0, 4}, {2, 5}}})
            {
                const auto grid = gridFor(xy);
                near(placer.evaluateClusterPlacement(grid, xy), u250 ? 198.8175 : 217.63,
                     "Endpoint-column score");
                near(placer.incrementalEvaluateClusterPlacement(grid, xy), 198.8175,
                     "Existing greedy score must stay unchanged");
                ++checks;
            }
            // A connection crossing column 2 without an endpoint there never had the surcharge.
            std::vector<std::pair<int, int>> crossing{{1, 4}, {3, 5}};
            near(placer.evaluateClusterPlacement(gridFor(crossing), crossing), 198.8175,
                 "Crossing-column score");
            ++checks;

            // VU095 and its alias must retain the old default over the whole grid.
            // For U250 the fully placed and greedy objectives agree for these distinct bins.
            for (int x = 0; x < width; ++x)
                for (int y = 0; y < height; ++y)
                {
                    std::vector<std::pair<int, int>> xy{{x, y}, {(x + 3) % width, (y + 5) % height}};
                    const auto grid = gridFor(xy);
                    const double reference = u250 ? placer.incrementalEvaluateClusterPlacement(grid, xy)
                                                  : legacyDefault.evaluateClusterPlacement(grid, xy);
                    near(placer.evaluateClusterPlacement(grid, xy), reference, "Grid compatibility");
                    ++checks;
                }

            adj[0][1] = adj[1][0] = .5f;
            std::vector<std::pair<int, int>> xy{{2, 4}, {4, 5}};
            near(placer.evaluateClusterPlacement(gridFor(xy), xy), u250 ? 28.4025 : 47.215,
                 "Legacy surcharge remains unweighted");
            adj[0][1] = adj[1][0] = 0;
            near(placer.evaluateClusterPlacement(gridFor(xy), xy), 0, "No edge has no distance cost");
            adj[0][1] = adj[1][0] = 3.5f;
            checks += 2;

            // Collision and fixed-terminal costs are unaffected by the device switch.
            terminals = {{1}, {0}}; fixedX = {0}; fixedY = {0};
            xy = {{2, 0}, {2, 0}};
            near(placer.evaluateClusterPlacement(gridFor(xy), xy),
                 legacyDefault.evaluateClusterPlacement(gridFor(xy), xy), "Other objective terms");
            terminals = std::vector<std::vector<float>>(2); fixedX.clear(); fixedY.clear();
            ++checks;

            // The DFX physical-grid objective has no legacy column term and stays unchanged.
            std::vector<std::pair<float, float>> points;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    points.emplace_back((x + .5f) * 18.8125f, (y + .5f) * 59.9375f);
            placer.setPhysicalGrid(points, std::vector<bool>(points.size(), true));
            xy = {{2, 4}, {4, 5}};
            near(placer.evaluateClusterPlacement(gridFor(xy), xy), 198.8175, "Physical-grid compatibility");
            ++checks;
        }
        std::cout << checks << " SA device-policy cases passed (VCU108, VU095, U250)\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
