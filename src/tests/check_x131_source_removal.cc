#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "MacroLocalRoutes.h"
#include "simpleJSON.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <omp.h>
static void need(bool x,const char*s){if(!x)throw std::runtime_error(s);}
using Site=ParallelCLBPacker::PackingCLBSite;using Cluster=Site::PackingCLBCluster;
static std::string sig(Cluster*c){std::ostringstream s;if(!c)return "null";s<<"mux="<<c->getNumMuxes()<<";";
 for(auto p:c->getPUs())s<<"P"<<p->getId()<<",";
 for(auto p:c->getSingleLUTs())s<<"L"<<p->getCellId()<<",";
 for(auto p:c->getPairedLUTs())s<<"l"<<p.first->getCellId()<<":"<<p.second->getCellId()<<",";
 for(auto cs:c->getFFControlSets()){s<<"B"<<cs.getCSId()<<":";for(auto f:cs.getFFs())s<<f->getCellId()<<",";}return s.str();}
int main(int argc,char**argv){try{
 need(argc==2,"fixture");omp_set_num_threads(1);std::string root=argv[1];auto cfg=parseJSONFile(root+"/config.json");
 DeviceInfo dev(cfg,cfg.at("device"));DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
 Site::SiteBELMapping historical;std::set<DesignInfo::DesignCell*> members;std::ifstream f(root+"/slots.tsv");std::string name,siteName,bel;
 while(f>>name>>siteName>>bel){auto c=design.getCell(name);members.insert(c);
 if(bel.find("F7MUX_")==0){int row=bel[6]-'A';historical.MuxF7[row/4][row%4/2]=c;}
 else{int row=bel[0]-'A';if(bel.find("LUT")!=std::string::npos)historical.LUTs[row/4][bel[1]=='5'][row%4]=c;
 else historical.FFs[row/4][bel.back()=='2'][row%4]=c;}}
 std::cout<<"HISTORICAL exact_bel_guard="<<MacroLocalRoutes::compatible(historical)<<" real="<<members.size()<<"\n";
 for(auto c:members){auto m=historical;for(int h=0;h<2;++h){for(int k=0;k<2;++k)if(m.MuxF7[h][k]==c)m.MuxF7[h][k]=nullptr;
 for(int l=0;l<2;++l)for(int k=0;k<4;++k){if(m.LUTs[h][l][k]==c)m.LUTs[h][l][k]=nullptr;if(m.FFs[h][l][k]==c)m.FFs[h][l][k]=nullptr;}}
 std::cout<<"REMOVE_CELL "<<c->getName()<<" "<<c->getCellType()<<" compatible="<<MacroLocalRoutes::compatible(m)<<"\n";}
 InitialPacker initial(&design,&dev,&pi,cfg);initial.pack();pi.buildSimpleTimingGraph();
 DeviceInfo::DeviceSite* target=nullptr;for(auto s:dev.getSites())if(s->getName()=="SLICE_X131Y42")target=s;need(target,"site");
 std::vector<Site*> unused(pi.getPlacementUnits().size(),nullptr);Site site(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
 std::set<PlacementInfo::PlacementUnit*,Packing_PUcompare> pus;
 for(auto c:members)pus.insert(pi.getPlacementUnitByCell(c));
 Cluster cluster(&site);bool all=true;
 for(auto p:pus){bool ok=cluster.tryAddPU(p);std::cout<<"ADD_PU "<<p->getId()<<" "<<p->getName()<<" ok="<<ok<<"\n";all&=ok;}
 std::cout<<"ALL_ADMITTED "<<all<<"\n";if(!all)return 2;
 site.setDeterminedClusterInSite(new Cluster(&cluster));std::string before=sig(site.getDeterminedClusterInSite());
 bool legal=site.localCandidateAllows(site.getDeterminedClusterInSite());std::cout<<"BASE candidate="<<legal<<"\n";
 site.finalMapToSlots();std::cout<<"BASE final_map=1 guard="<<MacroLocalRoutes::compatible(site.getSlotMapping())<<"\n";
 auto baseline=new Cluster(site.getDeterminedClusterInSite());unsigned preserved=0,badRemove=0;
 for(auto p:pus){
 site.setDeterminedClusterInSite(new Cluster(baseline));site.recordGlobalBestPacking();auto saved=sig(site.getDeterminedClusterInSite());
 bool preflight=site.canRelocateFrom(p);
 need(sig(site.getDeterminedClusterInSite())==saved,"source preflight mutated live membership");
 site.getDeterminedClusterInSite()->removePUToConstructDetCluster(p);
 bool accepted=site.localCandidateAllows(site.getDeterminedClusterInSite());need(preflight==accepted,"source gate disagrees with full trial");bool finalok=true;std::string error;
 try{site.finalMapToSlots();}catch(const std::exception&e){finalok=false;error=e.what();}
 std::cout<<"REMOVE_PU "<<p->getId()<<" "<<p->getName()<<" source_guard="<<accepted<<" final_map="<<finalok<<" error="<<error<<"\n";badRemove+=!accepted;
 site.recoverGlobalBestPacking();need(sig(site.getDeterminedClusterInSite())==saved,"checkpoint changed cluster signature");
 need(site.localCandidateAllows(site.getDeterminedClusterInSite()),"restored legal cluster lost witness");site.finalMapToSlots();++preserved;
 }
 unsigned total=0,bad=0,explicitBad=0,restoredBad=0;
 std::vector<PlacementInfo::PlacementUnit*> pv(pus.begin(),pus.end());
 for(unsigned mask=0;mask<(1u<<pv.size());++mask){
  auto cluster=new Cluster(baseline);site.setDeterminedClusterInSite(cluster);
  for(unsigned k=0;k<pv.size();++k)if(!(mask&(1u<<k)))cluster->removePUToConstructDetCluster(pv[k]);
  if(!cluster->getNumMuxes())continue;
  auto explicitMap=historical;
  for(auto c:members){auto p=pi.getPlacementUnitByCell(c);unsigned k=std::find(pv.begin(),pv.end(),p)-pv.begin();
   if(mask&(1u<<k))continue;
   for(int h=0;h<2;++h){for(int i=0;i<2;++i)if(explicitMap.MuxF7[h][i]==c)explicitMap.MuxF7[h][i]=nullptr;
    for(int l=0;l<2;++l)for(int i=0;i<4;++i){if(explicitMap.LUTs[h][l][i]==c)explicitMap.LUTs[h][l][i]=nullptr;if(explicitMap.FFs[h][l][i]==c)explicitMap.FFs[h][l][i]=nullptr;}}}
  bool explicitOk=MacroLocalRoutes::compatible(explicitMap);explicitBad+=!explicitOk;
  bool gate=site.localCandidateAllows(cluster);auto state=sig(cluster);
  site.recordGlobalBestPacking();site.recoverGlobalBestPacking();
  need(sig(site.getDeterminedClusterInSite())==state,"subset checkpoint copy changed membership/control sets");
  bool restoredGate=site.localCandidateAllows(site.getDeterminedClusterInSite());need(gate==restoredGate,"restore changed gate");
  bool finalok=true;try{site.finalMapToSlots();}catch(const std::exception&e){finalok=false;}
  if(!gate||!finalok){
   ++bad;restoredBad+=!restoredGate;
   std::cout<<"SUBSET_BAD mask="<<mask<<" explicit="<<explicitOk<<" gate="<<gate<<" restored_gate="<<restoredGate<<" final="<<finalok<<" state="<<state<<"\n";
  }++total;
 }
 need(total==1536 && !bad && !explicitBad && !restoredBad && !badRemove && preserved==11,"subset or removal regression");
 // Block every FF output on this test site. Removing an ordinary PU must
 // reject the remaining real MUX FFs before publishing either site.
 site.setDeterminedClusterInSite(new Cluster(baseline));
 auto signature=sig(site.getDeterminedClusterInSite());
 site.ffQBlockedMask=0xffff;
 need(!site.canRelocateFrom(*pus.begin()),"infeasible source accepted");
 need(sig(site.getDeterminedClusterInSite())==signature,"rejected source mutated");
 site.ffQBlockedMask=0;
 need(site.canRelocateFrom(*pus.begin()),"legal source still rejected after restoring access");
 // Test the untouched MUX row mapping through the same physical checks.
 Site raw(&pi,target,3,10,.25,.5,6,10,.4,.02,unused);
 raw.setDeterminedClusterInSite(new Cluster(baseline));
 for(int order=0;order<24;++order) raw.greedyMapMuxForCommonLUTFFInSite(order,false);
 raw.requireLegalSlotMapping("test pre-swap");
 raw.finalMapToSlots();
 need(MacroLocalRoutes::compatible(raw.getSlotMapping()),"pre-swap row mapping lost");
 auto swapped=historical;std::swap(swapped.LUTs[0][0][0],swapped.LUTs[0][0][1]);
 need(!MacroLocalRoutes::compatible(swapped),"known F7 input-wire conflict accepted");
 std::cout<<"SUBSETS total="<<total<<" bad="<<bad<<" explicit_bad="<<explicitBad<<" restored_bad="<<restoredBad<<"\n";

 std::cout<<"PASS global_restores="<<preserved<<" removal_rejections="<<badRemove<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"TEST_EXCEPTION "<<e.what()<<"\n";return 1;}}
