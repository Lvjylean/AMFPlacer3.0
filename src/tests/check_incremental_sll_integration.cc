#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <fstream>
#include <iostream>
static void require(bool ok,const char*msg){if(!ok)throw std::runtime_error(msg);}
int main(int argc,char**argv){try {
 require(argc==3,"usage: checkIncrementalSllIntegration config NEW_OUTPUT");
 auto cfg=parseJSONFile(argv[1]);std::filesystem::path out=argv[2];
 require(std::filesystem::create_directory(out),"refuse overwrite");
 cfg["SllPlanningReportDirectory"]=(out/"planning").string();
 DeviceInfo dev(cfg,cfg.at("device"));
 // A synthetic seven-FF netlist: one internal fanout-2 net, two interface nets.
 DesignInfo design(cfg,&dev);require(design.getCells().size()==7,"seven-cell fixture");
 std::ofstream allowed(out/"allowed.txt"),blocked(out/"blocked.txt"),fixed(out/"fixed.tsv");
 fixed<<"cell\tsite\tbel\n";
 std::vector<DeviceInfo::DeviceSite*> slices;
 for(auto site:dev.getSites())if(site->getName().rfind("SLICE_",0)==0)slices.push_back(site);
 require(slices.size()>2,"device slice supply");
 fixed<<"static_source\t"<<slices[0]->getName()<<"\tAFF\n";
 fixed<<"static_sink\t"<<slices[1]->getName()<<"\tAFF\n";fixed.close();
 for(size_t i=2;i<slices.size();++i)allowed<<slices[i]->getName()<<"\n";allowed.close();blocked.close();
 cfg["IncrementalCompilation"]="true";cfg.erase("DFX placement mode");
 cfg["DFX active partition"]="rm";cfg["DFX input scope"]="rm-boundary-cone-v1";
 cfg["DFX allowed sites file"]=(out/"allowed.txt").string();
 cfg["DFX blocked sites file"]=(out/"blocked.txt").string();
 cfg["DFX static placement file"]=(out/"fixed.tsv").string();
 cfg["SllIncrementalScope"]="internal-active-nets";
 cfg["SllReservedResourceFile"]=cfg.at("fixture reservations");
 PlacementInfo pi(&design,&dev,cfg);
 for(auto c:design.getCells()){
  auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);
  const auto name=c->getName();
  u->setAnchorLocationAndForgetTheOriginalOne(50,(name=="rm/remote"||name=="rm/input"||name=="static_sink")?510:270);
  if(name.rfind("static_",0)==0)u->setFixed();
  pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);
  pi.getCellId2PlacementUnit()[c->getCellId()]=u;
 }
 pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);
 pi.createGridBins(5,5);pi.getTimingInfo()->buildSimpleTimingGraph();
 PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();
 require(pi.getIncrementalContext().enabled,"real incremental context enabled");
 require(pi.sllPlanner->model().plans.size()==1,"only wholly active internal net planned");
 require(pi.sllPlanner->variableCount()==1,"fanout shares one virtual crossing");
 const auto&plan=pi.sllPlanner->model().plans.front();
 require(plan.net.source.name=="rm/source/Q" && plan.net.sinks.size()==2,"exact active tree");
 std::vector<std::pair<float,float>> before;
 for(auto u:pi.getPlacementUnits())before.push_back({u->X(),u->Y()});
 WirelengthOptimizer wl(&pi,cfg,false);
 wl.GlobalPlacementQPSolve(.02,true,true,false,true,false,10,nullptr,false);
 for(auto u:pi.getPlacementUnits())if(u->isFixed())
  require(before.at(u->getId())==std::make_pair(u->X(),u->Y()),"QP moved frozen static PU");
 // Restore controlled coordinates after the actual production solve.
 for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(before[u->getId()].first,before[u->getId()].second);
 pi.sllPlanner->exportPlan((out/"sll-plan.tcl").string());
 require(pi.sllPlanner->model().plans.size()==1,"export scope preserved");
 for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings)
  require(!amf_sll_plan::Model::conflicts(pi.sllPlanner->model().channels[e.channel],pi.sllPlanner->model().reserved),"reserved channel reused");
 for(auto u:pi.getPlacementUnits())if(u->getName()=="rm/remote")u->setAnchorLocationAndForgetTheOriginalOne(50,270);
 pi.sllPlanner->refresh();require(pi.sllPlanner->variableCount()==0,"local internal net leaves no stale SLL variables");
 require(pi.sllPlanner->model().plans.empty(),"interface nets must not acquire a synthetic SLL plan");
 std::cout<<"PASS incremental active scope, frozen PU immutability, shared QP crossing, reservation, export, lifecycle\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
