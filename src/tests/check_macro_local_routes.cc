#include "DesignInfo.h"
#include "PlacementInfo.h"
#include "ParallelCLBPacker.h"
#include "MacroLocalRoutes.h"
#include "simpleJSON.h"
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <omp.h>
static void need(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv){try {
    need(argc==2,"fixture directory");omp_set_num_threads(1);
    std::string root=argv[1];auto cfg=parseJSONFile(root+"/config.json");
    DeviceInfo dev(cfg,cfg.at("device"));DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    using Site=ParallelCLBPacker::PackingCLBSite;
    std::map<std::string,Site::SiteBELMapping> mappings;
    std::map<std::string,std::pair<std::string,std::string>> slots;
    std::ifstream f(root+"/slots.tsv");std::string line;
    while(std::getline(f,line)) {
        std::istringstream s(line);std::string name,site,bel;s>>name>>site>>bel;
        auto c=design.getCell(name);auto &m=mappings[site];slots[name]={site,bel};
        if(bel=="CARRY8")m.Carry=c;
        else if(bel.find("F7MUX_")==0) {unsigned row=bel[6]-'A';m.MuxF7[row/4][(row%4)/2]=c;}
        else if(bel.find("F8MUX_")==0)m.MuxF8[bel=="F8MUX_TOP"]=c;
        else {unsigned row=bel[0]-'A';
            if(bel.find("LUT")!=std::string::npos)m.LUTs[row/4][bel[1]=='5'][row%4]=c;
            else m.FFs[row/4][bel.back()=='2'][row%4]=c;
        }
    }
    unsigned checks=0;std::vector<Site*> unused;
    auto check=[&](const std::string &site,const Site::SiteBELMapping &m,bool expected,const std::string &label) {
        need(MacroLocalRoutes::compatible(m)==expected,label+" helper");++checks;
        DeviceInfo::DeviceSite *target=nullptr;
        for(auto ds:dev.getSites())if(ds->getName()==site){target=ds;break;}
        need(target,"fixture site missing");
        Site pack(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
        need(pack.macroLocalRoutesEnabled,"architecture gate");
        pack.getSlotMappingRef()=m;
        need(pack.chooseLocalPinMapping()==expected,label+" production final-map gate");++checks;
        bool rejected=false;try{pack.verifyFfQAccess();}catch(const std::runtime_error&){rejected=true;}
        need(rejected!=expected,label+" commit gate");++checks;
        pack.macroLocalRoutesEnabled=false;
        need(pack.chooseLocalPinMapping(),"other architecture unchanged");++checks;
    };
    std::map<std::string,Site::SiteBELMapping> base=mappings;
    auto remove=[&](Site::SiteBELMapping &m,const std::string &name) {
        auto lookup=name;auto c=design.getCell(lookup);
        for(int h=0;h<2;++h)for(int lane=0;lane<2;++lane)for(int row=0;row<4;++row) {
            if(m.LUTs[h][lane][row]==c)m.LUTs[h][lane][row]=nullptr;
            if(m.FFs[h][lane][row]==c)m.FFs[h][lane][row]=nullptr;
        }
    };
    struct Batch {int id;std::string reason,a,b;};std::vector<Batch> batches;
    std::ifstream bf(root+"/batches.tsv");
    while(std::getline(bf,line)) {
        std::istringstream s(line);Batch b;s>>b.id>>b.reason>>b.a>>b.b;batches.push_back(b);
        remove(base[slots.at(b.a).first],b.a);remove(base[slots.at(b.b).first],b.b);
    }
    unsigned carryComplete=0,muxRejected=0,partialCarryRejected=0;
    for(const auto &entry:mappings) {
        bool carry=entry.second.Carry!=nullptr;
        check(entry.first,entry.second,carry,"complete "+entry.first);
        if(carry)++carryComplete;else ++muxRejected;
    }
    for(const auto &b:batches) {
        auto site=slots.at(b.a).first;
        if(b.reason=="outmux_contention") {
            // Original partial import state is not a complete packing candidate.
            auto partial=base[site];
            for(auto name:{b.a,b.b}) {
                auto c=design.getCell(name);auto bel=slots.at(name).second;unsigned row=bel[0]-'A';
                if(c->isFF())partial.FFs[row/4][bel.back()=='2'][row%4]=c;
                else partial.LUTs[row/4][bel[1]=='5'][row%4]=c;
            }
            check(site,partial,false,"partial import "+std::to_string(b.id));++partialCarryRejected;
        }
    }
    // Remove only the LUTs that collide with mandatory F7 O6 wires. Empty
    // source rows may legally provide the packer's existing route-through.
    for(const auto &entry:mappings)if(!entry.second.Carry) {
        auto repaired=entry.second;unsigned removed=0;
        for(int h=0;h<2;++h)for(int k=0;k<2;++k)if(MacroLocalRoutes::real(repaired.MuxF7[h][k]))
            for(int input=0;input<2;++input) {
                auto required=MacroLocalRoutes::pin(repaired.MuxF7[h][k],input?"I1":"I0");
                auto &owner=repaired.LUTs[h][0][k*2+(input?0:1)];
                if(required&&MacroLocalRoutes::real(owner)&&MacroLocalRoutes::lutOutput(owner,false)!=required){owner=nullptr;++removed;}
            }
        need(removed>0,"MUX negative control must change a real conflicting LUT");
        check(entry.first,repaired,true,"MUX route-through repaired "+entry.first);
    }
    // Moving a CARRY's direct FF out exposes the sum on its OUTMUX. The
    // occupied O5 route must then reject the different net; restore is legal.
    for(const auto &entry:mappings)if(entry.second.Carry) {
        auto partial=entry.second;bool tested=false;
        for(int row=0;row<8&&!tested;++row)if(partial.FFs[row/4][0][row%4]) {
            auto saved=partial.FFs[row/4][0][row%4];partial.FFs[row/4][0][row%4]=nullptr;
            if(!MacroLocalRoutes::compatible(partial)) {check(entry.first,partial,false,"CARRY exposed sum conflict");tested=true;}
            partial.FFs[row/4][0][row%4]=saved;
        }
        need(tested,"CARRY negative control missing");
        check(entry.first,partial,true,"CARRY direct sum restored");
    }
    Site::SiteBELMapping empty;need(MacroLocalRoutes::compatible(empty),"ordinary empty site");++checks;
    volatile unsigned accepted=0;auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<10000;++i)for(const auto &e:mappings)accepted+=MacroLocalRoutes::compatible(e.second);
    auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"PASS checks="<<checks<<" complete_carry_legal="<<carryComplete<<" mux_conflict_rejected="<<muxRejected
             <<" partial_import_outmux_rejected="<<partialCarryRejected<<" mean_check_ns="<<ns/100000.0<<" accepted="<<accepted<<"\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
