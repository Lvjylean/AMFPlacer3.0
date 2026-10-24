#pragma once

// A private density view for spreading. Global placement/QP grids are unchanged.
// Only rows intersecting physical SLR seams are subdivided. A partial bin counts
// as one bin, including when it has no capacity. Boundary points belong below.
#include "PlacementInfo.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace die_spread {
using Bin = PlacementInfo::PlacementBinInfo;
using Grid = std::vector<std::vector<Bin *>>;
inline int dieAt(const std::vector<float> &seams, float y) {
    return std::lower_bound(seams.begin(), seams.end(), y) - seams.begin();
}
struct Range {
    float low = -std::numeric_limits<float>::infinity();
    float high = std::numeric_limits<float>::infinity();
    void includeMember(const std::vector<float> &seams, float y, float offset) {
        const int die = dieAt(seams, y);
        if (die) low = std::max(low, std::nextafter(seams[die-1], INFINITY) - offset);
        if (die < int(seams.size())) high = std::min(high, seams[die] - offset);
    }
    bool contains(float y) const { return y >= low && y <= high; }
};

// Seam fragments can be only one float ULP tall. Keep their mapped values
// inside [bottom, top), including the single-cell case. Full bins retain the
// original nominal-height arithmetic.
inline std::pair<float,float> verticalBounds(Bin *bin, float nominal, int count, bool seamAware=true) {
    if (!seamAware || bin->top()-bin->bottom()==nominal)
        return {bin->bottom()+nominal/(count+1.0), bin->top()-nominal/(count+1.0)};
    const double bottom=bin->bottom(), top=bin->top();
    const double margin=(top-bottom)/(count+1.0);
    const float last=std::nextafter(bin->top(),-INFINITY);
    auto inside=[&](double y){return std::max(bin->bottom(),std::min(last,float(y)));};
    return {inside(bottom+margin),inside(top-margin)};
}

class GridView {
  public:
    Grid rows;
    std::vector<int> rowDie;
    std::vector<float> seams;
    // Original bin counters are not changed by this view.
    GridView(Grid &original, PlacementInfo::CompatiblePlacementTable *table,
             std::vector<float> boundaries, size_t cells,
             const std::function<PlacementInfo::Location(int)> &location,
             const std::function<int(int)> &occupation)
        : seams(std::move(boundaries)), cellBins(cells, nullptr), weights(cells, 0) {
        std::sort(seams.begin(), seams.end());
        seams.erase(std::unique(seams.begin(), seams.end()), seams.end());
        if (original.empty() || original[0].empty()) throw std::invalid_argument("Empty spreading grid");
        for (auto b : original[0]) xEnds.push_back(b->right());
        nominal.assign(seams.size()+1,0);
        for (auto &sourceRow : original) {
            std::vector<float> cuts{sourceRow[0]->bottom()};
            for (float seam : seams) {
                float cut = std::nextafter(seam, INFINITY);
                if (cut > cuts.front() && cut < sourceRow[0]->top()) cuts.push_back(cut);
            }
            cuts.push_back(sourceRow[0]->top());
            for (size_t part=1; part<cuts.size(); ++part) {
                const int y = rows.size(); rows.emplace_back();
                rowDie.push_back(dieAt(seams, cuts[part-1]));
                yEnds.push_back(cuts[part]);
                for (auto b : sourceRow) {
                    storage.emplace_back(new Bin(b->getType(), b->left(), b->right(),
                        cuts[part-1], cuts[part], y, b->X(), table));
                    auto copy=storage.back().get(); rows.back().push_back(copy);
                    copy->setRequiredBinShrinkRatio(b->getRequiredBinShrinkRatio());
                    copy->resetBinShrinkRatio();
                    // Preserve the current (possibly temporarily shrunk) effective supply.
                    if (b->getRequiredBinShrinkRatio() > 0)
                        copy->shrinkBinBy(1-b->getBinShrinkRatio()/b->getRequiredBinShrinkRatio());
                    for (auto site : b->getCorrespondingSites())
                        if (site->Y() >= cuts[part-1] && site->Y() < cuts[part]) copy->addSiteIntoBin(site);
                    nominal[rowDie.back()]+=copy->getNominalCapacity();
                }
            }
        }
        for (auto &row : original) for (auto b : row) for (auto cell : b->getCells()) {
            const int id=cell->getCellId(); const auto loc=location(id);
            if (cellBins.at(id)) throw std::logic_error("Duplicate spreading view member");
            auto target=at(loc.X,loc.Y); weights[id]=occupation(id);
            target->addCell(cell, weights[id]); cellBins[id]=target;
        }
    }
    Bin *at(float x, float y) const {
        auto ix=std::upper_bound(xEnds.begin(),xEnds.end(),x)-xEnds.begin();
        auto iy=std::upper_bound(yEnds.begin(),yEnds.end(),y)-yEnds.begin();
        if (ix>=long(xEnds.size()) || iy>=long(yEnds.size()) ||
            x<rows[0][0]->left() || y<rows[0][0]->bottom())
            throw std::out_of_range("Cell outside spreading density view");
        return rows[iy][ix];
    }
    void update(DesignInfo::DesignCell *cell, float x, float y) {
        const int id=cell->getCellId(); auto old=cellBins.at(id);
        if (!old) return; // A macro member belonging to another resource pass.
        auto next=at(x,y); if (old==next) return;
        old->removeCell(cell,weights[id]); next->addCell(cell,weights[id]); cellBins[id]=next;
    }
    std::pair<int,int> rowRange(int row) const {
        int a=row,b=row; const int die=rowDie.at(row);
        while(a>0 && rowDie[a-1]==die)--a;
        while(b+1<int(rows.size()) && rowDie[b+1]==die)++b;
        return {a,b};
    }
    struct Supply { double demand=0, nominal=0, effective=0; };
    std::vector<Supply> supply() const {
        std::vector<Supply> out(seams.size()+1);
        for(size_t y=0;y<rows.size();++y) for(auto b:rows[y]) {
            auto &s=out[rowDie[y]]; s.demand+=b->getUtilization();
            s.effective+=b->getCapacity();
        }
        for(size_t i=0;i<out.size();++i) out[i].nominal=nominal[i];
        return out;
    }
  private:
    std::vector<std::unique_ptr<Bin>> storage;
    std::vector<Bin*> cellBins;
    std::vector<int> weights;
    std::vector<float> xEnds,yEnds;
    std::vector<double> nominal;
};
// A bounded local attempt must actually have been written before its residual
// overflow can authorize the geometry/writeback fallback on a later inner pass.
struct Failure { bool needsWiderLocal=false, exhaustedLocalWritten=false; };
}
