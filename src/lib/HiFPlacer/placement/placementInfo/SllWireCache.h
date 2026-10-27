#pragma once
#include "SllVirtualModel.h"
#include <climits>

namespace amf_sll_plan {
// Immutable during a candidate-search batch. All pins of a trial PU move by
// the same delta, including multiple pins in the same local shared hyperedge.
struct WireSnapshotPoint {
    Point position, unitOrigin;
    int unit = -1; // -1 is a fixed SLL endpoint, never a trial PU.
};

class WireSnapshotTree {
    struct Group { int unit; Point origin; TileBox bounds; };
    struct TwoMin {
        double first = INFINITY, second = INFINITY;
        int owner = INT_MIN;
        // Each owner is added once, after its pins have been grouped.
        void add(double value, int unit) {
            if (value < first) { second = first; first = value; owner = unit; }
            else if (value < second) second = value;
        }
        double excluding(int unit) const { return owner == unit ? second : first; }
    };
    std::vector<Group> groups;
    TwoMin lowX, highX, lowY, highY;
    TileBox bounds;
    size_t terminals = 0;
public:
    explicit WireSnapshotTree(const std::vector<WireSnapshotPoint>& points) : terminals(points.size()) {
        std::map<int, Group> grouped;
        for (const auto& point : points) {
            auto it = grouped.emplace(point.unit, Group{point.unit, point.unitOrigin, {}}).first;
            it->second.bounds.add(point.position);
            bounds.add(point.position);
        }
        groups.reserve(grouped.size());
        for (const auto& item : grouped) {
            const auto& group = item.second;
            lowX.add(group.bounds.x0, group.unit); highX.add(-group.bounds.x1, group.unit);
            lowY.add(group.bounds.y0, group.unit); highY.add(-group.bounds.y1, group.unit);
            groups.push_back(group);
        }
    }
    double length(double ratio, int trialUnit = -1, Point trial = {}) const {
        if (terminals < 2) return 0;
        const auto baseline = [&] { return bounds.x1 - bounds.x0 + ratio * (bounds.y1 - bounds.y0); };
        if (trialUnit < 0) return baseline();
        auto it = std::lower_bound(groups.begin(), groups.end(), trialUnit,
                                   [](const Group& group, int unit) { return group.unit < unit; });
        if (it == groups.end() || it->unit != trialUnit) return baseline();
        const double dx = trial.x - it->origin.x, dy = trial.y - it->origin.y;
        const double x0 = std::min(lowX.excluding(trialUnit), it->bounds.x0 + dx);
        const double x1 = std::max(-highX.excluding(trialUnit), it->bounds.x1 + dx);
        const double y0 = std::min(lowY.excluding(trialUnit), it->bounds.y0 + dy);
        const double y1 = std::max(-highY.excluding(trialUnit), it->bounds.y1 + dy);
        return x1 - x0 + ratio * (y1 - y0);
    }
};

struct WireSnapshotNet {
    std::vector<WireSnapshotTree> trees;
    double length(double ratio, int trialUnit = -1, Point trial = {}) const {
        double result = 0;
        for (const auto& tree : trees) result += tree.length(ratio, trialUnit, trial);
        return result;
    }
};
}
