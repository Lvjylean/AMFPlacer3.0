#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace shared_residual {
constexpr double tolerance = 1e-6;
struct History {
    unsigned writes = 0;
    bool hard = false;
};
inline History afterWrite(unsigned previous, bool overflow, double before, double after) {
    if (!overflow || !previous) return {};
    return {previous, previous >= 2 || after + tolerance >= before};
}

// One label per bin, one parent edge per bin. A receiver owns a single balance,
// even when several sources reach it through different branches.
struct Bin {
    double margin = 0;
    double movable = 0;
    bool open = true;
    int die = 0;
    double overflow = 0;
};
struct Forest {
    std::vector<int> parent, receiver, order;
};
inline Forest receivers(const std::vector<Bin>& bins, int width) {
    int count = int(bins.size());
    Forest f{std::vector<int>(count, -1), std::vector<int>(count, -1), {}};
    f.order.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (bins[i].open && bins[i].margin > tolerance) {
            f.receiver[i] = i;
            f.order.push_back(i);
        }
    }
    for (size_t k = 0; k < f.order.size(); ++k) {
        int i = f.order[k];
        std::array<int,4> neighbors{{i % width ? i-1 : -1,
                                    i % width+1 < width ? i+1 : -1, i-width, i+width}};
        for (int j : neighbors) {
            if (j < 0 || j >= count || !bins[j].open || bins[j].die != bins[i].die ||
                bins[j].movable <= tolerance || f.receiver[j] >= 0) continue;
            f.parent[j] = i;
            f.receiver[j] = f.receiver[i];
            f.order.push_back(j);
        }
    }
    return f;
}
struct Allocation {
    std::vector<double> target;
    double relieved = 0;
};
inline Allocation allocate(const std::vector<Bin>& bins, int width) {
    if (width <= 0 || bins.size() % width != 0)
        throw std::invalid_argument("Invalid residual rectangle dimensions");
    Allocation result;
    for (const Bin &bin : bins)
        result.target.push_back(bin.movable);

    // Every open same-die component shares all of its positive room. Fixed
    // demand stays in place. Each donor and receiver is charged exactly once.
    std::vector<bool> visited(bins.size());
    std::vector<int> component;
    for (int first = 0; first < int(bins.size()); ++first) {
        if (visited[first] || !bins[first].open) continue;
        component.assign(1, first);
        visited[first] = true;
        double excess = 0, room = 0;
        for (size_t next = 0; next < component.size(); ++next) {
            const int i = component[next];
            excess += std::min(bins[i].overflow, bins[i].movable);
            room += std::max(0.0, bins[i].margin);
            for (int j : {i % width ? i - 1 : -1, i % width + 1 < width ? i + 1 : -1,
                          i - width, i + width}) {
                if (j >= 0 && j < int(bins.size()) && !visited[j] &&
                    bins[j].open && bins[j].die == bins[first].die) {
                    visited[j] = true;
                    component.push_back(j);
                }
            }
        }
        const double transfer = std::min(excess, room);
        if (transfer <= tolerance) continue;
        const double donorFraction = transfer / excess;
        const double receiverFraction = transfer / room;
        for (int i : component) {
            const Bin &bin = bins[i];
            result.target[i] -= donorFraction * std::min(bin.overflow, bin.movable);
            result.target[i] += receiverFraction * std::max(0.0, bin.margin);
        }
        result.relieved += transfer;
    }
    return result;
}
}
