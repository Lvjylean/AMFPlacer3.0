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
 unsigned passed=0;
 auto check=[&](std::string mem,std::string ff,unsigned mask,bool enabled,bool expected){
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu(mem));need(macro,"memory macro");
  macro->setFixed(); // Reproduce actual macro-legalization state at packing entry.
  site.mapLUTRAMRelatedCellsToSlots(macro,4);site.prepareMemoryFFPacking(enabled);site.ffQBlockedMask=mask;
  Cluster c(&site);need(c.tryAddPU(pu(ff))==expected,"candidate decision");
  if(expected){Cluster cp(&c);cp.removePUToConstructDetCluster(pu(ff));need(cp.tryAddPU(pu(ff)),"copy/remove/reinsert");
   site.setDeterminedClusterInSite(new Cluster(&cp));pu(ff)->setFixed();site.finalMapToSlots();pu(ff)->setUnfixed();unsigned count=0;
   for(int h=0;h<2;++h)for(int l=0;l<2;++l)for(int k=0;k<4;++k){auto x=site.getSlotMapping().FFs[h][l][k];if(x){++count;need(x==design.getCell(ff),"identity");need(!(mask&MemoryFFRules::slotBit(h*4+k,l)),"Q blocked");}}
   need(count==1,"single occupied FF");
  }++passed;
 };
 for(std::string mem:{"srl","ram","srl16"}){
  check(mem,mem+"_local",65535u^MemoryFFRules::slotBit(7,0),true,true);
  check(mem,mem+"_local",65535u^MemoryFFRules::slotBit(7,1),true,true);
  check(mem,"external0",65535u^MemoryFFRules::slotBit(7,0),true,mem!="srl16");
  check(mem,"external0",65535u^MemoryFFRules::slotBit(7,1),true,mem=="srl16");
  check(mem,"external0",65535u^MemoryFFRules::slotBit(0,1),true,true);
  check(mem,"external0",0,false,false);
  check(mem,"logic",0,true,false);
 }
 {
  using namespace srl_site_sharing;
  Layout four,eight,after;
  for(int i=0;i<8;++i) {
   Unit u;u.id=i;u.srlCount=1;u.controls="clk+ce";u.bels={"H6LUT"};
   if(i<4) {need(append(u,four,after),"old four-cap");four=after;}
   else need(!append(u,four,after),"fifth blocked by old limit");
   need(append(u,eight,after,8),"eight distinct rows");eight=after;
  }
  Unit ninth;ninth.id=8;ninth.srlCount=1;ninth.controls="clk+ce";ninth.bels={"H6LUT"};
  need(eight.occupied.size()==8 && !append(ninth,eight,after,8),"no ninth row");
  ninth.controls="other clock";need(!append(ninth,four,after,8),"eight-cap control mismatch");
  ++passed;
 }
 {
  using namespace srl_site_sharing;
  need(siteLimit({},"U250")==1 && siteLimit({{"SRLMaxPerSLICEM","1"}},"U250")==1,"default sharing off");
  bool invalid=false;try{siteLimit({{"SRLMaxPerSLICEM","8"}},"other");}catch(const std::runtime_error&){invalid=true;}
  need(invalid,"eight must be gated by device");
  for(unsigned limit:{4u,8u}) {
   Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
   using Macro=PlacementInfo::PlacementMacro;
   auto owner=[&](unsigned i,const std::string &bel) {
    std::string name="handoff"+std::to_string(i);auto m=dynamic_cast<Macro*>(pu(name));need(m,"SRL macro");
    m->getFixedCellInfoVec().clear();m->addFixedCellInfo(design.getCell(name),"SLICEM",bel);m->setFixed();return m;
   };
   auto reject=[&](Macro *m,const std::string &why) {
    auto before=site.getLUTRAMMacros();bool refused=false;
    try{site.mapLUTRAMRelatedCellsToSlots(m,limit);}
    catch(const std::runtime_error &e){refused=std::string(e.what()).find(why)!=std::string::npos;}
    need(refused && before==site.getLUTRAMMacros(),"rejected owner must not change accepted list");
   };
   Layout layout,after;
   for(unsigned i=0;i<limit;++i) {
    Unit u;u.id=i;u.srlCount=1;u.controls="clk+ce";u.bels={"H6LUT"};
    need(append(u,layout,after,limit),"legalizer assigned the site");layout=after;
    auto m=owner(i,layout.assignments.at(i).at(0));site.mapLUTRAMRelatedCellsToSlots(m,limit);site.addLUTRAMMacro();
    need(site.getLUTRAMMacros().size()==i+1,"packing retained every owner");
    if(i==0) {
     reject(m,"Duplicate prepacked");reject(owner(8,"H6LUT"),"BEL collision");
     reject(owner(8,"A5LUT"),"BEL incompatible");
     auto partial=owner(8,"A6LUT");partial->getFixedCellInfoVec().clear();reject(partial,"complete BEL");
    }
   }
   need(site.getDeterminedClusterInSite()->getCellSet().size()==limit,"packed cluster exposed all SRLs");
   if(limit==4) reject(owner(4,"A6LUT"),"configured SRL limit 4");
   else {reject(owner(8,"A6LUT"),"BEL collision");site.prepareMemoryFFPacking(true,true);
    Cluster c(&site);need(!c.tryAddPU(pu("logic")),"full eight-row site has no ordinary LUT row");}
   ++passed;
  }
 }
 for(std::string kind:{"srl","srl16","ram","mux","RAM32M","RAM64X1D"}) {
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu(kind));
  macro->setFixed();site.mapLUTRAMRelatedCellsToSlots(macro,4);site.prepareMemoryFFPacking(true,true);
  Cluster c(&site);
  bool allow=(kind=="srl" || kind=="srl16" || kind=="RAM32M" || kind=="RAM64X1D");
  need(c.tryAddPU(pu("logic"))==allow,"ordinary LUT row availability");
  if(allow){
   need(c.tryAddPU(pu("external0")),"logic with external FF");
   Cluster copy(&c);copy.removePUToConstructDetCluster(pu("logic"));
   need(copy.tryAddPU(pu("logic")),"logic copy/remove/reinsert");
   site.setDeterminedClusterInSite(new Cluster(&copy));
   pu("logic")->setFixed();site.finalMapToSlots();pu("logic")->setUnfixed();
   auto &slots=site.getSlotMapping();unsigned found=0;
   std::string logicName="logic";
   for(unsigned r=0;r<8;++r)if(slots.LUTs[r/4][0][r%4]){
    ++found;need(r!=7 && slots.LUTs[r/4][0][r%4]==design.getCell(logicName),"preserve SRL row");
    if(kind=="srl" || kind=="srl16") need(!slots.FFs[r/4][0][r%4]&&!slots.FFs[r/4][1][r%4],"SRL route-through reservation");
   }
   need(found==1,"logic BEL after commit");
   site.ffQBlockedMask=65535u;Cluster blocked(&site);
   need(blocked.tryAddPU(pu("logic"))==(kind=="RAM32M" || kind=="RAM64X1D"),"Q reservation and logic row");
  }
  ++passed;
 }
 // Expected masks from the physical BEL inventory, independent of FF profiles.
 for (const auto &footprint : std::vector<std::pair<std::string,unsigned>>{
        {"RAM32M16",0}, {"RAM64M8",0}, {"RAM32M",15}, {"RAM64M",15},
        {"RAM256X1S",15}, {"RAM32X1D",63}, {"RAM64X1D",63}}) {
  auto m=dynamic_cast<PlacementInfo::PlacementMacro*>(pu(footprint.first));
  need(m,"composite RAM macro");
  m->setFixed();
  for (unsigned row=0;row<8;++row) {
   if (!(footprint.second & (1u<<row))) continue;
   for (unsigned freeLanes=0; freeLanes<4; ++freeLanes) {
    Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
    site.mapLUTRAMRelatedCellsToSlots(m,4);site.prepareMemoryFFPacking(true);
    unsigned open=0;
    for(unsigned lane=0;lane<2;++lane) if(freeLanes & (1u<<lane)) open |= MemoryFFRules::slotBit(row,lane);
    site.ffQBlockedMask=65535u ^ open;
    Cluster c(&site);
    const unsigned n=__builtin_popcount(footprint.second);
    for(unsigned i=0;i<n;++i) need(c.tryAddPU(pu("row_logic"+std::to_string(i))),"LUT rows independent of Q reservations");
    auto direct=pu("logic_ff"+std::to_string(row)+"_0");
    auto bypass=pu("external0");
    if(!open) {
     need(!c.tryAddPU(direct) && !c.tryAddPU(bypass),"reserved Q blocks both D sources");
    } else {
     Cluster byD(&c);
     need(byD.tryAddPU(bypass),"external D in either open lane");
     need(c.tryAddPU(direct),"ordinary LUT drives FF in either open lane");
     need(!c.tryAddPU(pu("other_clock")),"incompatible half-slice clock");
     need(c.tryAddPU(bypass)==(freeLanes==3),"second FF needs a second open lane");
     need(!c.tryAddPU(pu("external1")),"no third FF at this row");
     site.setDeterminedClusterInSite(new Cluster(&c));site.finalMapToSlots();
     const auto &slots=site.getSlotMapping();
     auto lut=slots.LUTs[row/4][0][row%4];
     need(lut && lut->getName()=="row_logic"+std::to_string(row),"same-row ordinary LUT identity");
     std::set<PlacementInfo::PlacementUnit*> mapped;
     for(unsigned lane=0; lane<2; ++lane) {
      auto ff=slots.FFs[row/4][lane][row%4];
      need(bool(ff)==bool(freeLanes & (1u<<lane)),"final witness uses only open FF lanes");
      if(ff)mapped.insert(pi.getPlacementUnitByCell(ff));
     }
     need(mapped.count(direct) && mapped.count(bypass)==(freeLanes==3),"FF identities in final mapping");
    }
    ++passed;
   }
  }
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  site.mapLUTRAMRelatedCellsToSlots(m,4);site.prepareMemoryFFPacking(false,true);
  Cluster off(&site);need(!off.tryAddPU(pu("logic")),"disabled memory-site packing");
  site.prepareMemoryFFPacking(true);Cluster c(&site);
  unsigned n=__builtin_popcount(footprint.second);
  for(unsigned i=0;i<n;++i) need(c.tryAddPU(pu("row_logic"+std::to_string(i))),"fill every spare RAM row");
  need(!c.tryAddPU(pu("row_logic"+std::to_string(n))),"do not spill into RAM footprint");
  site.setDeterminedClusterInSite(new Cluster(&c));
  for(auto x:c.getPUs())x->setFixed();
  site.finalMapToSlots();
  for(auto x:c.getPUs())x->setUnfixed();
  auto &slots=site.getSlotMapping();std::set<DesignInfo::DesignCell*> found;
  for(unsigned r=0;r<8;++r) {
   auto lut=slots.LUTs[r/4][0][r%4];
   need(bool(lut)==bool(footprint.second & (1u<<r)) && !slots.LUTs[r/4][1][r%4],"exact complete-row footprint");
   if(lut)need(found.insert(lut).second && c.getSingleLUTs().count(lut),"distinct logic BEL owner");
  }
  ++passed;
 }
 {
  // Two independent FF clocks still need both halves; A-D can retain all four LUTs.
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  auto m=dynamic_cast<PlacementInfo::PlacementMacro*>(pu("RAM32X1D"));
  site.mapLUTRAMRelatedCellsToSlots(m,4);site.prepareMemoryFFPacking(true);
  Cluster c(&site);
  for(unsigned i=0;i<4;++i)need(c.tryAddPU(pu("row_logic"+std::to_string(i))),"four ordinary LUTs");
  need(c.tryAddPU(pu("external0")) && c.tryAddPU(pu("other_clock")),"alternative LUT row for two FF clocks");
  site.setDeterminedClusterInSite(new Cluster(&c));site.finalMapToSlots();
  const auto &slots=site.getSlotMapping();unsigned lutCount=0, halves=0;
  for (unsigned r=0;r<8;++r) {
   auto lut=slots.LUTs[r/4][0][r%4];
   if (lut) {need(r<6,"preserve G/H");++lutCount;}
   for(unsigned l=0;l<2;++l)if(slots.FFs[r/4][l][r%4]) {
    need(r<6,"independent D in spare row only");halves|=1u<<(r/4);
   }
  }
  need(halves==3 && lutCount==4,"two clocks, four LUTs in legal final rows");
  ++passed;
 }
 check("mux","external0",65535u^MemoryFFRules::slotBit(0,0),true,false);
 check("mux","external0",65535u^MemoryFFRules::slotBit(1,0),true,true);
 check("mux","external0",65535u^MemoryFFRules::slotBit(0,1),true,false);
 {
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu("mux"));need(macro,"SRL+MUX owner");
  need(macro->getFixedCellInfoVec().size()==3,"complete MUX footprint");
  macro->setFixed();site.mapLUTRAMRelatedCellsToSlots(macro,4);site.prepareMemoryFFPacking(true);
  pu("external0")->setFixed();Cluster rejected(&site);
  need(!rejected.tryAddPU(pu("external0")),"external fixed FF remains excluded");pu("external0")->setUnfixed();
  Cluster c(&site);
  for(int i=0;i<13;++i)need(c.tryAddPU(pu("external"+std::to_string(i))),"MUX: 13 independent FFs fit");
  need(!c.tryAddPU(pu("external13")),"MUX: select plus two memory Q2 reservations");
  site.setDeterminedClusterInSite(new Cluster(&c));
  for(auto x:c.getPUs())x->setFixed();
  site.finalMapToSlots();
  need(!site.getSlotMapping().FFs[0][0][0],"MUX select AFF stays empty");
  for(auto x:c.getPUs())x->setUnfixed();
  macro->setLocked();site.prepareMemoryFFPacking(true);
  need(!site.acceptsMemoryFFPU(pu("external0")),"locked memory remains excluded");macro->setUnlocked();++passed;
 }
 {
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  site.mapLUTRAMRelatedCellsToSlots(dynamic_cast<PlacementInfo::PlacementMacro*>(pu("srl")),4);site.prepareMemoryFFPacking(true);
  Cluster c(&site);
  // One memory row removes exactly one independent bypass slot: fifteen fit.
  for(int i=0;i<15;++i)need(c.tryAddPU(pu("external"+std::to_string(i))),"15 independent FFs must fit");
  need(!c.tryAddPU(pu("external15")),"16 independent FFs must reject");
  need(c.tryAddPU(pu("srl_local")),"local output may use the remaining Q2 slot");
  site.setDeterminedClusterInSite(new Cluster(&c));site.finalMapToSlots();
  std::string srlLocal="srl_local";need(site.getSlotMapping().FFs[1][1][3]==design.getCell(srlLocal),"memory output at HFF2");++passed;
 }
 {
  // Shared physical site with two separately legalized memory owners.
  Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
  for(std::string name:{"srl16","srl"}){
   auto m=dynamic_cast<PlacementInfo::PlacementMacro*>(pu(name));
   m->getFixedCellInfoVec().clear();m->addFixedCellInfo(design.getCell(name),"SLICEM",name=="srl16"?"D6LUT":"H6LUT");
   m->setFixed();site.mapLUTRAMRelatedCellsToSlots(m,4);
  }
  site.prepareMemoryFFPacking(true);Cluster c(&site);
  for(int i=0;i<14;++i)need(c.tryAddPU(pu("external"+std::to_string(i))),"shared SRL16/SRL32: 14 bypass FFs");
  need(!c.tryAddPU(pu("external14")),"shared SRLs protect occupied input paths");
  need(c.tryAddPU(pu("srl16_local")),"SRL16 local Q still usable");
  site.setDeterminedClusterInSite(new Cluster(&c));site.finalMapToSlots();++passed;
 }
 {
  auto model=dev.getPhysicalBoundaryModel();
  need(model && model->getRegions().size()>1,"die fixture model");
  auto region=model->getRegions().at(model->regionAt(target->X(),target->Y()));
  auto foreign=std::find_if(model->getRegions().begin(),model->getRegions().end(),
                            [&](const auto &r){return r.slr!=region.slr;});
  need(foreign!=model->getRegions().end(),"second die available");
  std::vector<std::pair<float,float>> previous;
  for(auto unit:pi.getPlacementUnits()) {
   previous.emplace_back(unit->X(),unit->Y());
   unit->setAnchorLocationAndForgetTheOriginalOne((region.x0+region.x1)/2,(region.y0+region.y1)/2);
  }
  pu("fanout_ff")->setAnchorLocationAndForgetTheOriginalOne((foreign->x0+foreign->x1)/2,(foreign->y0+foreign->y1)/2);
  PackingDieScope scope(&pi);
  unused.assign(pi.getPlacementUnits().size(),nullptr);
  Site site(&pi,target,3,1,.25,.5,6,10,.4,.02,unused);
  site.mapLUTRAMRelatedCellsToSlots(dynamic_cast<PlacementInfo::PlacementMacro*>(pu("srl")),4);
  site.prepareMemoryFFPacking(true);site.setPackingDieScope(&scope);
  for(auto name:{"srl_local","fanout_ff"}) pu(name)->setAnchorLocationAndForgetTheOriginalOne(target->X(),target->Y());
  site.getNeighborPUs().insert(pu("external0"));
  site.updateStep(true);
  need(site.getNeighborPUs().count(pu("srl_local")) && !site.getNeighborPUs().count(pu("fanout_ff")),
       "same distance, packing-entry die decides which direct FF enters search");
  for(auto unit:pi.getPlacementUnits()) {
   auto pos=previous.at(unit->getId());unit->setAnchorLocationAndForgetTheOriginalOne(pos.first,pos.second);
  }
  ++passed;
 }
 {
  unused.assign(pi.getPlacementUnits().size(), nullptr);
  Site site(&pi,target,3,1,.25,.5,6,10,.4,.02,unused);
  site.mapLUTRAMRelatedCellsToSlots(dynamic_cast<PlacementInfo::PlacementMacro*>(pu("srl")),4);
  site.prepareMemoryFFPacking(true);
  auto distance = [&](std::string n, float dx) {
   pu(n)->setAnchorLocationAndForgetTheOriginalOne(target->X()+dx,target->Y());
  };
  for (auto n:{"srl_local","different_ce_ff","fanout_ff","srl16_local"}) distance(n,5);
  distance("srl_local",.25);
  Cluster single(&site);
  need(single.tryAddPU(pu("srl_local")),"single direct FF admitted");
  single.updateScoreInSite();
  need(single.getScoreInSite()==single.baseScoreInSite()+5,"single row bonus");
  distance("srl_local",5);
  need(single.getScoreInSite()==single.baseScoreInSite(),"far single has no bid preference");
  distance("srl_local",.25);
  site.getNeighborPUs().insert(pu("external0"));
  site.updateStep(true);
  need(site.getNeighborPUs().count(pu("srl_local")) && !site.getNeighborPUs().count(pu("fanout_ff")),
       "direct neighbor found within current radius even with no refill");
  site.refreshAuctionBidScore();
  need(site.hasValidPQTop() && site.getAuctionBidScore(pu("srl_local"))==site.getAuctionBidScore()+5,
       "direct single competes for its SRL site");
  distance("fanout_ff",.25);
  need(single.tryAddPU(pu("fanout_ff")),"second load may fit in ordinary lane");
  need(single.directSRLRowsFor(pu("fanout_ff"))+single.directSRLRowsFor(pu("srl_local"))==1,
       "two consumers of the same Q share one dedicated slot");
  auto firstWins=single.directSRLRowsFor(pu("srl_local"))!=0;
  single.removePUToConstructDetCluster(pu(firstWins?"srl_local":"fanout_ff"));
  need(single.directSRLRowsFor(pu(firstWins?"fanout_ff":"srl_local"))==1,
       "remaining load gets the vacated dedicated lane");
  Cluster mixed(&site);
  distance("different_ce_ff",.25);
  need(mixed.tryAddPU(pu("external0")) && mixed.tryAddPU(pu("different_ce_ff")),
       "two legal control groups admitted");
  need(mixed.directSRLRowsFor(pu("different_ce_ff"))==1,"control bank ordering honors SRL row");
  site.setDeterminedClusterInSite(new Cluster(&mixed));
  site.finalMapToSlots();
  std::string direct="different_ce_ff";
  need(site.getSlotMapping().FFs[1][1][3]==design.getCell(direct),"final SRLC32E local HFF2");
  for (auto n:{"srl_local","different_ce_ff","fanout_ff","srl16_local"}) distance(n,5);
  ++passed;
 }
 {
  // Build the same two-member PU as IncrementalBELPacker and refresh the
  // placement nets, so trial admission and scoring see the pair as a unit.
  using Macro=PlacementInfo::PlacementMacro;
  auto checkPair=[&](std::string mem,std::string a,std::string b,bool accepted,float bonus,
                     unsigned blocked=0,bool enabled=true) {
   auto m=dynamic_cast<Macro*>(pu(mem));
   need(m,"missing RAM/SRL macro");
   auto first=design.getCell(a),second=design.getCell(b);
   auto oldFirst=pu(a),oldSecond=pu(b);
   auto pair=new Macro(a,pi.getPlacementUnits().size(),Macro::PlacementMacroType_FFFFPair);
   pair->addOccupiedSite(0.0,0.0625);
   pair->setWeight(2);
   pair->setAnchorLocationAndForgetTheOriginalOne(target->X(),target->Y());
   for(auto cell:{first,second}) {
    pair->addCell(cell,cell->getCellType(),0,0);
    pi.getCellId2PlacementUnit()[cell->getCellId()]=pair;
    pi.getCellId2PlacementUnitVec()[cell->getCellId()]=pair;
   }
   pi.getPlacementMacros().push_back(pair);
   pi.getPlacementUnits().push_back(pair);
   pi.reloadNets();
   unused.assign(pi.getPlacementUnits().size(),nullptr);
   Site site(&pi,target,3,1,.25,.5,6,10,.4,.02,unused);
   m->setFixed();site.mapLUTRAMRelatedCellsToSlots(m,4);
   if (b=="srl16_local") site.mapLUTRAMRelatedCellsToSlots(dynamic_cast<Macro*>(pu("srl16")),4);
   site.prepareMemoryFFPacking(enabled);site.ffQBlockedMask=blocked;
   Cluster c(&site);
   Cluster::ScoreExtensionState seed;
   c.updateScoreInSite();c.prepareScoreExtension(seed);
   std::cerr<<"pair case "<<mem<<" "<<a<<" "<<b<<" expected "<<accepted<<'\n';
   if (accepted) {
    pair->setLocked();
    need(!c.tryAddPU(pair) && c.getPUs().empty(),"locked pair admitted");
    pair->setUnlocked();pair->setFixed();
    need(!c.tryAddPU(pair) && c.getPUs().empty(),"external fixed pair admitted");
    pair->setUnfixed();
   }
   need(c.tryAddPU(pair)==accepted,"whole-pair admission");
   if(!accepted) {
    need(c.getPUs().empty() && c.getScoreInSite()==c.baseScoreInSite(),"failed trial mutated seed");
   } else {
    c.updateScoreInSite();
    float full=c.getScoreInSite();
    need(full==c.baseScoreInSite()+bonus,"wrong site reward");
    c.incrementalUpdateScoreInSite(pair,seed);
    need(c.getScoreInSite()==full,"incremental versus full reward");
    site.getNeighborPUs().insert(pair);
    site.updateStep(true);
    need(site.hasValidPQTop() && site.getPriorityQueueTop()->getScoreInSite()==full,"live candidate queue");
    site.refreshAuctionBidScore();
    need(site.getAuctionBidScore()==c.baseScoreInSite(),"pair reward leaked into common site bid");
    need(site.getAuctionBidScore(pair)==site.getAuctionBidScore()+bonus,"wrong pair bid");
    need(site.getAuctionBidScore(oldFirst)==site.getAuctionBidScore(),"bonus leaked to single FF");
    Cluster copy(&c);
    need(copy.getScoreInSite()==full,"copied reward");
    copy.removePUToConstructDetCluster(pair);
    need(copy.getScoreInSite()==copy.baseScoreInSite(),"removed reward");
    need(copy.tryAddPU(pair) && copy.getScoreInSite()==full,"reinserted reward");
    site.setDeterminedClusterInSite(new Cluster(&copy));pair->setFixed();
    site.finalMapToSlots();pair->setUnfixed();
    std::set<DesignInfo::DesignCell*> mapped;
    for(unsigned h=0;h<2;++h)for(unsigned l=0;l<2;++l)for(unsigned row=0;row<4;++row) {
     if(auto ff=site.getSlotMapping().FFs[h][l][row]){
      need(!(blocked&MemoryFFRules::slotBit(4*h+row,l)),"blocked final BEL");
      need(mapped.insert(ff).second,"duplicate FF in two BELs");
     }
    }
    need(mapped==std::set<DesignInfo::DesignCell*>{first,second},"partial final pair mapping");
    if(a=="shared_write0" && b=="shared_write1"){
     need(site.getSlotMapping().FFs[0][1][0]==first,"DIA[0] must use AFF2");
     need(site.getSlotMapping().FFs[0][0][0]==second,"DIA[1] must use AFF");
    }
   }
   pi.getCellId2PlacementUnit()[first->getCellId()]=oldFirst;
   pi.getCellId2PlacementUnitVec()[first->getCellId()]=oldFirst;
   pi.getCellId2PlacementUnit()[second->getCellId()]=oldSecond;
   pi.getCellId2PlacementUnitVec()[second->getCellId()]=oldSecond;
   ++passed;
  };
  for(std::string kind:{"RAM32M16","RAM32M"})
   checkPair(kind,kind+"_DOA0",kind+"_DOA1",true,5.0f);
  for(std::string kind:{"RAM64M8","RAM64M"})
   checkPair(kind,kind+"_DOA0",kind+"_DOB0",true,5.0f);
  for(std::string kind:{"RAM32X1D","RAM64X1D"})
   checkPair(kind,kind+"_DPO0",kind+"_SPO0",true,5.0f);
  checkPair("RAM256X1S","external0","external1",true,0.0f);
  checkPair("RAM32X1D","RAM32X1D_DPO0","writer",true,5.0f); // read and reverse Q->RAM.D
  checkPair("RAM32X1D","RAM32X1D_DPO0","external0",true,2.5f);
  checkPair("RAM32X1D","external0","external1",true,0.0f);
  checkPair("RAM32X1D","external0","RAM32X1D_DPO0",true,2.5f); // single read in second member
  checkPair("RAM32X1D","external0","writer",true,2.5f); // single reverse Q-to-RAM input
  checkPair("srl","srl_local","external0",true,5.0f);
  checkPair("srl","srl_local","srl16_local",true,10.0f);
  checkPair("srl","srl_local","fanout_ff",true,5.0f);
  checkPair("srl","srl_local","external0",true,0.0f,MemoryFFRules::slotBit(7,1));
  const unsigned ghPrimaryOnly=65535u^(MemoryFFRules::slotBit(6,0)|MemoryFFRules::slotBit(7,0));
  checkPair("RAM32X1D","external0","external1",false,0.0f,ghPrimaryOnly);
  checkPair("RAM32X1D","RAM32X1D_DPO0","RAM32X1D_SPO0",true,5.0f,ghPrimaryOnly);
  checkPair("RAM256X1S","external0","external1",false,0.0f,
            65535u^(MemoryFFRules::slotBit(4,0)|MemoryFFRules::slotBit(4,1)));
  checkPair("RAM32M16","external0","external1",false,0.0f);
  checkPair("RAM32M16","shared_write0","shared_write1",true,0.0f);
  checkPair("RAM32M16","RAM32M16_DOA0","shared_write0",false,0.0f,65535u^(MemoryFFRules::slotBit(0,0)|MemoryFFRules::slotBit(0,1))); // exclusive whole-site profiles, two slots only

  checkPair("RAM32X1D","RAM32X1D_DPO0","writer",false,0.0f,65535u^1u);
  checkPair("RAM32X1D","external0","other_clock",false,0.0f);
  checkPair("RAM32X1D","RAM32X1D_DPO0","writer",false,0.0f,0,false);
 }
 std::cout<<"PASS memory FF packing integration cases="<<passed<<" (real input, trial, copy/remove, control banks, capacity and final BEL mapping)\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
