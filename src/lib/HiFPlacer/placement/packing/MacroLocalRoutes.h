#ifndef AMF_MACRO_LOCAL_ROUTES_H
#define AMF_MACRO_LOCAL_ROUTES_H
#include "DesignInfo.h"
#include "../legalization/HardResourceUtils.h"
#include <array>

// UltraScale eight-row SLICE intrinsic data paths, independent of a DFX cache.
// Check a COMPLETE mapping, never a transient Vivado import batch. This is a
// local necessary-condition check, not a proof of global routability.
namespace MacroLocalRoutes {
using Cell=DesignInfo::DesignCell;
using Net=DesignInfo::DesignNet;
inline bool real(Cell *c) { return c && !c->isVirtualCell(); }
inline Net *pin(Cell *c,const std::string &ref) {
    if(!real(c))return nullptr;
    for(auto p:c->getPins())if(p->getRefPinName()==ref && !p->isUnconnected())return p->getNet();
    return nullptr;
}
inline Net *lutOutput(Cell *c,bool five) {
    if(!real(c))return nullptr;
    return pin(c,c->getOriCellType()==DesignInfo::CellType_LUT6_2?(five?"O5":"O6"):"O");
}
template<class Mapping> bool compatible(const Mapping &m) {
    bool macro=real(m.Carry);
    for(int h=0;h<2;++h) {
        macro|=real(m.MuxF8[h]);
        for(int k=0;k<2;++k)macro|=real(m.MuxF7[h][k]);
    }
    if(!macro)return true;
    std::array<Net*,8> out{};
    auto reserve=[&](unsigned row,Net *net) {
        if(!net)return true;
        if(out[row] && out[row]!=net)return false;
        out[row]=net;return true;
    };
    // Dedicated F7 I1/I0 consume the lower/upper O6 wire. A virtual or
    // unoccupied source may provide a route-through; a real LUT cannot drive
    // a DIFFERENT net on that same physical wire.
    for(int h=0;h<2;++h)for(int k=0;k<2;++k)if(real(m.MuxF7[h][k])) {
        for(int input=0;input<2;++input) {
            auto required=pin(m.MuxF7[h][k],input?"I1":"I0");
            auto owner=m.LUTs[h][0][k*2+(input?0:1)];
            if(required && real(owner) && lutOutput(owner,false)!=required)return false;
        }
    }
    for(int h=0;h<2;++h)if(real(m.MuxF8[h]))for(int input=0;input<2;++input) {
        auto required=pin(m.MuxF8[h],input?"I1":"I0");
        auto owner=m.MuxF7[h][input?0:1];
        if(required && real(owner) && pin(owner,"O")!=required)return false;
    }
    // Only actual direct sinks avoid the shared OUTMUX. A local cell in a
    // different row is still a fabric consumer. Same-net sharing is legal.
    for(int row=0;row<8;++row) {
        int h=row/4,k=row%4;
        auto lut=m.LUTs[h][1][k];
        if(!real(lut) && real(m.LUTs[h][0][k]) &&
           m.LUTs[h][0][k]->getOriCellType()==DesignInfo::CellType_LUT6_2)lut=m.LUTs[h][0][k];
        auto net=lutOutput(lut,true);
        if(net)for(auto sink:net->getPinsBeDriven()) {
            bool direct=sink->getCell()==m.FFs[h][1][k] && sink->getRefPinName()=="D";
            direct|=sink->getCell()==m.Carry && sink->getRefPinName()=="DI["+std::to_string(row)+"]";
            if(!direct) {if(!reserve(row,net))return false;break;}
        }
    }
    // The existing prepacker reserves ALL eight FF BELs when this half
    // needs transparent-latch export. This is an alternate carry O/CO exit,
    // not another claim on the LUT's OUTMUX. Partial/free reservations or
    // any sequential FF cannot certify the shared latch mode.
    std::array<bool,2> latchHalf{{false,false}};
    if(real(m.Carry))for(int h=0;h<2;++h) {
        latchHalf[h]=true;
        for(int lane=0;lane<2;++lane)for(int row=0;row<4;++row)
            latchHalf[h]&=m.FFs[h][lane][row] && m.FFs[h][lane][row]->isVirtualCell();
    }
    if(real(m.Carry))for(auto driver:m.Carry->getOutputPins()) {
        const auto &ref=driver->getRefPinName();
        bool co=ref.size()==5 && ref.compare(0,3,"CO[")==0;
        bool sum=ref.size()==4 && ref.compare(0,2,"O[")==0;
        if((!co&&!sum)||driver->isUnconnected()||!driver->getNet())continue;
        unsigned row=ref[co?3:2]-'0';if(row>=8)continue;
        for(auto sink:driver->getNet()->getPinsBeDriven()) {
            bool direct=sink->getCell()==m.FFs[row/4][co?1:0][row%4] && sink->getRefPinName()=="D";
            direct|=HardResourceUtils::isCarryCascade(driver,sink);
            if(!direct) {
                // Its corresponding virtual latch/Q is exclusively reserved;
                // the ordinary FF allocator may not consume it.
                if(!latchHalf[row/4] && !reserve(row,driver->getNet()))return false;
                break;
            }
        }
    }
    for(int h=0;h<2;++h)for(int level=0;level<3;++level) {
        auto cell=level<2?m.MuxF7[h][level]:m.MuxF8[h];
        auto net=pin(cell,"O");if(!net)continue;
        int row=h*4+(level<2?level*2+1:2);
        for(auto sink:net->getPinsBeDriven()) {
            bool direct=sink->getCell()==m.FFs[h][0][row%4] && sink->getRefPinName()=="D";
            if(level<2)direct|=sink->getCell()==m.MuxF8[h] && sink->getRefPinName()==(level?"I0":"I1");
            if(!direct) {if(!reserve(row,net))return false;break;}
        }
    }
    return true;
}
}
#endif
