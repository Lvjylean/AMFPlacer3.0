#include "MinCostBipartiteMatcher.h"
#include "SparseMinCostMatching.h"
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

using amf_matching::Adjacency;
static void require(bool value, const std::string &message)
{
    if (!value) throw std::runtime_error(message);
}

static std::pair<int, double> enumerate(const Adjacency &adj, int rights, int target)
{
    std::pair<int, double> best{-1, 0};
    std::vector<bool> used(rights, false);
    std::function<void(int, int, double)> visit = [&](int left, int cardinality, double cost) {
        if (left == static_cast<int>(adj.size()) || cardinality == target)
        {
            if (cardinality > best.first || (cardinality == best.first && cost < best.second))
                best = {cardinality, cost};
            return;
        }
        visit(left + 1, cardinality, cost);
        for (auto edge : adj[left]) if (!used[edge.first])
        {
            used[edge.first] = true;
            visit(left + 1, cardinality + 1, cost + edge.second);
            used[edge.first] = false;
        }
    };
    visit(0, 0, 0);
    return best;
}

static void compareOracle(const Adjacency &adj, int rights, int target)
{
    auto expected = enumerate(adj, rights, target);
    for (int kernel : {0, 1, 2})
    {
        auto actual = amf_matching::solve(adj, rights, target, 1, kernel == 1, kernel == 2);
        require(actual.cardinality == expected.first, "cardinality differs from exhaustive enumeration");
        require(std::abs(actual.cost - expected.second) <= 1e-9 * (1 + std::abs(expected.second)),
                "cost differs from exhaustive enumeration");
        auto biased = amf_matching::solve(adj, rights, target, 1, kernel == 1, kernel == 2, 0.01);
        require(biased.cardinality == expected.first, "forward bias changed maximum cardinality");
        // The biased heuristic need not minimize original costs. This small-graph
        // bound allows the residual path penalties, while catching gross errors.
        require(biased.cost + 1e-9 * (1 + std::abs(expected.second)) >= expected.second &&
                biased.cost <= expected.second + 0.01 * (3 * adj.size() + 1) +
                               1e-9 * (1 + std::abs(expected.second)),
                "biased matching cost outside bounded residual perturbation");
    }
}

static Adjacency synthetic(const std::string &shape, int count, int &rights)
{
    Adjacency adj(count);
    if (shape == "disconnected")
    {
        rights = ((count + 3) / 4) * 6;
        for (int left = 0; left < count; ++left)
            for (int site = 0; site < 6; ++site)
                adj[left].emplace_back((left / 4) * 6 + site, 1.f + ((left * 17 + site * 13) % 97));
    }
    else if (shape == "connected")
    {
        rights = count + 8;
        for (int left = 0; left < count; ++left)
            for (int site = 0; site < 9; ++site)
                adj[left].emplace_back(left + site, 1.f + ((left * 17 + site * 13) % 97));
    }
    else throw std::invalid_argument("unknown synthetic graph shape");
    return adj;
}

static void benchmark(Adjacency &adj, int rights, int target, const std::string &backend, int threads,
                      double forwardBias = 0)
{
    const auto start = amf_matching::Clock::now();
    MinCostBipartiteMatcher matcher(adj.size(), rights, target, adj, threads, false, backend, forwardBias);
    const double buildSeconds = amf_matching::seconds(start);
    matcher.solve();
    const double totalSeconds = amf_matching::seconds(start);
    std::vector<bool> used(rights, false);
    int count = 0;
    double cost = 0;
    std::uint64_t hash = 1469598103934665603ULL;
    for (int left = 0; left < static_cast<int>(adj.size()); ++left)
    {
        const int right = matcher.getMatchedRightNode(left);
        hash = (hash ^ static_cast<std::uint64_t>(right + 1)) * 1099511628211ULL;
        if (right < 0) continue;
        require(right < rights && !used[right], "duplicate/invalid matched site");
        used[right] = true;
        double edgeCost = std::numeric_limits<double>::infinity();
        for (auto edge : adj[left]) if (edge.first == right) edgeCost = std::min(edgeCost, double(edge.second));
        require(std::isfinite(edgeCost), "assignment not present in input graph");
        cost += edgeCost;
        ++count;
    }
    require(count <= target, "exceeded requested matching count");
    std::cout << std::setprecision(15) << "{\"backend\":\"" << backend << "\",\"left\":" << adj.size()
              << ",\"right\":" << rights << ",\"threads\":" << threads << ",\"matched\":" << count
              << ",\"forward_bias\":" << forwardBias << ",\"cost\":" << cost << ",\"build_seconds\":" << buildSeconds
              << ",\"total_seconds\":" << totalSeconds << ",\"assignment_hash\":\"" << hash << "\"}\n";
}

int main(int argc, char **argv)
{
    try
    {
        if ((argc == 6 || argc == 7) && std::string(argv[1]) == "--benchmark")
        {
            int rights;
            auto adj = synthetic(argv[2], std::stoi(argv[3]), rights);
            benchmark(adj, rights, adj.size(), argv[4], std::stoi(argv[5]), argc == 7 ? std::stod(argv[6]) : 0);
            return 0;
        }
        if ((argc == 5 || argc == 6) && std::string(argv[1]) == "--replay")
        {
            std::ifstream input(argv[2]);
            std::string magic;
            int lefts, rights, target;
            require(bool(input >> magic >> lefts >> rights >> target) && magic == "AMF_MATCHING_V1" && lefts >= 0,
                    "invalid matching snapshot header");
            Adjacency adj(lefts);
            int left, right;
            float cost;
            while (input >> left >> right >> cost)
            {
                require(left >= 0 && left < lefts, "invalid snapshot left endpoint");
                adj[left].emplace_back(right, cost);
            }
            require(input.eof(), "malformed snapshot edge");
            benchmark(adj, rights, target, argv[3], std::stoi(argv[4]), argc == 6 ? std::stod(argv[5]) : 0);
            return 0;
        }
        require(argc == 1, "usage: checkBipartiteMatching [--benchmark shape count backend threads [bias] | --replay file backend threads [bias]]");
        for (int kernel : {0, 1, 2})
        {
            // A constant shift of all candidate costs would still swap these
            // rows. c+.01 / -c must retain the diagonal instead (gain < .01).
            Adjacency nearTie{{{0, 1}, {1, 1.001f}}, {{0, 1}, {1, 1.009f}}};
            auto unbiased = amf_matching::solve(nearTie, 2, 2, 1, kernel == 1, kernel == 2, 0);
            auto biased = amf_matching::solve(nearTie, 2, 2, 1, kernel == 1, kernel == 2, 0.01);
            require(unbiased.leftToRight == std::vector<int>({1, 0}) &&
                    biased.leftToRight == std::vector<int>({0, 1}),
                    "asymmetric perturbation did not penalize reversal");
            nearTie[1][1].second = 1.02f;
            biased = amf_matching::solve(nearTie, 2, 2, 1, kernel == 1, kernel == 2, 0.01);
            require(biased.leftToRight == std::vector<int>({1, 0}),
                    "forward bias blocked a worthwhile reversal");
            Adjacency chain{{{0, 1}, {1, 1.001f}}, {{1, 1}, {2, 1.001f}}, {{0, 1}, {2, 1.015f}}};
            biased = amf_matching::solve(chain, 3, 3, 1, kernel == 1, kernel == 2, 0.01);
            require(biased.leftToRight == std::vector<int>({0, 1, 2}),
                    "two-reversal path did not accumulate two forward penalties");
        }
        compareOracle({}, 0, 0);
        compareOracle(Adjacency(4), 0, 4);
        compareOracle({{{0, 1}}, {{0, 1}}}, 1, 2);
        compareOracle({{{0, 0}, {1, 100}}, {{0, 1}}}, 2, 2); // Cardinality takes precedence.
        compareOracle({{{0, 1}, {1, 1.001f}}, {{0, 1}, {1, 1.009f}}}, 2, 2); // Requires reversal.
        compareOracle({{{0, 1}, {0, 0.5f}, {1, 0.75f}}, {{1, 0.25f}}}, 3, 2);
        compareOracle({{{0, 1e30f}, {1, 2e30f}}, {{0, 2e30f}, {1, 3e30f}}}, 2, 2);
        compareOracle({{{0, 1.00000012f}, {1, 1.f}}, {{0, 1.f}, {1, 1.f}}}, 2, 2);
        int cases = 8;
        for (int mask = 0; mask < (1 << 9); ++mask)
            for (int pattern = 0; pattern < 2; ++pattern)
            {
                Adjacency adj(3);
                for (int left = 0; left < 3; ++left)
                    for (int right = 0; right < 3; ++right)
                        if (mask & (1 << (left * 3 + right)))
                            adj[left].emplace_back(right, pattern ? float((left * 7 + right * 11) % 13) / 8 : 1.f);
                for (int target = 0; target <= 3; ++target) { compareOracle(adj, 3, target); ++cases; }
            }
        std::mt19937 random(20260929);
        for (int test = 0; test < 1000; ++test)
        {
            const int lefts = 1 + random() % 7, rights = random() % 8;
            Adjacency adj(lefts);
            for (int left = 0; left < lefts; ++left)
                for (int right = 0; right < rights; ++right)
                    if (random() % 3 != 0) adj[left].emplace_back(right, float(random() % 300) / 16);
            compareOracle(adj, rights, random() % (lefts + 1));
            compareOracle(adj, rights, lefts);
            cases += 2;
        }
        for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            bool rejected = false;
            try { amf_matching::solve({{{0, invalid}}}, 1, 1, 1); }
            catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "invalid edge cost accepted");
            rejected = false;
            try { amf_matching::solve({{{0, 1}}}, 1, 1, 1, true, true, invalid); }
            catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "invalid forward bias accepted");
        }
        int rights;
        auto separated = synthetic("disconnected", 2000, rights);
        for (int kernel : {0, 1, 2})
        {
            auto serial = amf_matching::solve(separated, rights, separated.size(), 1, kernel == 1, kernel == 2);
            auto parallel = amf_matching::solve(separated, rights, separated.size(), 8, kernel == 1, kernel == 2);
            require(serial.leftToRight == parallel.leftToRight && serial.cost == parallel.cost,
                    "component scheduling changes the matching");
            require(parallel.components == 500 && parallel.largestLeft == 4, "component decomposition mismatch");
            auto biasedSerial = amf_matching::solve(separated, rights, separated.size(), 1, kernel == 1, kernel == 2, 0.01);
            auto biasedParallel = amf_matching::solve(separated, rights, separated.size(), 8, kernel == 1, kernel == 2, 0.01);
            require(biasedSerial.leftToRight == biasedParallel.leftToRight && biasedSerial.cost == biasedParallel.cost,
                    "component scheduling changes biased matching");
        }
        std::cout << "PASS: " << cases << " oracle/cardinality cases for all three kernels with bias 0 and .01, asymmetric reversal fixtures, invalid inputs, and serial/parallel determinism\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
