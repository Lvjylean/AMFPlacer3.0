#include "BoundaryAwareClusterer.h"
#include "HierarchicalBoundaryPolicy.h"
#include "RegionCapacityTracker.h"
#include "../../../utils/RuntimeProfiler.h"
#include <algorithm>
#include <array>
#include <limits>
#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using PU = PlacementInfo::PlacementUnit;
std::vector<DesignInfo::DesignCell *> cells(PU *pu)
{
    if (auto c = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) return {c->getCell()};
    if (auto m = dynamic_cast<PlacementInfo::PlacementMacro *>(pu)) return m->getCells();
    return {};
}
size_t instanceCount(PU *pu)
{
    size_t count = 0;
    for (auto c : cells(pu)) if (!c->isVirtualCell()) ++count;
    return count;
}
bool movable(PU *pu)
{
    return !pu->isFixed() && !pu->isLocked() && !pu->checkHasDSP() && !pu->checkHasBRAM() &&
           !pu->checkHasURAM() && (pu->checkHasLUT() || pu->checkHasFF() || pu->checkHasCARRY() ||
                                  pu->checkHasLUTRAM() || pu->checkHasMUX());
}
void offsets(PU *pu, float &lx, float &hx, float &ly, float &hy)
{
    lx = hx = ly = hy = 0;
    if (auto m = dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
        for (int i = 0; i < m->getNumOfCells(); ++i)
        {
            float x, y; DesignInfo::DesignCellType type;
            m->getVirtualCellInfo(i, x, y, type);
            lx = std::min(lx, x); hx = std::max(hx, x);
            ly = std::min(ly, y); hy = std::max(hy, y);
        }
}
int integer(std::map<std::string, std::string> &config, const char *key, int fallback)
{
    auto it = config.find(key);
    if (it == config.end()) return fallback;
    size_t parsed = 0; int result = std::stoi(it->second, &parsed);
    if (parsed != it->second.size() || result < 1)
        throw std::runtime_error(std::string("Invalid paper boundary parameter: ") + key);
    return result;
}
}

void BoundaryAwareClusterer::runPaper(bool deferSLRSpreading)
{
    AMF_PROFILE_FUNCTION("boundary_clustering");
    placement->clearRegionPreferences();
    auto graph = placement->getTimingInfo()->getSimplePlacementTimingGraph();
    auto &nodes = graph->getNodes();
    auto &sorted = placement->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    auto &locations = placement->getCellId2location();
    const auto &regions = model->getRegions();
    std::set<int> slrs;
    for (const auto &r : regions) slrs.insert(r.slr);
    placement->setPaperSLRStage(slrs.size() > 1);
    const int threshold = integer(config, "BoundaryPaperPathLengthThreshold", placement->getLongPathThresholdLevel());
    const int maxCells = integer(config, "BoundaryPaperMaxCells", placement->isDensePlacement() ? 2000 : 20000);
    // Retain the upstream long-path seed scope (top 10%). Do not use the old
    // 128-node, delay-ranked segment optimizer or its edge-gain acceptance gate.
    const size_t seeds = std::min(sorted.size(), std::max(size_t(1), (sorted.size() + 9) / 10));
    std::set<int> claimed;
    RegionCapacityTracker capacity(placement);
    std::ofstream report;
    if (!config["BoundaryReportDirectory"].empty())
    {
        report.open(config["BoundaryReportDirectory"] + "/paper_clusters.tsv", std::ios::app);
        if (report.tellp() == 0)
            report << "seed\tcluster\tinstances\tunits\tslr\tslr_votes\tregion\tside_votes\tguide_x\tguide_y\treason\n";
    }
    int accepted = 0, selectedPUs = 0;
    for (size_t s = 0; s < seeds; ++s)
    {
        auto seed = sorted[s];
        if (seed->getLongestPathLength() <= threshold) continue;
        auto seedPU = placement->getPlacementUnitByCellId(seed->getId());
        if (claimed.count(seedPU->getId()) || seed->getForwardLevel() > seed->getLongestPathLength() * 0.2) continue;
        std::vector<int> stack{seed->getId()};
        std::set<int> seen;
        std::map<int, PU *> units;
        size_t count = 0;
        while (!stack.empty())
        {
            int id = stack.back(); stack.pop_back();
            if (!seen.insert(id).second) continue;
            auto pu = placement->getPlacementUnitByCellId(id);
            if (claimed.count(pu->getId())) continue;
            if (!units.count(pu->getId()))
            {
                size_t n = instanceCount(pu);
                if (count + n > size_t(maxCells)) continue; // Never split a macro to meet the limit.
                units[pu->getId()] = pu; count += n;
            }
            // The original implementation traverses both directions through
            // unregistered long-path logic. Respect claimed units and size here.
            auto visit = [&](Node *n) {
                if (!n->checkIsRegister() && n->getLongestPathLength() > threshold && !seen.count(n->getId()))
                    stack.push_back(n->getId());
            };
            for (auto e : nodes[id]->getOutEdges()) visit(e->getSink());
            for (auto e : nodes[id]->getInEdges()) visit(e->getSource());
        }
        if (count < size_t(threshold)) continue;
        std::map<int, double> votes;
        for (auto entry : units)
            for (auto c : cells(entry.second))
                if (!c->isVirtualCell())
                {
                    auto loc = locations.at(c->getCellId());
                    ++votes[model->regionAt(loc.X, loc.Y)];
                }
        auto choice = hierarchical_boundary::select(regions, votes);
        size_t sideCount = 0;
        for (const auto &r : regions) if (r.slr == choice.slr) ++sideCount;
        bool guideX = choice.region >= 0 && sideCount > 1;
        bool guideY = slrs.size() > 1;
        std::string reason = "no-slr-majority";
        std::map<PU *, int> targets;
        std::map<PU *, PlacementInfo::RegionPreference> preferences;
        bool valid = choice.slr >= 0 && (guideX || guideY);
        if (valid)
        {
            reason = "no-movable-units";
            for (auto entry : units)
            {
                auto pu = entry.second;
                if (!movable(pu)) continue;
                int target = choice.region;
                if (target < 0)
                {
                    // SLR-only decision: retain each PU's horizontal side. It
                    // receives no X anchor even though capacity is kept by side.
                    for (const auto &r : regions)
                        if (r.slr == choice.slr && pu->X() >= r.x0 && pu->X() <= r.x1)
                        { target = r.id; break; }
                }
                PlacementInfo::RegionPreference pref{target, accepted, 1.0f, guideX, guideY};
                float x, y;
                if (!placement->regionAnchor(pu, pref, x, y))
                { valid = false; reason = "macro-does-not-fit-target"; break; }
                const auto &selected = regions[target];
                float slrLeft = selected.x0, slrRight = selected.x1;
                for (const auto &side : regions) if (side.slr == selected.slr)
                { slrLeft = std::min(slrLeft, side.x0); slrRight = std::max(slrRight, side.x1); }
                // HPIO keeps the existing outer-region, one-sided rule. SLRs
                // use the upstream outer/middle cases rotated from X to Y.
                if (guideX) pref.pullX = (selected.x0 + selected.x1 < slrLeft + slrRight) ? -1 : 1;
                if (guideY) pref.pullY = hierarchical_boundary::slrPullDirection(regions, target);
                preferences[pu] = pref;
                targets[pu] = target;
            }
            if (valid && !targets.empty()) valid = capacity.assignTargets(targets, true, &reason);
            else valid = false;
        }
        if (valid)
        {
            for (auto entry : preferences)
            {
                placement->getRegionPreferences()[entry.first] = entry.second;
                claimed.insert(entry.first->getId());
            }
            selectedPUs += preferences.size();
            reason = guideX ? "accepted-slr-and-side" : "accepted-slr-only";
            ++accepted;
        }
        if (report)
            report << seed->getId() << '\t' << (valid ? accepted - 1 : -1) << '\t' << count << '\t'
                   << preferences.size() << '\t' << choice.slr << '\t' << choice.slrVotes << '\t'
                   << choice.region << '\t' << choice.sideVotes << '\t' << guideX << '\t' << guideY << '\t' << reason << '\n';
    }
    // The first generation keeps its original cluster selection, but must
    // not alter X coordinates before fixedCLB initialization completes.
    if (deferSLRSpreading && placement->paperSLRStage())
        print_info("Paper boundary SLR X spreading deferred until after fixedCLB initialization");
    else if (placement->paperSLRStage())
        spreadPaperSLR();
    else
        spreadPaper(false);
    print_info("Paper boundary clustering accepted " + std::to_string(accepted) + " clusters / " +
               std::to_string(selectedPUs) + " PUs; path threshold=" + std::to_string(threshold) +
               " cell limit=" + std::to_string(maxCells));
}

void BoundaryAwareClusterer::spreadPaperSLR()
{
    if (!placement->paperBoundaryClusteringEnabled() || !placement->paperSLRStage()) return;
    print_info("MERGED_PREP 1 slr_x_room");
    spreadPaper(true);
    advancePaperStage();
}

void BoundaryAwareClusterer::advancePaperStage()
{
    if (!placement->paperBoundaryClusteringEnabled() || !placement->paperSLRStage()) return;
    // Preparation order: SLR X room, SLR clip, HPIO Y room, HPIO clip.
    // There are no intervening QP solves or ordinary density-spreading rounds.
    print_info("MERGED_PREP 2 slr_clip");
    placement->clipPaperRegionLocations();
    placement->setPaperSLRStage(false);
    placement->refreshRegionPreferences();
    print_info("MERGED_PREP 3 hpio_y_room");
    spreadPaper(false);
    print_info("MERGED_PREP 4 hpio_clip");
    placement->clipPaperRegionLocations();
    print_info("Paper boundary phase: merged XY guidance prepared");
}

void BoundaryAwareClusterer::spreadPaper(bool slrStage)
{
    AMF_PROFILE_FUNCTION("boundary_spreading");
    const auto &regions = model->getRegions();
    const auto &preferences = placement->getRegionPreferences();

    // BEGIN SLR_ROOM_MOVEMENT_AUDIT: observation only, before room-making.
    struct SLRRoomBefore { PU *pu; float x, y; int slr; };
    std::vector<SLRRoomBefore> slrRoomBefore;
    static unsigned long long slrRoomCallCounter = 0;
    unsigned long long slrRoomCall = 0;
    if (slrStage)
    {
        slrRoomCall = ++slrRoomCallCounter;
        slrRoomBefore.reserve(placement->getPlacementUnits().size());
        for (auto pu : placement->getPlacementUnits())
            slrRoomBefore.push_back({pu, pu->X(), pu->Y(),
                regions.at(model->regionAt(pu->X(), pu->Y())).slr});
    }
    // END SLR_ROOM_MOVEMENT_AUDIT
    // SLR phase: whole-die X expansion, with no HPIO-side attraction/fence yet.
    // HPIO phase: selected-side Y expansion, bounded by that die's Y limits.
    std::map<int, PhysicalBoundaryModel::Region> boxes;
    for (const auto &r : regions)
    {
        int key = slrStage ? r.slr : r.id;
        auto inserted = boxes.emplace(key, r);
        if (!inserted.second)
        {
            auto &box = inserted.first->second;
            box.x0 = std::min(box.x0, r.x0); box.x1 = std::max(box.x1, r.x1);
        }
    }
    std::map<int, double> incoming;
    for (auto entry : preferences)
    {
        auto pu = entry.first; const auto &pref = entry.second;
        const auto &target = regions.at(pref.region);
        for (auto c : cells(pu))
        {
            if (c->isVirtualCell()) continue;
            auto loc = placement->getCellId2location().at(c->getCellId());
            int from = model->regionAt(loc.X, loc.Y);
            if (slrStage && pref.guideY && regions.at(from).slr != target.slr) ++incoming[target.slr];
            if (!slrStage && pref.guideX && (loc.X < target.x0 || loc.X > target.x1)) ++incoming[target.id];
        }
    }
    std::ofstream report;
    if (!config["BoundaryReportDirectory"].empty())
    {
        report.open(config["BoundaryReportDirectory"] + "/paper_spreading.tsv", std::ios::app);
        if (report.tellp() == 0)
            report << "stage\tgroup\taxis\tincoming\tinside\told_low\told_high\tnew_low\tnew_high\tmoved_pus\n";
    }
    for (const auto &group : boxes)
    {
        const int key = group.first;
        const auto &r = group.second;
        if (incoming[key] <= 0) continue;
        std::vector<PU *> residents;
        double inside = 0;
        float low = std::numeric_limits<float>::max(), high = -low;
        for (auto pu : placement->getPlacementUnits())
        {
            const auto &current = regions.at(model->regionAt(pu->X(), pu->Y()));
            if ((slrStage ? current.slr : current.id) != key) continue;
            auto pref = preferences.find(pu);
            if (pref != preferences.end())
            {
                const auto &target = regions.at(pref->second.region);
                if (target.slr != r.slr || (!slrStage && pref->second.guideX && target.id != r.id)) continue;
            }
            inside += instanceCount(pu);
            if (!movable(pu)) continue;
            float lx, hx, ly, hy; offsets(pu, lx, hx, ly, hy);
            if (pu->X() + lx < r.x0 + 0.25f || pu->X() + hx > r.x1 - 0.25f ||
                pu->Y() + ly < r.y0 + 0.25f || pu->Y() + hy > r.y1 - 0.25f) continue;
            float coord = slrStage ? pu->X() : pu->Y();
            low = std::min(low, coord); high = std::max(high, coord);
            residents.push_back(pu);
        }
        if (residents.empty() || high - low < 0.01f || inside <= 0) continue;
        float lower = (slrStage ? r.x0 : r.y0) + 0.25f;
        float upper = (slrStage ? r.x1 : r.y1) - 0.25f;
        auto range = hierarchical_boundary::expandedRange(low, high, lower, upper, incoming[key], inside);
        int moved = 0;
        for (auto pu : residents)
        {
            float lx, hx, ly, hy; offsets(pu, lx, hx, ly, hy);
            float x = pu->X(), y = pu->Y();
            float value = range.first + ((slrStage ? x : y) - low) * (range.second - range.first) / (high - low);
            if (slrStage) x = std::max(r.x0 - lx + 0.25f, std::min(r.x1 - hx - 0.25f, value));
            else y = std::max(r.y0 - ly + 0.25f, std::min(r.y1 - hy - 0.25f, value));
            placement->legalizeXYInArea(pu, x, y);
            if (x + lx < r.x0 || x + hx > r.x1 || y + ly < r.y0 || y + hy > r.y1) continue;
            if (std::fabs(x - pu->X()) + std::fabs(y - pu->Y()) > 1e-5f)
            {
                pu->setAnchorLocationAndForgetTheOriginalOne(x, y);
                placement->clipPaperRegionLocation(pu);
                ++moved;
            }
        }
        if (report) report << (slrStage ? "slr" : "hpio") << '\t' << key << '\t' << (slrStage ? 'X' : 'Y') << '\t'
                           << incoming[key] << '\t' << inside << '\t' << low << '\t' << high << '\t'
                           << range.first << '\t' << range.second << '\t' << moved << '\n';
    }

    // BEGIN SLR_ROOM_MOVEMENT_AUDIT: measure actual locations AFTER internal clipping.
    if (slrStage)
    {
        struct Movement
        {
            size_t movedPUs = 0, movedInstances = 0;
            double absDx = 0, absDy = 0, maxDx = 0, maxDy = 0;
        };
        std::map<int, Movement> movement;
        for (const auto &before : slrRoomBefore)
        {
            const double dx = std::fabs(double(before.pu->X()) - before.x);
            const double dy = std::fabs(double(before.pu->Y()) - before.y);
            if (dx + dy <= 1e-5) continue;
            auto &m = movement[before.slr];
            ++m.movedPUs;
            m.movedInstances += instanceCount(before.pu);
            m.absDx += dx; m.absDy += dy;
            m.maxDx = std::max(m.maxDx, dx); m.maxDy = std::max(m.maxDy, dy);
        }
        std::ofstream movementReport;
        if (!config["BoundaryReportDirectory"].empty())
        {
            movementReport.open(config["BoundaryReportDirectory"] +
                                "/slr_x_room_movement.tsv", std::ios::app);
            if (movementReport.tellp() == 0)
                movementReport << "call\tslr\tincoming_instances\tmoved_pus\tmoved_real_instances"
                               << "\tsum_abs_dx\tmean_abs_dx_moved_pu\tmax_abs_dx"
                               << "\tsum_abs_dy\tmax_abs_dy\tstatus\n";
            movementReport.precision(10);
        }
        for (const auto &group : boxes)
        {
            const int slr = group.first;
            const auto &m = movement[slr];
            const char *status = incoming[slr] <= 0 ? "no_incoming" :
                                 (m.movedPUs ? "moved" : "no_actual_movement");
            if (movementReport)
                movementReport << slrRoomCall << '\t' << slr << '\t' << incoming[slr] << '\t'
                               << m.movedPUs << '\t' << m.movedInstances << '\t'
                               << m.absDx << '\t' << (m.movedPUs ? m.absDx / m.movedPUs : 0) << '\t'
                               << m.maxDx << '\t' << m.absDy << '\t' << m.maxDy << '\t' << status << '\n';
            print_info("SLR_X_ROOM_MOVEMENT call=" + std::to_string(slrRoomCall) +
                       " slr=" + std::to_string(slr) + " incoming=" + std::to_string(incoming[slr]) +
                       " moved_pus=" + std::to_string(m.movedPUs) +
                       " moved_real_instances=" + std::to_string(m.movedInstances) +
                       " sum_abs_dx=" + std::to_string(m.absDx) +
                       " max_abs_dx=" + std::to_string(m.maxDx) +
                       " sum_abs_dy=" + std::to_string(m.absDy) + " status=" + status);
        }
    }
    // END SLR_ROOM_MOVEMENT_AUDIT
    placement->updateElementBinGrid();
}
