#ifndef AMF_LOCAL_PLACEMENT_ACCESS_H
#define AMF_LOCAL_PLACEMENT_ACCESS_H
#include "LocalAccessDatabase.h"
#include "LutPinAssignment.h"
#include "../DesignInfo.h"

namespace amf_native {
// This adapter uses the current module's nets, never old RM cell identities.
class LocalPlacementAccess {
public:
    LocalAccessDatabase database;
    bool enabled=false;
    std::vector<LocalPinAccess::Demand> dspContext;
    LocalPinAccess::ContextIndex dspContextIndex;
    void setDspContext(const std::vector<LocalPinAccess::Demand>&demands) {
        if(!database.access.solve(demands).feasible)
            throw std::runtime_error("DSP context is not jointly locally feasible");
        dspContext=demands;
        dspContextIndex=database.access.indexContext(dspContext);
    }
    using Cell=DesignInfo::DesignCell;
    using Pin=DesignInfo::DesignPin;
    struct Slot { Cell *cell; std::string bel; bool outputInternal=false; };
    static std::string net(Pin *p) {
        if(p->isUnconnected())return "";
        return p->getNet()?p->getNet()->getName():p->getNetName();
    }
    static bool logicLut(Cell *c) {
        auto t=c->getOriCellType();
        return t==DesignInfo::CellType_LUT1||t==DesignInfo::CellType_LUT2||
               t==DesignInfo::CellType_LUT3||t==DesignInfo::CellType_LUT4||
               t==DesignInfo::CellType_LUT5||t==DesignInfo::CellType_LUT6||
               t==DesignInfo::CellType_LUT6_2;
    }
    LutPinAssignment::Result slice(const std::string &site,const std::vector<Slot> &slots) const {
        std::vector<LutPinAssignment::Input> inputs;
        std::vector<LocalPinAccess::Demand> other;
        std::set<unsigned> seen;
        std::set<unsigned> pairedRows;
        for(const auto &s:slots)if(s.bel.size()>1&&s.bel.substr(1)=="5LUT")pairedRows.insert(s.bel[0]-'A');
        for(const auto &s:slots) {
            auto c=s.cell;if(!c||c->isVirtualCell()||!seen.insert(c->getCellId()).second)continue;
            bool lut=logicLut(c),ff=c->isFF();
            if(!lut&&!ff)continue; // SRL/RAM and carry/mux dedicated legality is retained by the original packer.
            if(lut) {
                const bool five=s.bel.substr(1)=="5LUT";
                const bool dual=c->getOriCellType()==DesignInfo::CellType_LUT6_2;
                unsigned row=s.bel[0]-'A';
                for(auto p:c->getInputPins()) {
                    const auto &ref=p->getRefPinName();
                    if(ref.size()!=2||ref[0]!='I'||ref[1]<'0'||ref[1]>'5')
                        throw std::runtime_error("Unexpected LUT input: "+p->getName());
                    auto n=net(p);
                    LutPinAssignment::Input in;
                    in.cell=c->getCellId();in.logical=ref;in.row=row;
                    in.demand=database.demand(p->getName(),n.empty()?"@UNCONNECTED/"+p->getName():n,{});
                    in.checkEntrance=!n.empty()&&database.sites.count(site);
                    unsigned last=(five||pairedRows.count(row)||dual)?5:6;
                    if(dual&&ref=="I5")last=6;
                    unsigned first=(dual&&ref=="I5")?6:1;
                    for(unsigned k=first;k<=last;++k) {
                        auto paths=database.lookup(site,s.bel,"A"+std::to_string(k));
                        in.physical[k]=paths?*paths:LocalAccessDatabase::Paths{};
                    }
                    inputs.push_back(std::move(in));
                }
                // A separate 5LUT/6LUT pair needs the high half selected on A6.
                if(!five&&!dual&&pairedRows.count(row)&&database.sites.count(site)) {
                    auto paths=database.lookup(site,s.bel,"A6");
                    other.push_back(database.demand(c->getName()+"/@A6","<const1>",*paths));
                }
            }
            if(s.outputInternal)continue;
            for(auto p:c->getOutputPins()) {
                auto n=net(p);
                if(n.empty()||(p->getNet()&&p->getNet()->getPinsBeDriven().empty()))continue;
                std::string physical=ff?"Q":s.bel.substr(1)=="5LUT"?"O5":"O6";
                if(lut&&c->getOriCellType()==DesignInfo::CellType_LUT6_2)physical=p->getRefPinName();
                auto bel=s.bel;
                // O5 of a LUT6_2 is the paired 5LUT output.
                if(lut&&physical=="O5")bel[1]='5';
                auto paths=database.lookup(site,bel,physical);
                if(paths)other.push_back(database.demand(p->getName(),n,*paths));
            }
        }
        // Include the connected component of already legalized DSP entrances;
        // a CLB must not independently consume an entrance required by a DSP.
        auto seeds=other;
        for(const auto &in:inputs)if(in.checkEntrance){auto demand=in.demand;demand.entrances.clear();
            for(const auto&p:in.physical)demand.entrances.insert(demand.entrances.end(),p.second.begin(),p.second.end());
            seeds.push_back(std::move(demand));}
        for(auto i:database.access.conflictingContext(seeds,dspContext,dspContextIndex))other.push_back(dspContext[i]);
        return LutPinAssignment::solve(database.access,inputs,other);
    }
    static std::string dspPin(const std::string &logical) {
        auto p=logical.find('[');
        if(p!=std::string::npos) {
            if(logical.back()!=']')throw std::runtime_error("Invalid DSP bus pin");
            auto prefix=logical.substr(0,p),bit=logical.substr(p+1,logical.size()-p-2);
            if(prefix=="ACIN"||prefix=="BCIN"||prefix=="ACOUT"||prefix=="BCOUT")prefix+="_B";
            else if(prefix=="D")prefix="DIN";
            return prefix+bit;
        }
        if(logical=="PATTERNDETECT")return "PATTERN_DETECT";
        if(logical=="PATTERNBDETECT")return "PATTERN_B_DETECT";
        return logical;
    }
    static bool cascade(const std::string &ref) {
        return ref.compare(0,4,"ACIN")==0||ref.compare(0,4,"BCIN")==0||
               ref.compare(0,4,"PCIN")==0||ref.compare(0,5,"ACOUT")==0||
               ref.compare(0,5,"BCOUT")==0||ref.compare(0,5,"PCOUT")==0||
               ref=="CARRYCASCIN"||ref=="CARRYCASCOUT"||ref=="MULTSIGNIN"||ref=="MULTSIGNOUT";
    }
    std::vector<LocalPinAccess::Demand> dspDemands(Cell *c,const std::string &site) const {
        std::vector<LocalPinAccess::Demand> ds;
        if(!enabled||!c->isDSP()||c->isVirtualCell())return ds;
        for(auto p:c->getPins()) {
            auto n=net(p);const auto &ref=p->getRefPinName();
            if(n.empty()||ref=="CLK")continue; // Clock network uses the retained clock contract/guidance.
            if(p->isOutputPort()&&p->getNet()&&p->getNet()->getPinsBeDriven().empty())continue;
            // Dedicated cascade defaults are configuration values, not ordinary
            // fabric tie routes. Real signal cascades remain constrained to the
            // dedicated one-hop path and the existing macro adjacency checker.
            if(cascade(ref)&&(n=="<const0>"||n=="<const1>"))continue;
            auto paths=database.lookup(site,"DSP48E2",dspPin(ref));
            if(!paths)throw std::runtime_error("DSP site missing from local access certificate: "+site);
            ds.push_back(database.demand(p->getName(),n,*paths));
        }
        return ds;
    }
    bool dspAllows(Cell *c,const std::string &site) const {
        if(!enabled||!c->isDSP())return true;
        return database.access.solve(dspDemands(c,site)).feasible;
    }
};
}
#endif

