#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
int main(int argc,char**argv){try {
    check(argc==3,"usage: checkSllPlannerIntegration config.json NEW_OUTPUT");
    std::filesystem::path root=argv[2];check(std::filesystem::create_directory(root),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningReportDirectory"]=(root/"diagnostics").string();cfg["SllPlanningAllowSplit"]="false";
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    for(auto cell:design.getCells())if(!cell->isVirtualCell()){auto u=new PlacementInfo::PlacementUnpackedCell(cell->getName(),pi.getPlacementUnits().size(),cell);pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[cell->getCellId()]=u;}
    check(pi.getPlacementUnits().size()==4,"four-cell production fixture");
    double ys[]={270,510,511,750};for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50+u->getId(),ys[u->getId()]);
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();
    auto count=design.getCells().size();check(!pi.sllPlanner->model().plans.empty(),"crossing plans exist");
    std::vector<Eigen::Triplet<float>> trip;std::vector<float> diag(4,0);Eigen::VectorXd rhs=Eigen::VectorXd::Zero(4);
    for(const auto&p:pi.sllPlanner->model().plans)pi.sllPlanner->addWireModel(p.net.id,trip,diag,rhs,1,.4,true);
    bool positive=false;for(auto d:diag){check(std::isfinite(d)&&d>=0,"finite QP diagonal");positive|=d>0;}check(positive,"virtual endpoints add real-cell forces");
    auto plan=pi.sllPlanner->model().plans.front();auto sink=plan.net.sinks.front();double d=0;
    check(pi.sllPlanner->pinDelay(plan.net.source.id,sink.id,plan.net.source.p,sink.p,d),"exact pin path lookup");
    auto sourcePin=design.getPins().at(plan.net.source.id),sinkPin=design.getPins().at(sink.id);
    double actual=timing.getDelayForPins(sourcePin,sinkPin,plan.net.source.p.x,plan.net.source.p.y,sink.p.x,sink.p.y);
    check(std::abs(actual-d)<1e-5,"STA uses selected SLL path");
    double expected=0;auto cur=plan.net.source.p;
    for(int k:plan.paths.at(sink.id)){auto c=pi.sllPlanner->model().channels[plan.crossings[k].channel];expected+=timing.getDelayByModel_conservative(cur.x,cur.y,c.in.x,c.in.y,true)+c.slowMax;cur=c.out;}
    expected+=timing.getDelayByModel_conservative(cur.x,cur.y,sink.p.x,sink.p.y,true);
    check(std::abs(actual-expected)<1e-5,"no legacy 1.5ns addition");
    pi.sllPlanner->exportPlan((root/"sll-plan.tcl").string());
    // Execute the production QP, including the real-cell writeback, rather than
    // validating only an isolated mathematical helper.
    cfg["SllQuantityEnable"]="false";cfg["SllQuantityDiagnostics"]="false";cfg["SllDynamicTauEnable"]="false";cfg["SllShortSpanEnable"]="false";
    const float before=pi.getPlacementUnits()[0]->X();
    {WirelengthOptimizer wl(&pi,cfg,false);wl.GlobalPlacementQPSolve(.02,true,true,false,true,false,-10,nullptr,false);}
    check(std::isfinite(pi.getPlacementUnits()[0]->X()),"production QP writes finite coordinates");
    bool moved=false;for(auto u:pi.getPlacementUnits())moved|=std::abs(u->X()-(50+u->getId()))>1e-6;
    check(moved || std::abs(pi.getPlacementUnits()[0]->X()-before)>1e-6,"real cells respond to virtual endpoints");check(design.getCells().size()==count,"no netlist cell insertion");
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(u->X(),270);
    pi.sllPlanner->refresh();check(pi.sllPlanner->model().plans.empty(),"production lifecycle deletes crossings");
    std::cout<<"PASS production planner/QP/timing/export/lifecycle; initial_model_delay_ns="<<actual<<"\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
