#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
    check(argc==3,"usage: checkSllJointIntegration config NEW_OUTPUT");
    std::filesystem::path root=argv[2];check(std::filesystem::create_directory(root),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningReportDirectory"]=(root/"planning").string();
    cfg["SllPlanningAllowSplit"]="false";cfg["SllJointPlacementEnable"]="true";cfg["SllSourceNearInitialization"]="true";
    // Deliberately small target utilization exposes density pressure using a
    // four-register fixture on the actual full U250 resource table.
    cfg["SllDensityRowHeight"]="1000";cfg["SllDensityTargetUtilization"]="0.0001";
    cfg["SllDensityWeight"]="1000000";cfg["SllDensityAnchorWeight"]="0.05";
    cfg["SllQuantityEnable"]="true";cfg["SllQuantityDiagnostics"]="true";
    cfg["SllDynamicTauEnable"]="true";cfg["SllShortSpanEnable"]="true";
    cfg["SllReportDirectory"]=(root/"quantity").string();cfg["SllTraceNetList"]=(root/"sample.txt").string();
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    for(auto c:design.getCells())if(!c->isVirtualCell()){auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[c->getCellId()]=u;}
    const size_t cells=design.getCells().size(),real=pi.getPlacementUnits().size();check(real==4,"four-register fixture");
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,u->getId()%2?510:270);
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);pi.createGridBins(5,5);pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();pi.sllPlanner->prepareDensityTargets();
    check(pi.sllPlanner->variableCount()==3,"one shared QP variable per crossing");
    const size_t n=real+pi.sllPlanner->variableCount();std::vector<Eigen::Triplet<float>> trip;std::vector<float> diag(n,0);Eigen::VectorXd rhs=Eigen::VectorXd::Zero(n);
    for(const auto&p:pi.sllPlanner->model().plans)pi.sllPlanner->addWireModel(p.net.id,trip,diag,rhs,1,.4,true);
    bool coupled=false;for(auto&e:trip)coupled|=e.row()<real&&e.col()>=real&&e.value()<0;
    check(coupled,"true real/virtual QP cross terms, not fixed anchors");
    pi.sllPlanner->addDensityModel(diag,rhs,true);for(size_t i=real;i<n;++i)check(diag[i]>0&&std::isfinite(rhs[i]),"density terms enter auxiliary rows");
    for(auto u:pi.getPlacementUnits())u->setFixed();
    WirelengthOptimizer wl(&pi,cfg,false);wl.recordSllStage("ordinary",1,0,"begin");
    wl.GlobalPlacementQPSolve(.02,true,true,false,true,false,-10,nullptr,false);
    std::set<int> columns;std::set<std::string> used;
    for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){const auto&c=pi.sllPlanner->model().channels[e.channel];columns.insert(c.column);check(!amf_sll_plan::Model::conflicts(c,used),"unique exact channels");amf_sll_plan::Model::occupy(c,used);}
    check(columns.size()>1,"density disperses virtual SLL cells while real endpoints are fixed");
    for(auto u:pi.getPlacementUnits())check(u->X()==50&&u->Y()==(u->getId()%2?510:270),"fixed real cells unchanged");
    wl.GlobalPlacementQPSolve(.02,false,true,false,true,false,-10,nullptr,false);
    wl.recordSllStage("ordinary",1,0,"after_legalization");
    // A second solve can also rebuild a different-size topology safely.
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,270);
    wl.GlobalPlacementQPSolve(.02,false,true,false,true,false,-10,nullptr,false);
    check(pi.sllPlanner->variableCount()==0,"mid-sequence lifecycle removes all auxiliary variables");
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,u->getId()%2?510:270);
    wl.GlobalPlacementQPSolve(.02,false,true,false,true,false,-10,nullptr,false);
    check(pi.sllPlanner->variableCount()==3,"auxiliary variables recreated safely");
    auto source=pi.getPlacementUnits()[0];source->setUnfixed();
    const double beforeX=source->X(),beforeY=source->Y();
    pi.updateElementBinGrid(false);
    timing.conductStaticTimingAnalysis();
    wl.GlobalPlacementQPSolve(.02,true,true,false,true,false,10,&timing,false);
    check(std::isfinite(source->X())&&std::isfinite(source->Y()),"joint timing QP returns finite real coordinates");
    check(std::hypot(source->X()-beforeX,source->Y()-beforeY)>1e-6,"real cell responds to joint virtual wire/timing/density model");
    pi.updateElementBinGrid(false);
    timing.conductStaticTimingAnalysis();
    pi.sllPlanner->exportPlan((root/"sll-plan.tcl").string());
    check(design.getCells().size()==cells,"logical netlist unchanged");
    std::cout<<"PASS joint QP coupling, density movement, legal channels, two solves, dynamic tau/quantity, lifecycle, export\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
