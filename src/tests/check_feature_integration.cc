#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "SllRunDiagnostics.h"
#include "GlobalPlacer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){try{
    check(argc==3,"usage: checkFeatureIntegration fixture NEW_OUTPUT");
    std::filesystem::path out=argv[2];check(std::filesystem::create_directory(out),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningEnable"]="true";cfg["SllJointPlacementEnable"]="true";
    cfg["SllCapacityOptimizationEnable"]="false";cfg["DetailedDiagnosticsEnable"]="false";
    cfg["SllPlanningAllowTransientOverflow"]="true"; // Off child overrides old soft-capacity settings.
    cfg["SllTimingCorrectionEnable"]="true";cfg["SllRouteConstraintEnable"]="false";
    cfg["SllQuantityEnable"]="true";cfg["SllDynamicTauEnable"]="true";
    cfg["dumpDirectory"]=out.string();cfg["SllReportDirectory"]=(out/"quantity").string();
    DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo pi(&design,&device,cfg);
    for(auto c:design.getCells())if(!c->isVirtualCell()){
        auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[c->getCellId()]=u;
    }
    check(pi.getPlacementUnits().size()==4,"four-register fixture");
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,u->getId()%2?510:270);
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);pi.createGridBins(5,5);pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();
    size_t n=4+pi.sllPlanner->variableCount();check(n>4,"virtual cells survive capacity off");
    std::vector<float> diag(n,0);Eigen::VectorXd rhs=Eigen::VectorXd::Zero(n);
    pi.sllPlanner->prepareDensityTargets();pi.sllPlanner->addDensityModel(diag,rhs,true);
    check(rhs.isZero()&&std::all_of(diag.begin(),diag.end(),[](float x){return x==0;}),"no density anchor with capacity off");
    std::vector<Eigen::Triplet<float>> trip;
    for(const auto&plan:pi.sllPlanner->model().plans)pi.sllPlanner->addWireModel(plan.net.id,trip,diag,rhs,1,.4,true);
    bool coupled=false;for(const auto&t:trip)coupled|=t.row()<4&&t.col()>=4&&t.value()<0;
    check(coupled,"wire QP still couples real and virtual cells");
    size_t checked=0;
    for(const auto&plan:pi.sllPlanner->model().plans)for(const auto&s:plan.net.sinks)if(!plan.paths.at(s.id).empty()){
        const auto a=plan.net.source.p,b=s.p;auto source=design.getPins().at(plan.net.source.id),sink=design.getPins().at(s.id);
        double planned=0;check(pi.sllPlanner->pinDelay(plan.net.source.id,s.id,a,b,planned),"planned delay exists");
        check(std::abs(timing.getDelayForPins(source,sink,a.x,a.y,b.x,b.y)-planned)<1e-5,"timing on uses this plan");
        cfg["SllTimingCorrectionEnable"]="false";
        double old=timing.getDelayByModel_conservative(a.x,a.y,b.x,b.y);
        check(std::abs(timing.getDelayForPins(source,sink,a.x,a.y,b.x,b.y)-old)<1e-5,"timing off uses ordinary model");
        cfg["SllTimingCorrectionEnable"]="true";++checked;
    }
    check(checked>0,"crossing timing exercised");
    // Diagnostics must not alter either axis of the QP, including the quantity contribution.
    std::vector<Eigen::VectorXd> outputs;
    for(bool detailed:{true,false}) {
        auto c=cfg;c["DetailedDiagnosticsEnable"]=detailed?"true":"false";
        c["SllReportDirectory"]=(out/(detailed?"verbose":"light")).string();
        c["SllTraceNetList"]=(out/(detailed?"verbose-sample":"light-sample")).string();
        SllRunDiagnostics diagnostics(&pi,c,.4);diagnostics.stage("ordinary",1,0,"begin");
        QPSolverWrapper x(true,false,-.5,959.5,n,false),y(true,false,-.5,959.5,n,false);
        for(auto q:{&x,&y}){q->solverData.objectiveMatrixDiag.assign(n,2);q->solverData.objectiveVector.setZero();}
        diagnostics.prepare(x,y,1,10);outputs.push_back(y.solverData.objectiveVector);
        diagnostics.stage("ordinary",1,0,"after_legalization");
    }
    check((outputs[0].array()==outputs[1].array()).all(),"detailed off preserves quantity gradient exactly");
    check(!std::filesystem::exists(out/"light/qp.tsv")&&!std::filesystem::exists(out/"light-sample"),"no detailed files or sample selection");
    cfg["SllQuantityEnable"]="false";cfg["SllReportDirectory"]=(out/"no-quantity").string();
    cfg["SllTauPolicy"]="periodic";cfg["SllTauCompletionRequired"]="true";
    {SllRunDiagnostics d(&pi,cfg,.4);d.stage("ordinary",1,0,"begin");
     QPSolverWrapper x(true,false,-.5,959.5,n,false),y(true,false,-.5,959.5,n,false);
     for(auto q:{&x,&y}){q->solverData.objectiveMatrixDiag.assign(n,2);q->solverData.objectiveVector.setZero();}
     d.prepare(x,y,1,10);check(y.solverData.objectiveVector.isZero(),"quantity off does not inject linear term");d.stage("ordinary",1,0,"after_legalization");check(d.readyForPacking(),"quantity off cannot extend GP through a diagnostic tau schedule");}
    pi.sllPlanner->exportPlan((out/"disabled-plan.tcl").string(),false);
    check(!std::filesystem::exists(out/"disabled-plan.tcl"),"route off emits no constraint");
    for(const auto&plan:pi.sllPlanner->model().plans)for(const auto&e:plan.crossings)check(e.channel>=0,"final exact channels still legalized");
    cfg["TimingDrivenPlacementEnable"]="false";
    auto&slack=timing.getPUId2Slack(true);check(std::all_of(slack.begin(),slack.end(),[](float x){return x==0;}),"no slack priority when timing off");
    cfg["IterativeMacroLegalizationEnable"]="false";
    cfg["SllDynamicTauEnable"]="false";cfg["SllShortSpanEnable"]="false";cfg["SllQuantityDiagnostics"]="false";
    cfg["BoundaryAwareClustering"]="false";
    cfg["SRLMaxPerSLICEM"]="1"; // This fixture contains only FFs, no SRL control metadata.
    {
        pi.calculateNetNumDistributionOfPUs();
        GlobalPlacer gp(&pi,cfg);
        gp.GlobalPlacement_CLBElements(1,false,5,true,false,25,&timing);
        gp.finalizeMacroLegalization();
        for(auto u:pi.getPlacementUnits())check(std::isfinite(u->X())&&std::isfinite(u->Y()),"macro-off bounded GP and final legalization return finite positions");
    }
    cfg["SllPlanningEnable"]="false";cfg["SllTimingCorrectionEnable"]="false";
    PlacementInfo plain(&design,&device,cfg);check(!plain.sllPlanner,"planning off creates no virtual resources");
    std::cout<<"PASS independent quantity, capacity, timing, diagnostics, export and planning switches\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
