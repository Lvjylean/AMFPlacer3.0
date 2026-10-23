#include "PlacementInfo.h"
#include "../../../utils/RuntimeProfiler.h"
#include <chrono>
#include <cstring>
#include <omp.h>

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point t) { return std::chrono::duration<double>(Clock::now()-t).count(); }
}

void PlacementInfo::updateElementBinGrid(bool full, bool bypass, const char *caller)
{
    AMF_PROFILE_FUNCTION("placement_bookkeeping");
    const auto started = Clock::now();
    if(hardRegions.enabled) for(auto pu:placementUnits) enforceLegalizeXYInArea(pu,false);
    const auto mode = grid_update::mode();
    gridUpdateStats = {};
    ++gridUpdateCall;
    if (mode == grid_update::Mode::legacy) {
        const bool trace = std::getenv("AMF_GRID_UPDATE_TRACE") != nullptr;
        const auto previous = trace ? cellId2location : std::vector<Location>{};
        updateElementBinGridLegacy(full, bypass);
        if (trace) {
            for (size_t i=0;i<cellId2location.size();++i)
                if (i>=previous.size() || previous[i].X!=cellId2location[i].X || previous[i].Y!=cellId2location[i].Y)
                    ++gridUpdateStats.coordinateChanges;
            for (auto pu : placementUnits) {
                if (bypass && (pu->checkHasBRAM() || pu->checkHasDSP() || pu->checkHasURAM())) continue;
                if (auto macro = dynamic_cast<PlacementMacro *>(pu)) gridUpdateStats.records += macro->getNumOfCells();
                else ++gridUpdateStats.records;
            }
        }
    }
    else updateElementBinGridBatched(full, bypass);
    const double updateSeconds = elapsed(started);
    double verifySeconds = 0;
    if (grid_update::verify() && mode != grid_update::Mode::legacy) {
        const auto t = Clock::now();
        verifyElementBinGrid(full, bypass);
        verifySeconds = elapsed(t);
    }
    if (const char *path = std::getenv("AMF_GRID_UPDATE_TRACE")) {
        if (!gridUpdateTrace.is_open()) {
            gridUpdateTrace.open(path);
            if (!gridUpdateTrace) throw std::runtime_error("Cannot open AMF_GRID_UPDATE_TRACE");
            gridUpdateTrace << "call\tmode\tcaller\tfull\tbypass\tpus\trecords\tcoordinate_changes\tbins\tmember_rebuilds\tmember_reuses\tprepare_s\tpartition_s\tshared_s\tauxiliary_s\tupdate_s\tverify_s\tlegacy_reference_s\n";
        }
        const auto &s = gridUpdateStats;
        gridUpdateTrace << gridUpdateCall << '\t' << static_cast<int>(mode) << '\t' << caller << '\t'
            << full << '\t' << bypass << '\t' << placementUnits.size() << '\t' << s.records << '\t'
            << s.coordinateChanges << '\t' << s.bins << '\t' << s.memberRebuilds << '\t' << s.memberReuses
            << '\t' << s.prepare << '\t' << s.partition << '\t' << s.shared << '\t' << s.auxiliary
            << '\t' << updateSeconds << '\t' << verifySeconds << '\t' << s.legacyReference << '\n';
        gridUpdateTrace.flush();
    }
}

void PlacementInfo::updateElementBinGridBatched(bool full, bool bypass)
{
    AMF_PROFILE_FUNCTION("placement_bookkeeping");
    const bool parallel = grid_update::parallel();
    const bool incremental = grid_update::mode() == grid_update::Mode::incremental;
    if (SharedBELTypeBinGrid.empty() || SharedBELTypeBinGrid.front().empty()) {
        // Keep the original behavior for incomplete initialization.
        updateElementBinGridLegacy(full, bypass);
        return;
    }
    auto t = Clock::now();
    const size_t ny = SharedBELTypeBinGrid.front().size();
    const size_t nx = SharedBELTypeBinGrid.front().front().size();
    const size_t nBins = nx * ny, nTypes = SharedBELTypeBinGrid.size();
    for (const auto &grid : SharedBELTypeBinGrid) {
        if (grid.size() != ny) throw std::runtime_error("Unequal resource grid heights");
        for (const auto &row : grid)
            if (row.size() != nx) throw std::runtime_error("Unequal resource grid widths");
    }
    cellId2location.resize(designInfo->getNumCells());
    gridRecords.clear();
    gridRecords.reserve(designInfo->getNumCells());
    auto append = [&](DesignInfo::DesignCell *cell, float x, float y, bool auxiliary) {
        int bx, by;
        getGridXY(x, y, bx, by);
        if (bx < 0 || by < 0 || size_t(bx) >= nx || size_t(by) >= ny)
            throw std::runtime_error("Cell outside density grid: " + cell->getName());
        const auto &types = getPotentialBELTypeIDs(cell);
        if (types.empty()) throw std::runtime_error("Cell without BEL types");
        for (int type : types) {
            if (type < 0 || size_t(type) >= nTypes || !SharedBELTypeBinGrid[type][by][bx]->inRange(x,y))
                throw std::runtime_error("Cell outside assigned density bin: " + cell->getName());
        }
        const float occupation = getActualOccupation(cell);
        if (!(occupation >= 0)) throw std::runtime_error("Invalid cell occupation");
        auto &loc = cellId2location.at(cell->getCellId());
        if (loc.X != x || loc.Y != y) ++gridUpdateStats.coordinateChanges;
        loc.X = x; loc.Y = y;
        const bool lutff = cell->isLUT() || cell->isFF();
        gridRecords.push_back({cell,x,y,occupation,bx,by,size_t(by)*nx+bx,
                               auxiliary && lutff,
                               auxiliary && (lutff || cell->isCarry() || cell->isMux()), &types});
    };
    // This order is the legacy PU vector order, then the virtual-cell order.
    for (auto pu : placementUnits) {
        const bool hard = pu->checkHasBRAM() || pu->checkHasDSP() || pu->checkHasURAM();
        if (bypass && hard) continue;
        if (auto cell = dynamic_cast<PlacementUnpackedCell *>(pu))
            append(cell->getCell(), cell->X(), cell->Y(), !hard);
        else if (auto macro = dynamic_cast<PlacementMacro *>(pu)) {
            for (int v = 0; v < macro->getNumOfCells(); ++v) {
                float dx,dy; DesignInfo::DesignCellType type;
                macro->getVirtualCellInfo(v,dx,dy,type);
                append(macro->getCell(v), macro->X()+dx, macro->Y()+dy, !hard);
            }
        }
    }
    gridUpdateStats.records = gridRecords.size();
    gridUpdateStats.bins = nBins;
    gridUpdateStats.prepare = elapsed(t);
    t = Clock::now();
    grid_update::stableBuckets(gridRecords,nBins,gridOffsets,gridOrder);
    gridUpdateStats.partition = elapsed(t);

    t = Clock::now();
    size_t rebuilt = 0, reused = 0;
#pragma omp parallel if(parallel) reduction(+:rebuilt,reused)
    {
        std::vector<PlacementBinInfo *> bins(nTypes);
        std::vector<std::vector<DesignInfo::DesignCell *>> members(incremental ? nTypes : 0);
#pragma omp for schedule(static)
        for (size_t b = 0; b < nBins; ++b) {
            const size_t y = b / nx, x = b % nx;
            for (size_t type = 0; type < nTypes; ++type) {
                bins[type] = SharedBELTypeBinGrid[type][y][x];
                bins[type]->beginGridRebuild(incremental);
                if (incremental) members[type].clear();
            }
            for (size_t i = gridOffsets[b]; i < gridOffsets[b+1]; ++i) {
                const auto &r = gridRecords[gridOrder[i]];
                const int type = grid_update::chooseType(*r.types, [&](int candidate) {
                    return bins[candidate]->canAddMore(r.occupation);
                });
                bins[type]->addGridCellExclusive(r.cell,r.occupation,incremental);
                if (incremental) members[type].push_back(r.cell);
                setCellBinInfo(r.cell->getCellId(),type,r.bx,r.by,r.occupation);
            }
            for (size_t type = 0; type < nTypes; ++type) {
                if (!incremental || bins[type]->finishGridMembers(members[type])) ++rebuilt;
                else ++reused;
            }
        }
    }
    gridUpdateStats.shared = elapsed(t);
    t = Clock::now();
    if (full) {
        // The auxiliary grids exclude hard-resource PUs, exactly as before.
#pragma omp parallel if(parallel) reduction(+:rebuilt,reused)
        {
            std::vector<DesignInfo::DesignCell *> globals, clbs;
#pragma omp for schedule(static)
            for (size_t b = 0; b < nBins; ++b) {
                const size_t y = b / nx, x = b % nx;
                auto global = globalBinGrid.at(y).at(x);
                auto clb = CLBBinGrid.at(y).at(x);
                global->beginGridRebuild(incremental); clb->beginGridRebuild(incremental);
                globals.clear(); clbs.clear();
                for (size_t i = gridOffsets[b]; i < gridOffsets[b+1]; ++i) {
                    const auto &r = gridRecords[gridOrder[i]];
                    if (r.global) {
                        global->addGridCellExclusive(r.cell,0,incremental);
                        if (incremental) globals.push_back(r.cell);
                    }
                    if (r.clb) {
                        clb->addGridCellExclusive(r.cell,0,incremental);
                        if (incremental) clbs.push_back(r.cell);
                    }
                }
                if (!incremental || global->finishGridMembers(globals)) ++rebuilt; else ++reused;
                if (!incremental || clb->finishGridMembers(clbs)) ++rebuilt; else ++reused;
            }
        }
    }
    gridUpdateStats.auxiliary = elapsed(t);
    gridUpdateStats.memberRebuilds = rebuilt;
    gridUpdateStats.memberReuses = reused;
}

void PlacementInfo::verifyElementBinGrid(bool full, bool bypass)
{
    AMF_PROFILE_FUNCTION("validation");
    using State = PlacementBinInfo::GridAuditState;
    auto capture = [&] {
        std::vector<State> states;
        for (auto &grid : SharedBELTypeBinGrid)
            for (auto &row : grid) for (auto bin : row) states.push_back(bin->gridAuditState());
        // Include auxiliary state even when full=false: it must remain unchanged.
        for (auto &row : globalBinGrid) for (auto bin : row) states.push_back(bin->gridAuditState());
        for (auto &row : CLBBinGrid) for (auto bin : row) states.push_back(bin->gridAuditState());
        return states;
    };
    const auto expected = capture();
    const auto locations = cellId2location;
    const auto info = cellId2CellBinInfo;
    const auto referenceStarted = Clock::now();
    updateElementBinGridLegacy(full,bypass);
    gridUpdateStats.legacyReference = elapsed(referenceStarted);
    const auto reference = capture();
    if (expected != reference) throw std::runtime_error("Grid oracle mismatch at update " + std::to_string(gridUpdateCall));
    if (locations.size() != cellId2location.size() || info.size() != cellId2CellBinInfo.size())
        throw std::runtime_error("Grid oracle vector size mismatch");
    for (size_t i = 0; i < locations.size(); ++i) {
        if (std::memcmp(&locations[i],&cellId2location[i],sizeof(Location)))
            throw std::runtime_error("Grid oracle coordinate mismatch at cell " + std::to_string(i));
        const auto &a = info[i], &b = cellId2CellBinInfo[i];
        if (a.X != b.X || a.Y != b.Y || a.sharedTypeId != b.sharedTypeId || a.occupation != b.occupation)
            throw std::runtime_error("Grid oracle assignment mismatch at cell " + std::to_string(i));
    }
}
