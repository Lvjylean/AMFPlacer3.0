#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "WirelengthOptimizer.h"
#include "simpleJSON.h"
#include "SllDensityModel.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
    check(argc==3 || (argc==4 && std::string(argv[3])=="tile"),"usage: checkSllJointIntegration config NEW_OUTPUT [tile]");
    const bool tileMode=argc==4;
    std::filesystem::path root=argv[2];check(std::filesystem::create_directory(root),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningReportDirectory"]=(root/"planning").string();
    cfg["SllPlanningAllowSplit"]="false";cfg["SllJointPlacementEnable"]="true";cfg["SllSourceNearInitialization"]="true";
    // Deliberately small target utilization exposes density pressure using a
    // four-register fixture on the actual full U250 resource table.
    if(tileMode){cfg.erase("SllDensityRowHeight");cfg["SllDensityTargetUtilization"]="0.05";}
    else {cfg["SllDensityRowHeight"]="1000";cfg["SllDensityTargetUtilization"]="0.0001";}
    cfg["SllDensityWeight"]="1000000";cfg["SllDensityAnchorWeight"]="0.05";
    cfg["SllQuantityEnable"]="true";cfg["SllQuantityDiagnostics"]="true";
    cfg["SllDynamicTauEnable"]="true";cfg["SllShortSpanEnable"]="true";
    cfg["SllReportDirectory"]=(root/"quantity").string();cfg["SllTraceNetList"]=(root/"sample.txt").string();
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    for(auto c:design.getCells())if(!c->isVirtualCell()){auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[c->getCellId()]=u;}
    const size_t cells=design.getCells().size(),real=pi.getPlacementUnits().size();check(real==4,"four-register fixture");
    for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,u->getId()%2?510:270);
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);pi.createGridBins(5,5);pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();
    {
        std::ofstream out(root/"initial-tile-selection.tsv");
        out<<"net\tfrom\tto\tcolumn\tin_x\tin_y\tout_x\tout_y\tchannel\n";
        for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){
            const auto&t=pi.sllPlanner->model().geometry(e);
            out<<p.net.name<<'\t'<<t.from<<'\t'<<t.to<<'\t'<<t.column<<'\t'<<t.in.x<<'\t'<<t.in.y<<'\t'<<t.out.x<<'\t'<<t.out.y<<'\t'<<e.channel<<'\n';
        }
    }
    pi.sllPlanner->prepareDensityTargets();
    check(pi.sllPlanner->variableCount()==3,"one shared QP variable per crossing");
    amf_sll_plan::DensityModel tileInventory;tileInventory.index(pi.sllPlanner->model());
    check(tileInventory.capacity.size()==2880,"U250 has 3 boundaries times 16 columns times 60 tile bins");
    for(const auto&b:tileInventory.capacity)check(b.second==24,"real U250 table gives 24 canonical channels per tile");
    {
        const auto before=pi.sllPlanner->virtualPositions();
        Eigen::VectorXd x=Eigen::VectorXd::Zero(real+before.size()),y=x;
        std::vector<int> expected;
        for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){
            const auto&base=pi.sllPlanner->model().tiles[e.tile];int chosen=-1;
            for(int ti:pi.sllPlanner->model().availableTiles.at({base.from,base.to})){
                const auto&t=pi.sllPlanner->model().tiles[ti];
                if(t.in.x==62&&std::abs(t.in.y-base.in.y)<.01){chosen=ti;break;}
            }
            check(chosen>=0,"real U250 adjacent column fixture");
            const size_t i=expected.size();expected.push_back(chosen);
            const auto&t=pi.sllPlanner->model().tiles[chosen];x[real+i]=t.in.x+.1;y[real+i]=t.in.y+.1;
        }
        pi.sllPlanner->projectQp(x,y);
        check(tileInventory.current(pi.sllPlanner->model())==expected,"actual planner consumes virtual QP coordinates");
        pi.sllPlanner->refresh();
        check(tileInventory.current(pi.sllPlanner->model())==expected,"refresh keeps QP-selected tile");
        check(pi.sllPlanner->model().initialTileEvaluations==0,"refresh does not initialize surviving crossings");
        pi.sllPlanner->prepareDensityTargets();
    }
    {
        std::ofstream out(root/"bounded-column-cases.tsv");
        out<<"gap\tdirection\tfrom\tto\told_x\traw_x\tlimited_x\tlegal_x\told_y\tlegal_y\n";
        auto proposals=[&](double wanted,double limit){
            Eigen::VectorXd x=Eigen::VectorXd::Zero(real+pi.sllPlanner->variableCount()),y=x;
            auto old=pi.sllPlanner->virtualPositions();
            for(size_t i=0;i<old.size();++i){x[real+i]=wanted;y[real+i]=old[i].y;}
            pi.sllPlanner->projectQp(x,y,limit);
        };
        for(const auto&pair:std::vector<std::pair<double,double>>{{14,25},{42,54}}){
            proposals(pair.first,-10); // Place the test origin without clipping.
            for(int direction:{1,-1}){
                const double start=direction>0?pair.first:pair.second;
                const double finish=direction>0?pair.second:pair.first;
                const auto old=pi.sllPlanner->virtualPositions();
                for(const auto&v:old)check(v.x==start,"real U250 11/12-gap starting column");
                proposals(start+100*direction,10);
                const auto now=pi.sllPlanner->virtualPositions();size_t k=0;
                for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){
                    const auto&t=pi.sllPlanner->model().geometry(e);
                    check(now[k].x==finish&&now[k].y==old[k].y,"real U250 column switches after 10-unit clipping");
                    check(std::abs(now[k].x-old[k].x)>10,"legalized displacement can exceed 10");
                    out<<pair.second-pair.first<<'\t'<<direction<<'\t'<<t.from<<'\t'<<t.to<<'\t'<<start<<'\t'<<start+100*direction<<'\t'<<start+10*direction<<'\t'<<finish<<'\t'<<old[k].y<<'\t'<<now[k].y<<'\n';++k;
                }
            }
        }
        pi.sllPlanner->prepareDensityTargets();
    }
    const size_t n=real+pi.sllPlanner->variableCount();std::vector<Eigen::Triplet<float>> trip;std::vector<float> diag(n,0);Eigen::VectorXd rhs=Eigen::VectorXd::Zero(n);
    for(const auto&p:pi.sllPlanner->model().plans)pi.sllPlanner->addWireModel(p.net.id,trip,diag,rhs,1,.4,true);
    bool coupled=false;for(auto&e:trip)coupled|=e.row()<real&&e.col()>=real&&e.value()<0;
    check(coupled,"true real/virtual QP cross terms, not fixed anchors");
    pi.sllPlanner->addDensityModel(diag,rhs,true);for(size_t i=real;i<n;++i)check(diag[i]>0&&std::isfinite(rhs[i]),"density terms enter auxiliary rows");
    for(auto u:pi.getPlacementUnits())u->setFixed();
    WirelengthOptimizer wl(&pi,cfg,false);wl.recordSllStage("ordinary",1,0,"begin");
    wl.GlobalPlacementQPSolve(.02,true,true,false,true,false,-10,nullptr,false);
    std::set<int> columns;std::set<std::string> used;
    for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){const auto&c=pi.sllPlanner->model().geometry(e);columns.insert(c.column);check(e.channel==-1,"no exact channel during QP");}
    if(tileMode){
        std::set<amf_sll_plan::DensityModel::Bin> bins;
        for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings)
            bins.insert(tileInventory.bin(pi.sllPlanner->model().geometry(e)));
        std::cout<<"Projected distinct tile bins="<<bins.size()<<'\n';
    } else std::cout<<"Projected distinct columns="<<columns.size()<<'\n';
    // Soft density targets need not survive rounding. QP-nearest projection is
    // checked against the recorded raw solve, independently by the test runner.
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
    {
        std::map<std::pair<int,int>,double> edgeDelays;
        for(auto edge:pi.getTimingInfo()->getSimplePlacementTimingGraph()->getEdges())
            edgeDelays[{edge->getSourcePin()->getElementIdInType(),edge->getSinkPin()->getElementIdInType()}]=edge->getDelay();
        std::ofstream out(root/"sll-sta-audit.tsv");out<<std::setprecision(17);
        out<<"net\tsink\tcrossings\tlocal_ns\tsll_ns\texpected_ns\tpin_api_ns\tsta_edge_ns\tlegacy_direct_plus_penalty_ns\n";
        size_t checked=0;
        for(const auto&p:pi.sllPlanner->model().plans)for(const auto&s:p.net.sinks){
            const auto&path=p.paths.at(s.id);if(path.empty())continue;
            double local=0,sll=0;auto cur=p.net.source.p;
            for(int k:path){const auto&c=pi.sllPlanner->model().geometry(p.crossings[k]);
                local+=timing.getDelayByModel_conservative(cur.x,cur.y,c.in.x,c.in.y,true);sll+=c.slowMax;cur=c.out;}
            local+=timing.getDelayByModel_conservative(cur.x,cur.y,s.p.x,s.p.y,true);
            const double actual=timing.getDelayForPins(design.getPins().at(p.net.source.id),design.getPins().at(s.id),p.net.source.p.x,p.net.source.p.y,s.p.x,s.p.y);
            const double edge=edgeDelays.at({p.net.source.id,s.id});
            check(std::abs(actual-local-sll)<1e-5,"joint STA pin API uses local polynomial plus process SLL delay");
            check(std::abs(edge-local-sll)<1e-5,"actual STA graph uses selected crossing without legacy addition");
            const double legacy=timing.getDelayByModel_conservative(p.net.source.p.x,p.net.source.p.y,s.p.x,s.p.y,true)+std::stod(cfg.at("SLRBoundaryDelayNs"))*path.size();
            out<<p.net.name<<'\t'<<s.name<<'\t'<<path.size()<<'\t'<<local<<'\t'<<sll<<'\t'<<local+sll<<'\t'<<actual<<'\t'<<edge<<'\t'<<legacy<<'\n';++checked;
        }
        check(checked>0,"cross-die STA edges audited");
    }
    const auto finalTiles=tileInventory.current(pi.sllPlanner->model());
    pi.sllPlanner->exportPlan((root/"sll-plan.tcl").string());
    check(tileInventory.current(pi.sllPlanner->model())==finalTiles,"export preserves final QP tiles");
    for(const auto&p:pi.sllPlanner->model().plans)for(const auto&e:p.crossings){
        check(e.channel>=0,"export assigns precise SLL channels");const auto&c=pi.sllPlanner->model().channels[e.channel];
        check(!amf_sll_plan::Model::conflicts(c,used),"final SLL paths are disjoint");amf_sll_plan::Model::occupy(c,used);
    }
    check(design.getCells().size()==cells,"logical netlist unchanged");
    std::cout<<"PASS joint QP coupling, density movement, legal channels, two solves, dynamic tau/quantity, lifecycle, export\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
