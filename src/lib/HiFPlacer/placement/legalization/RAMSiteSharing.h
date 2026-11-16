#pragma once
#include "PlacementInfo.h"
#include <array>
#include <map>

// Fixed-BEL U250 footprints. A descriptor is for one movable composite primitive.
namespace ram_site_sharing {
using Macro = PlacementInfo::PlacementMacro;
using Cell = DesignInfo::DesignCell;
struct Unit {
    Macro *macro;
    DesignInfo::DesignCellType type;
    std::vector<int> writeKey, readKey;
};
inline bool supported(DesignInfo::DesignCellType type)
{
    return type == DesignInfo::CellType_RAM32M || type == DesignInfo::CellType_RAM32X1D ||
           type == DesignInfo::CellType_RAM256X1S;
}
inline bool describe(Macro *macro, PlacementInfo *info, Unit &unit)
{
    if (!macro || macro->getCells().size()!=1) return false;
    Cell *cell=macro->getCells().front();
    auto type=cell->getOriCellType();
    if (!supported(type) || cell->isVirtualCell() || macro->getCellOffsetXInMacro(cell)!=0 ||
        macro->getCellOffsetYInMacro(cell)!=0) return false;
    int polarity=info->ramSharingClockPolarity(cell);
    if (polarity<0) return false;
    std::map<std::string,int> inputs;
    for (auto pin:cell->getInputPins())
        if (pin->getNet()) inputs.emplace(pin->getRefPinName(),pin->getNet()->getElementIdInType());
    auto append=[&](std::vector<int> &key,const std::string &port) {
        auto it=inputs.find(port);
        if (it==inputs.end()) return false;
        key.push_back(it->second);
        return true;
    };
    unit={macro,type,{polarity},{}};
    if (!append(unit.writeKey,"WCLK") || !append(unit.writeKey,"WE")) return false;
    for (int bit=0;bit<(type==DesignInfo::CellType_RAM256X1S?8:5);++bit) {
        auto index=std::to_string(bit);
        std::string addr=type==DesignInfo::CellType_RAM32M?"ADDRD["+index+"]":
                         type==DesignInfo::CellType_RAM256X1S?"A["+index+"]":"A"+index;
        if (!append(unit.writeKey,addr)) return false;
        if (type==DesignInfo::CellType_RAM32X1D && !append(unit.readKey,"DPRA"+index)) return false;
    }
    return true;
}

// Input order is stable PU id order. Recompute all anchors when a mixed group forms.
inline bool assign(const std::vector<Unit> &members,std::vector<std::string> &bels)
{
    if (members.empty() || members.size()>8) return false;
    unsigned halfRAM=0, xCount=0;
    for (const auto &unit:members) {
        if (unit.writeKey!=members.front().writeKey) return false;
        if (unit.type==DesignInfo::CellType_RAM32X1D) ++xCount;
        else ++halfRAM;
    }
    if (halfRAM>2 || (halfRAM && xCount && halfRAM!=1)) return false;
    bels.clear();
    unsigned halves=0;
    std::array<int,4> high{{-1,-1,-1,-1}};
    std::array<bool,4> low{};
    for (unsigned i=0;i<members.size();++i) {
        const auto &unit=members[i];
        if (unit.type!=DesignInfo::CellType_RAM32X1D) {
            bool lower=xCount || halves++;
            bels.push_back(std::string(lower?"D":"H")+(unit.type==DesignInfo::CellType_RAM32M?"5LUT":"6LUT"));
            continue;
        }
        const int count=halfRAM?2:4;
        int row=0;
        while (row<count && (high[row]<0 || low[row] || members[high[row]].readKey!=unit.readKey)) ++row;
        bool dual=row<count;
        if (!dual) {
            row=0;
            while (row<count && high[row]>=0) ++row;
            if (row==count) return false;
            high[row]=i;
        } else low[row]=true;
        bels.push_back(std::string(1,char('G'-2*row))+(dual?"5LUT":"6LUT"));
    }
    return true;
}
}
