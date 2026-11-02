#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char**argv){try {
    check(argc==4,"usage: checkSllDeferredStart CONFIG NEW_OUTPUT absent|false|true");
    const std::string mode=argv[3];check(mode=="absent"||mode=="false"||mode=="true","mode");
    const bool deferred=mode=="true";
    std::filesystem::path root=argv[2];check(std::filesystem::create_directory(root),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);
    if(mode=="absent")cfg.erase("SllDeferredUntilFirstSpread");else cfg["SllDeferredUntilFirstSpread"]=mode;
    cfg["SllPlanningReportDirectory"]=(root/"planning").string();
    cfg["SllReportDirectory"]=(root/"quantity").string();
    cfg["SllTraceNetList"]=(root/"sample.txt").string();
    cfg["SllPlanningAllowSplit"]="false";cfg["SllJointPlacementEnable"]="true";
    cfg["SllQuantityEnable"]="true";cfg["SllQuantityDiagnostics"]="true";
    cfg["SllDynamicTauEnable"]="true";cfg["SllShortSpanEnable"]="true";
    cfg["DetailedDiagnosticsEnable"]="true";
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    for(auto c:design.getCells())if(!c->isVirtualCell()){
        auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);
        pi.getCellId2PlacementUnit()[c->getCellId()]=u;
    }
    check(pi.getPlacementUnits().size()==4,"four-register fixture");
    for(auto u:pi.getPlacementUnits()){
        u->setAnchorLocationAndForgetTheOriginalOne(50,u->getId()%2?510:270);u->setFixed();
    }
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);
    pi.createGridBins(5,5);pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);auto&planner=*pi.sllPlanner;
    auto noPlan=[&]{
        planner.refresh();check(!planner.active()&&!planner.jointPlacement(),"deferred planner inactive");
        check(planner.variableCount()==0&&planner.model().plans.empty()&&planner.model().previous.empty(),"no early allocation or retained tiles");
        for(auto net:design.getNets())check(!planner.handles(net->getElementIdInType()),"ordinary wire model before activation");
        double d=0;check(!planner.pinDelay(0,1,{50,270},{50,510},d),"ordinary pin delay fallback");
        check(!planner.cellDelay(0,1,{50,270},{50,510},d),"ordinary cell delay fallback");
    };
    if(deferred){
        noPlan();bool rejected=false;
        try{planner.exportPlan((root/"too_early.tcl").string(),false);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"finalization must reject activation never reached");
    } else {planner.refresh();check(planner.active()&&planner.variableCount()==3,"legacy initial allocation");}
    WirelengthOptimizer wl(&pi,cfg,false);
    for(int j=0;j<6;++j){
        wl.recordSllStage("initial",0,j,"before_qp");
        wl.GlobalPlacementQPSolve(.0002,j==0,false);
        if(deferred)noPlan();
    }
    wl.recordSllStage("slr",1,0,"begin");
    for(int j=0;j<2;++j){wl.GlobalPlacementQPSolve(.0002,j==0,true);if(deferred)noPlan();}
    // Model a completed spreading pass: activation must use these new coordinates.
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(100,u->Y());
    pi.updateElementBinGrid();planner.notifyGlobalSpreadCompleted(1);
    if(deferred){
        noPlan();
        SllPlanner::TimingBatch batch(&planner,SllPlanner::TimingCoordinates::PlacementPins);
        bool rejected=false;try{planner.beginQp();}catch(const std::logic_error&){rejected=true;}
        check(rejected,"activation rejected during immutable timing batch");
    }
    wl.recordSllStage("slr",1,0,"after_legalization");
    wl.recordSllStage("slr",2,1,"begin");
    wl.GlobalPlacementQPSolve(.0002,true,true);
    check(planner.active()&&planner.jointPlacement()&&planner.variableCount()==3,"next QP activates joint variables");
    for(const auto&p:planner.model().plans)check(p.net.source.p.x==100,"fresh post-spread source coordinates");
    auto previous=planner.model().previous;planner.notifyGlobalSpreadCompleted(2);planner.refresh();
    check(planner.model().previous==previous,"later notification does not reseed");
    for(auto u:pi.getPlacementUnits())check(u->X()==100&&u->Y()==(u->getId()%2?510:270),"fixed real units preserved");
    const auto plan=root/"disabled_constraints.tcl";planner.exportPlan(plan.string(),false);
    check(!std::filesystem::exists(plan),"no route constraint export");
    check(std::filesystem::exists(plan.string()+".final-capacity.tsv"),"final capacity evidence retained");
    check(design.getCells().size()==4,"no logical cells added");
    std::cout<<"PASS deferred-start mode="<<mode<<" before=8 next=9 crossings="<<planner.variableCount()<<std::endl;
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
