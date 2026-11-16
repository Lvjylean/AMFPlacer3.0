#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "MacroLocalRoutes.h"
#include "simpleJSON.h"
#include <chrono>
#include <iostream>
#include <omp.h>
static void need(bool x,const char*s){if(!x)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
 need(argc==2,"config");omp_set_num_threads(1);auto cfg=parseJSONFile(argv[1]);
 DeviceInfo dev(cfg,cfg.at("device"));DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
 InitialPacker initial(&design,&dev,&pi,cfg);initial.pack();pi.buildSimpleTimingGraph();
 using Site=ParallelCLBPacker::PackingCLBSite;using Cluster=Site::PackingCLBCluster;
 std::string name="c1";auto carry=design.getCell(name);
 auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pi.getPlacementUnitByCell(carry));need(macro,"carry macro");
 unsigned vff=0;for(auto c:macro->getCells())vff+=c->isVirtualCell()&&c->isFF();
 need(macro->getCells().size()==19 && vff==11,"original 7-real + 1-virtual-LUT + 11-virtual-FF footprint");
 DeviceInfo::DeviceSite* target=nullptr;for(auto s:dev.getSites())if(s->getName()=="SLICE_X54Y80")target=s;need(target,"actual target");
 std::vector<Site*> unused(pi.getPlacementUnits().size(),nullptr);
 macro->setFixed();Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
 site.mapCarryRelatedCellsToSlots(macro,0);site.addCarry();
 auto base=site.getSlotMapping();need(MacroLocalRoutes::compatible(base),"complete latch export");
 unsigned cases=1;
 for(int lane=0;lane<2;++lane)for(int row=0;row<4;++row) {
  auto m=base;m.FFs[0][lane][row]=nullptr;
  need(!MacroLocalRoutes::compatible(m),"incomplete half cannot certify latch mode");++cases;
  std::string ff="freeFF0";m.FFs[0][lane][row]=design.getCell(ff);
  need(!MacroLocalRoutes::compatible(m),"sequential FF excludes shared latch mode");++cases;
 }
 for(int lane=0;lane<2;++lane)for(int row=0;row<4;++row) {
  auto m=base;if(m.FFs[1][lane][row])continue;
  std::string ff="freeFF0";m.FFs[1][lane][row]=design.getCell(ff);
  need(MacroLocalRoutes::compatible(m),"other half independent");++cases;
 }
 need(site.carryCandidateUsesFixedLUTsOnly(site.getDeterminedClusterInSite()),"positive fixed-only certificate");
 site.finalMapToSlots();need(MacroLocalRoutes::compatible(site.getSlotMapping()),"production final mapping");
 unsigned mapped=0;for(int h=0;h<2;++h)for(int l=0;l<2;++l)for(int k=0;k<4;++k)mapped+=site.getSlotMapping().FFs[h][l][k]!=nullptr;
 need(mapped==11,"virtual reserves preserved");
 auto old=site.getSlotMapping();site.carryRouteOwnerSize++;
 need(!site.carryCandidateUsesFixedLUTsOnly(site.getDeterminedClusterInSite()),"membership invalidates certificate");site.carryRouteOwnerSize--;
 std::string ff="freeFF0";auto pu=pi.getPlacementUnitByCell(design.getCell(ff));
 Cluster candidate(site.getDeterminedClusterInSite());
 need(candidate.tryAddPU(pu),"independent FF admission");
 need(site.intrinsicCarryFastAccepts>0,"FF positive witness used");
 auto fast=site.intrinsicCarryFastAccepts,slow=site.intrinsicSlowProbes;
 need(site.localCandidateAllows(&candidate),"fast FF candidate");
 site.carryFixedRoutesValid=false;
 need(site.localCandidateAllows(&candidate),"full probe agrees");site.carryFixedRoutesValid=true;
 need(site.intrinsicSlowProbes>slow,"full probe executed");
 auto start=std::chrono::steady_clock::now();volatile unsigned accepted=0;
 for(int i=0;i<10000;++i)accepted+=site.localCandidateAllows(&candidate);
 auto fastns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()/10000.0;
 site.carryFixedRoutesValid=false;start=std::chrono::steady_clock::now();
 for(int i=0;i<100;++i)accepted+=site.localCandidateAllows(&candidate);
 auto slowns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()/100.0;
 site.carryFixedRoutesValid=true;
 site.setDeterminedClusterInSite(new Cluster(&candidate));pu->setFixed();site.finalMapToSlots();pu->setUnfixed();
 need(MacroLocalRoutes::compatible(site.getSlotMapping()),"FF final mapping checked");
 // No legal witness must raise before publishing an empty mapping.
 auto saved=site.getSlotMapping();bool rejected=false;
 Site noWitness(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
 noWitness.getSlotMappingRef()=saved;
 try{noWitness.requireLegalSlotMapping("negative-control");}
 catch(const std::runtime_error&e){rejected=std::string(e.what()).find("AMF_NO_LEGAL_SLOT_MAPPING")!=std::string::npos;}
 need(rejected && site.getSlotMapping().Carry==saved.Carry,"no-witness leaves mapping intact");
 std::cout<<"PASS original_macro_members=19 virtual_FF=11 cases="<<cases<<" fast_ns="<<fastns<<" full_probe_ns="<<slowns<<" accepted="<<accepted<<"\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
