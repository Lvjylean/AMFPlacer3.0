#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <Eigen/Dense>
#include <filesystem>
#include <iostream>
#include <iomanip>
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
    check(argc==3,"usage: checkSllWireSplitQp config NEW_OUTPUT");
    std::filesystem::path out=argv[2];check(std::filesystem::create_directory(out),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningReportDirectory"]=(out/"planning").string();
    cfg["SllWireCacheAuditStride"]="1";cfg["SllJointPlacementEnable"]="true";
    cfg["SllPlanningAllowSplit"]="false";cfg["SllSourceNearInitialization"]="true";
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    for(auto c:design.getCells())if(!c->isVirtualCell()){
        auto u=new PlacementInfo::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[c->getCellId()]=u;
        if(c->getName()=="A")u->setAnchorLocationAndForgetTheOriginalOne(80,270);
        else if(c->getName()=="B")u->setAnchorLocationAndForgetTheOriginalOne(10,270);
        else if(c->getName()=="C")u->setAnchorLocationAndForgetTheOriginalOne(130,270);
        else if(c->getName()=="D")u->setAnchorLocationAndForgetTheOriginalOne(70,510);
        else throw std::runtime_error("unexpected fixture cell");
    }
    check(pi.getPlacementUnits().size()==4,"four cells");
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);pi.sllPlanner->refresh();
    check(pi.sllPlanner->model().plans.size()==1&&pi.sllPlanner->variableCount()==1,"one crossing");
    pi.sllPlanner->prepareDensityTargets();
    auto old=pi.sllPlanner->virtualPositions();Eigen::VectorXd x=Eigen::VectorXd::Zero(5),y=x;
    x[4]=33;y[4]=old[0].y;pi.sllPlanner->projectQp(x,y,-10);
    const auto&plan=pi.sllPlanner->model().plans.front();const auto&geo=pi.sllPlanner->model().geometry(plan.crossings.front());
    check(geo.in.x==33,"actual U250 physical COLUMN 189 projects to AMF X=33");
    const int source=plan.net.source.unit;const double sx=pi.getPlacementUnits()[source]->X();
    check(sx==80,"fixture source");
    auto assemble=[&](bool horizontal){
        std::vector<Eigen::Triplet<float>> trip;std::vector<float> diag(5,0);Eigen::VectorXd q=Eigen::VectorXd::Zero(5);
        pi.sllPlanner->addWireModel(plan.net.id,trip,diag,q,1,.4,horizontal);
        Eigen::MatrixXd h=Eigen::MatrixXd::Zero(5,5);
        for(int i=0;i<5;++i)h(i,i)=diag[i];for(const auto&t:trip)h(t.row(),t.col())+=t.value();
        check(h(source,4)<0&&h(4,source)==h(source,4),"mandatory real-source/virtual-entry coupling in X and Y");
        if(horizontal){
            Eigen::VectorXd coord(5);for(int i=0;i<4;++i)coord[i]=pi.getPlacementUnits()[i]->X();coord[4]=geo.in.x;
            Eigen::VectorXd gradient=h*coord+q;
            check(std::abs(gradient[source]-2*plan.net.weight)<1e-5,"source gradient retains input leg despite bounding-box interior");
            check(std::abs(h(source,4)+2*plan.net.weight/(sx-geo.in.x))<1e-7,"two-terminal B2B weight has no fanout dilution");
            const double sourceGradient=gradient[source];
            // Isolated solve of the real production wire matrix with explicit
            // test anchors; no density/timing terms can mask this regression.
            for(int i=0;i<5;++i){const double anchor=(i==source||i==4)?.02:1e4;h(i,i)+=anchor;q[i]-=anchor*coord[i];}
            Eigen::VectorXd solved=h.ldlt().solve(-q);
            check(solved.allFinite()&&solved[source]<sx&&solved[4]>geo.in.x,"source and virtual cell both respond toward each other");
            std::cout<<std::setprecision(10)<<"QP source_gradient="<<sourceGradient<<" source_before="<<sx<<" source_after="<<solved[source]<<" crossing_before="<<geo.in.x<<" crossing_after="<<solved[4]<<"\n";
        }
    };
    assemble(true);assemble(false);
    const double before=pi.sllPlanner->wireLength(plan.net.id,.4);
    const amf_sll_plan::Point trial{sx-5,pi.getPlacementUnits()[source]->Y()};
    const double moved=pi.sllPlanner->wireLength(plan.net.id,.4,source,trial);
    check(std::abs(before-moved-5)<1e-7,"live trial source cost sees the unmasked five-unit improvement");
    {SllPlanner::WireLengthBatch batch(pi.sllPlanner.get());
        check(float(before)==float(pi.sllPlanner->wireLength(plan.net.id,.4)),"baseline cache equals live cost");
        check(float(moved)==float(pi.sllPlanner->wireLength(plan.net.id,.4,source,trial)),"trial cache equals live cost");}
    std::cout<<"PASS actual U250 QP matrix, solve response and cached/uncached split wire costs\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
