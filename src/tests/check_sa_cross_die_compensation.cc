#include "SAPlacer.h"
#include "SACrossDieCompensation.h"
#include <functional>
#include <iostream>
#include <limits>

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
static void near(double a, double b, const char *message)
{
    if (std::fabs(a - b) > 1e-4) throw std::runtime_error(message);
}
static void rejects(const std::function<void()> &f)
{
    bool caught = false;
    try { f(); } catch (const std::invalid_argument &) { caught = true; }
    require(caught, "Expected invalid calibration to be rejected");
}
int main()
{
    try
    {
        using namespace amf_sa_cross_die;
        using amf_device::Kind;
        require(enabled({}, Kind::U250), "U250 candidate default");
        require(!enabled({}, Kind::VU095), "VU095 unchanged");
        require(!enabled({{"SACrossDieCompensationEnable", "false"}}, Kind::U250), "Ablation switch");
        rejects([&] { enabled({{"SACrossDieCompensationEnable", "true"}}, Kind::VU095); });
        rejects([&] { enabled({{"SACrossDieCompensationEnable", "1"}}, Kind::U250); });
        rejects([&] { extraVerticalDistance(0); });
        rejects([&] { extraVerticalDistance(std::numeric_limits<double>::quiet_NaN()); });
        rejects([&] { extraVerticalDistance(200); });
        // Independent inverse-model check, not another copy of the compensation formula.
        const double d = equivalentVerticalDistance;
        const double model = (202.12777470189823 - 131.59702340956898 * std::pow(d, .3) +
                              136.17523346378107 * std::sqrt(d)) / 1000;
        near(model, referenceDelayNs, "Full reference must map to 1.415 ns");
        constexpr int H = 16, W = 8;
        constexpr float pitch = 959.f / H, width = 150.5f / W, ratio = .32f;
        const double extra = extraVerticalDistance(pitch);
        near(pitch + extra, d, "Reference row must be replaced, not counted twice");
        std::vector<int> prefixes;
        for (int y = 0; y < H; ++y) prefixes.push_back(y / 4);
        std::vector<std::vector<float>> adj{{0, 3.5f}, {3.5f, 0}}, terminals(2);
        std::vector<std::vector<int>> enhanced;
        std::vector<float> weights{10000, 10000}, fx, fy;
        SAPlacer base("base", adj, enhanced, weights, terminals, fx, fy, H, W, 959, 150.5f,
                      5, ratio, 100, 1, 1, false, false);
        SAPlacer corrected("corrected", adj, enhanced, weights, terminals, fx, fy, H, W, 959, 150.5f,
                           5, ratio, 100, 1, 1, false, false);
        corrected.setCrossDieCompensation(prefixes, {}, extra);
        using XY = std::vector<std::pair<int,int>>;
        auto gridFor = [&](const XY &xy) {
            std::vector<std::vector<std::vector<int>>> grid(H, std::vector<std::vector<int>>(W));
            for (size_t i = 0; i < xy.size(); ++i)
                if (xy[i].first >= 0 && xy[i].second >= 0)
                    grid.at(xy[i].second).at(xy[i].first).push_back(i);
            return grid;
        };
        int checks = 0;
        for (int a = 0; a < H*W; ++a) for (int b = 0; b < H*W; ++b)
        {
            XY xy{{a%W, a/W}, {b%W, b/W}};
            const auto grid = gridFor(xy);
            // Count actual crossed seam positions independently of prefix subtraction.
            int seams = 0;
            for (int seam : {4, 8, 12})
                seams += (a/W < seam) != (b/W < seam);
            const double increment = 3.5 * ratio * extra * seams;
            near(corrected.evaluateClusterPlacement(grid, xy) - base.evaluateClusterPlacement(grid, xy),
                 increment, "Full-score seam delta / unchanged collision penalty");
            near(corrected.incrementalEvaluateClusterPlacement(grid, xy) - base.incrementalEvaluateClusterPlacement(grid, xy),
                 increment, "Greedy-score seam delta");
            ++checks;
        }
        // Existing X slope is retained while the undesired cross-seam preference flips.
        XY horizontal{{0,3},{2,3}}, cross{{0,3},{0,4}}, diagonal{{0,3},{2,4}};
        const auto score = [&](SAPlacer &p, const XY &xy) { return p.evaluateClusterPlacement(gridFor(xy),xy); };
        require(score(base, cross) < score(base, horizontal), "Baseline comparison fixture");
        require(score(corrected, cross) > score(corrected, horizontal), "Crossing becomes more expensive");
        near(score(corrected, diagonal)-score(corrected, cross), 3.5*2*width, "Horizontal increment retained");
        adj[0][1] = adj[1][0] = 0;
        near(score(corrected, cross), 0, "No edge means no compensation");
        adj[0][1] = adj[1][0] = 3.5f;
        XY partial{{0,3},{-1,-1}};
        near(corrected.incrementalEvaluateClusterPlacement(gridFor(partial),partial), 0, "Unplaced cluster ignored");
        // Fixed endpoints use the same weighted correction and the existing fixed factor.
        terminals = {{2}, {0}};fx = {10};fy = {800};
        corrected.setCrossDieCompensation(prefixes, {3}, extra);
        near(corrected.incrementalEvaluateClusterPlacement(gridFor(partial),partial) -
             base.incrementalEvaluateClusterPlacement(gridFor(partial),partial),
             5*2*ratio*extra*3, "Fixed endpoint cross-seam correction");
        near(score(corrected, diagonal)-score(base, diagonal),
             (3.5+5*2*3)*ratio*extra, "Pair and fixed correction consistently combined");
        rejects([&] { corrected.setCrossDieCompensation({0}, {3}, extra); });
        auto bad = prefixes;bad[2] = -1;
        rejects([&] { corrected.setCrossDieCompensation(bad, {3}, extra); });
        rejects([&] { corrected.setCrossDieCompensation(prefixes, {}, extra); });
        rejects([&] { corrected.setCrossDieCompensation(prefixes, {3}, -1); });
        std::vector<std::pair<float,float>> points(H*W,{0,0});
        rejects([&] { corrected.setPhysicalGrid(points, std::vector<bool>(H*W,true)); });
        base.setPhysicalGrid(points, std::vector<bool>(H*W,true));
        rejects([&] { base.setCrossDieCompensation(prefixes, {3}, extra); });
        std::cout << checks << " grid-pair cases plus calibration, ranking, fixed terminals, partial placement, "
                  << "device isolation and invalid-input checks passed\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
