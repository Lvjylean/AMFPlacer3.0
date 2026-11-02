#include "DesignInfo.h"
#include "PlacementInfo.h"
#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "CLBSiteLegality.h"
#include "simpleJSON.h"
#include <iostream>
#include <omp.h>
static void need(bool value,const char *why) { if(!value) throw std::runtime_error(why); }
int main(int argc,char **argv) { try {
    need(argc==2,"Expected fixture config"); omp_set_num_threads(1);
    auto cfg=parseJSONFile(argv[1]);
    DeviceInfo device(cfg,cfg.at("device")); DesignInfo design(cfg,&device); PlacementInfo pi(&design,&device,cfg);
    InitialPacker packer(&design,&device,&pi,cfg); packer.pack(); pi.buildSimpleTimingGraph();
    auto cell=[&](std::string name) {return design.getCell(name);};
    auto a=cell("normal0"), b=cell("inverted0"), c=cell("clock0"), ce=cell("enable0");
    need(a->getControlSetInfo()->getId()!=b->getControlSetInfo()->getId(),"SR polarity must split control IDs");
    need(!a->getControlSetInfo()->compatibleWith(b),"SR compatibility must reject inversion");
    need(a->getControlSetInfo()->compatibleWith(cell("normal1")),"equal control compatibility");
    need(!DesignInfo::compatibleClockAndReset(*a->getControlSetInfo(),*c->getControlSetInfo()),"clock edge must split half SLICE");
    need(DesignInfo::compatibleClockAndReset(*a->getControlSetInfo(),*ce->getControlSetInfo()),"independent CE banks may share clock/reset");
    need(a->getControlSetInfo()->getId()!=ce->getControlSetInfo()->getId(),"different CE must split four-FF banks");
    struct Mapping {DesignInfo::DesignCell *FFs[2][2][4]={};} slots;
    slots.FFs[0][0][0]=a;
    need(!CLBSiteLegality::canPlaceFF(slots,b,0,1),"opposite SR rejected in shared half");
    need(!CLBSiteLegality::canPlaceFF(slots,c,0,1),"opposite CLK rejected in shared half");
    need(CLBSiteLegality::canPlaceFF(slots,b,1,0),"opposite SR allowed in independent half");
    need(CLBSiteLegality::canPlaceFF(slots,ce,0,1),"separate CE bank allowed");
    using Site=ParallelCLBPacker::PackingCLBSite;
    using Cluster=Site::PackingCLBCluster;
    DeviceInfo::DeviceSite *target=nullptr;
    for(auto site:device.getSites())if(site->getSiteType()=="SLICEL"){target=site;break;}
    need(target,"SLICEL available");std::vector<Site*> unused;
    auto pu=[&](std::string name){return pi.getPlacementUnitByCell(cell(name));};
    {
        Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);Cluster cluster(&site);
        // Keep the existing first-fit bank preference; establish both halves
        // before filling their second CE banks. This is a legality test, not a
        // change to the bank reordering/search heuristic.
        for(int begin:{0,4})for(std::string prefix:{"normal","inverted"})
            for(int i=begin;i<begin+4;++i)need(cluster.tryAddPU(pu(prefix+std::to_string(i))),"opposite eight fit separate halves");
        Cluster copy(&cluster);copy.removePUToConstructDetCluster(pu("inverted0"));
        need(copy.tryAddPU(pu("inverted0")),"polarity survives copy/remove/reinsert");
        site.setDeterminedClusterInSite(new Cluster(&copy));site.finalMapToSlots();
    }
    {
        Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);Cluster cluster(&site);
        for(int i=0;i<9;++i)need(cluster.tryAddPU(pu("normal"+std::to_string(i))),"nine same polarity fit");
        need(!cluster.tryAddPU(pu("inverted0")),"nine normal leave no compatible half for opposite SR");
        need(!cluster.tryAddPU(pu("clock0")),"nine normal leave no compatible half for opposite CLK");
    }
    std::cout<<"PASS full input control IDs, CLK/SR polarity, CE independence, trial/copy/remove/reinsert and BEL mapping\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
