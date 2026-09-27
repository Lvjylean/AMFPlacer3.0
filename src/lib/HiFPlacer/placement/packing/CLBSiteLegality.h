#ifndef AMF_CLB_SITE_LEGALITY_H
#define AMF_CLB_SITE_LEGALITY_H

#include "DesignInfo.h"
#include <algorithm>
#include <vector>

// UltraScale/UltraScale+ rules shared by slot moves and final export checks.
namespace CLBSiteLegality
{
using Cell = DesignInfo::DesignCell;

inline bool lutPairCompatible(Cell *a, Cell *b)
{
    if (!a || !b) return true;
    if (a == b) return a->getOriCellType() == DesignInfo::CellType_LUT6_2;
    if (a->isLUT6() || b->isLUT6() || a->getInputPins().size() > 5 || b->getInputPins().size() > 5)
        return false;
    // Preserve multiplicity: two logical pins tied to one net need distinct LUT
    // input pins unless an explicit pin remapping has been proved valid.
    std::vector<DesignInfo::DesignNet *> remaining;
    for (auto pin : a->getInputPins())
        if (!pin->isUnconnected()) remaining.push_back(pin->getNet());
    unsigned inputs = remaining.size();
    for (auto pin : b->getInputPins())
        if (!pin->isUnconnected())
        {
            auto found = std::find(remaining.begin(), remaining.end(), pin->getNet());
            if (found == remaining.end()) ++inputs;
            else remaining.erase(found);
        }
    return inputs <= 5;
}

template <typename Mapping>
bool canPlaceFF(const Mapping &slots, Cell *cell, int half, int lane)
{
    if (!cell || cell->isVirtualCell()) return true;
    auto cs = cell->getControlSetInfo();
    if (!cs) return false;
    for (int j = 0; j < 2; ++j)
        for (int k = 0; k < 4; ++k)
        {
            auto other = slots.FFs[half][j][k];
            if (!other || other == cell || other->isVirtualCell()) continue;
            auto otherCS = other->getControlSetInfo();
            if (!otherCS) return false;
            if (j == lane && otherCS->getId() != cs->getId()) return false;
            if (otherCS->getCLK() != cs->getCLK() || otherCS->getSR() != cs->getSR() ||
                !DesignInfo::FFSRCompatible(other->getOriCellType(), cell->getOriCellType()))
                return false;
        }
    return true;
}

template <typename Mapping>
bool checkControlsAndLUTs(const Mapping &slots)
{
    for (int i = 0; i < 2; ++i)
        for (int k = 0; k < 4; ++k)
        {
            if (!lutPairCompatible(slots.LUTs[i][0][k], slots.LUTs[i][1][k])) return false;
            for (int j = 0; j < 2; ++j)
                if (!canPlaceFF(slots, slots.FFs[i][j][k], i, j)) return false;
        }
    return true;
}
}
#endif
