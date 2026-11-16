#include "DesignInfo.h"
#include "PlacementInfo.h"
#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "MemoryFFRules.h"
#include "SRLSiteSharing.h"
#include "simpleJSON.h"
#include <iostream>
#include <omp.h>
static void need(bool x,const char*s){if(!x)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
 need(argc==2,"config");omp_set_num_threads(1);auto cfg=parseJSONFile(argv[1]);
 DeviceInfo dev(cfg,cfg.at("device"));DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
 InitialPacker initial(&design,&dev,&pi,cfg);initial.pack();pi.buildSimpleTimingGraph();
 using Site=ParallelCLBPacker::PackingCLBSite;using Cluster=Site::PackingCLBCluster;
 DeviceInfo::DeviceSite* target=nullptr;for(auto s:dev.getSites())if(s->getSiteType()=="SLICEM"){target=s;break;}need(target,"SLICEM");
 auto pu=[&](std::string n){return pi.getPlacementUnitByCell(design.getCell(n));};
 std::vector<Site*> unused;
 // RAM read-clock legality must be identical in trial, copy and finalMap.
 unsigned clockCases=0;
 auto clockCase=[&](std::string mem,std::string name,unsigned row,unsigned lane,bool expected) {
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu(mem));need(macro,"clock-test RAM macro");
  macro->setFixed();site.mapLUTRAMRelatedCellsToSlots(macro,4);site.prepareMemoryFFPacking(true);
  unsigned slot=MemoryFFRules::slotBit(row,lane);site.ffQBlockedMask=65535u^slot;
  Cluster candidate(&site);
  need(candidate.tryAddPU(pu(name))==expected,"RAM read-clock single-slot admission");
  if(expected) {
   Cluster copy(&candidate);copy.removePUToConstructDetCluster(pu(name));
   need(copy.tryAddPU(pu(name)),"RAM clock copy/remove/reinsert");
   site.setDeterminedClusterInSite(new Cluster(&copy));
   pu(name)->setFixed();site.finalMapToSlots();pu(name)->setUnfixed();
   auto &mapped=site.getSlotMapping().FFs[row/4][lane][row%4];
   need(mapped && mapped->getName()==name,"RAM clock finalMap exact requested slot");
  } else need(candidate.getPUs().empty(),"rejected clock trial changed cluster");
  ++clockCases;
 };
 struct ClockExample {const char *ram,*ff;unsigned row,lane;};
 const ClockExample clockExamples[] = {
  {"RAM32M16","RAM32M16_DOA0",0,0},
  {"RAM64M8","RAM64M8_DOA0",0,1},
  {"RAM32M","RAM32M_DOA0",4,0},
  {"RAM64M","RAM64M_DOA0",4,1},
  {"RAM32X1D","RAM32X1D_DPO0",6,0},
  {"RAM64X1D","RAM64X1D_DPO0",6,1},
  {"RAM256X1S","RAM256X1S_O0",6,0}
 };
 for(const auto &e:clockExamples) {
  std::string ff=e.ff;
  clockCase(e.ram,ff,e.row,e.lane,true);
  clockCase(e.ram,ff+"_foreign_clock",e.row,e.lane,false);
  clockCase(e.ram,ff+"_inverted_clock",e.row,e.lane,false);
 }
 // RAM64M8's primary lane is independent D, but direct RAM data in the
 // same physical row still carries the read-clock restriction.
 clockCase("RAM64M8","RAM64M8_DOA0_foreign_clock",0,0,false);
 clockCase("RAM64M8","RAM64M8_DOA0_foreign_clock",1,0,true);
 for(const auto &e:clockExamples) {
  if(std::string(e.ram)=="RAM32M16") continue;
  unsigned freeRow=std::string(e.ram)=="RAM64M8"?1:0;
  clockCase(e.ram,std::string(e.ff)+"_foreign_clock",freeRow,0,true);
  clockCase(e.ram,"other_clock",freeRow,0,true);
 }
 using Macro=PlacementInfo::PlacementMacro;
 std::string firstName="RAM32M16_DOA0_foreign_clock", secondName="RAM32M16_DOA1_foreign_clock";
 auto first=design.getCell(firstName);
 auto second=design.getCell(secondName);
 auto pair=new Macro("asynchronous_RAM_pair",pi.getPlacementUnits().size(),Macro::PlacementMacroType_FFFFPair);
 pair->addOccupiedSite(0,.0625);pair->setWeight(2);
 for(auto cell:{first,second}) {
  pair->addCell(cell,cell->getCellType(),0,0);
  pi.getCellId2PlacementUnit()[cell->getCellId()]=pair;
  pi.getCellId2PlacementUnitVec()[cell->getCellId()]=pair;
 }
 pi.getPlacementMacros().push_back(pair);pi.getPlacementUnits().push_back(pair);pi.reloadNets();
 unused.assign(pi.getPlacementUnits().size(),nullptr);
 Site pairSite(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
 auto owner=dynamic_cast<Macro*>(pu("RAM32M16"));owner->setFixed();
 pairSite.mapLUTRAMRelatedCellsToSlots(owner,4);pairSite.prepareMemoryFFPacking(true);
 Cluster candidate(&pairSite);
 need(!candidate.tryAddPU(pair) && candidate.getPUs().empty(),"asynchronous whole-FF-pair rejection must be atomic");
 std::cout<<"PASS RAM clock-specific slot cases="<<clockCases<<" and asynchronous whole-FF-pair rejection\n";
 return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
