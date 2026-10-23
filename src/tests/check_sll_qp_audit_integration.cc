#include "SllRunDiagnostics.h"
#include "FrozenQpArchive.h"
#include "PlacementInfo.h"
#include "simpleJSON.h"
#include <iostream>
using namespace amf_sll_audit;
int main(int argc,char**argv){try{
    if(argc!=3)throw std::runtime_error("usage: checkSllQpAuditIntegration synthetic-config.json NEW_OUTPUT");
    std::filesystem::path root=argv[2];if(!std::filesystem::create_directory(root))throw std::runtime_error("Refusing fixture overwrite");
    auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo pi(&design,&device,cfg);
    size_t checks=0;auto check=[&](bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);};
    for(auto cell:design.getCells())if(!cell->isVirtualCell()){
        auto u=new PlacementInfo::PlacementUnpackedCell(cell->getName(),pi.getPlacementUnits().size(),cell);pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[cell->getCellId()]=u;
        float ys[]={239.9933014f,238.9726105f,240.5f,260.f};u->setAnchorLocationAndForgetTheOriginalOne(50+u->getId(),ys[u->getId()%4]);
    }
    check(pi.getPlacementUnits().size()==4,"Expected four synthetic cells");
    for(auto net:design.getNets()){net->setOverallClusterNetEnhancement(1.5);net->setOverallTimingNetEnhancement(2.0);if(net->getName()=="C/O")net->setGlobalClock();}
    pi.updateCells2PlacementUnits();pi.reloadNets();
    std::ofstream(root/"cells.txt")<<"A\nB\n";std::ofstream(root/"nets.txt")<<"A/O\n";
    cfg["SllQuantityEnable"]="true";cfg["SllQuantityDiagnostics"]="true";cfg["SllQuantityTau"]="10";cfg["SllQuantityGamma"]="0.02";cfg["SllTraceNetList"]=(root/"nets.txt").string();
    Eigen::VectorXd reference,baselineReference;
    for(auto mode:{"enabled","disabled","budget_failure","marker_failure","baseline_enabled","baseline_disabled"}){
        auto c=cfg;c["SllReportDirectory"]=(root/mode).string();
        if(std::string(mode)!="disabled"&&std::string(mode)!="baseline_disabled"){c["SllQpAuditCellsFile"]=(root/"cells.txt").string();c["SllQpAuditQps"]="1";if(std::string(mode)=="budget_failure")c["SllQpAuditMaxBytes"]="40";}
        if(std::string(mode)=="marker_failure"){
            uint64_t payload=0;for(auto&e:std::filesystem::directory_iterator(root/"enabled/frozen-qp/qp-1"))if(e.path().filename()!="COMPLETE.json")payload+=e.file_size();
            c["SllQpAuditMaxBytes"]=std::to_string(payload);
        }
        if(std::string(mode).find("baseline_")==0)c["SllQuantityEnable"]="false";
        SllRunDiagnostics diag(&pi,c,.4);diag.stage("hpio",24,0,"begin");
        QPSolverWrapper x(true,false,-.5,959.5,4,false),y(true,false,-.5,959.5,4,false);
        for(auto q:{&x,&y}){q->solverData.objectiveVector.setZero();q->solverData.objectiveMatrixDiag.assign(4,0);q->solverSettings.stabilityGuard=true;q->solverSettings.tolerence=1e-10;q->solverSettings.maxIters=10000;}
        for(auto u:pi.getPlacementUnits()){x.solverData.oriSolution[u->getId()]=u->X();y.solverData.oriSolution[u->getId()]=u->Y();}
        for(auto net:pi.getPlacementNets())if(!net->getDesignNet()->checkIsPowerNet()&&net->updateNetBounds(true,true))for(auto item:{std::make_pair(&x,true),std::make_pair(&y,false)})
            net->updateBound2BoundNetWeight(item.first->solverData.objectiveMatrixTripletList,item.first->solverData.objectiveMatrixDiag,item.first->solverData.objectiveVector,1.,.4,item.second,!item.second);
        auto before=y.solverData.objectiveVector;auto diagonal=y.solverData.objectiveMatrixDiag;auto entries=y.solverData.objectiveMatrixTripletList;
        diag.beginModelAudit(x,y,1.,.4);
        check((before.array()==y.solverData.objectiveVector.array()).all()&&diagonal==y.solverData.objectiveMatrixDiag&&entries.size()==y.solverData.objectiveMatrixTripletList.size(),"Audit begin changed live B2B");
        diag.auditCheckpoint("anchor_legal_site",x,y);
        y.solverData.objectiveVector[0]+=1.25;
        if(diag.auditTarget(0))diag.auditTimingAnchor(0,false,x,y);
        pi.addPseudoNetsInPlacementInfo(y.solverData.objectiveMatrixTripletList,y.solverData.objectiveMatrixDiag,y.solverData.objectiveVector,pi.getPlacementUnits()[0],240.1,2.,.4,false,true);
        if(diag.auditTarget(0))diag.auditTimingAnchor(0,true,x,y);
        diag.auditCheckpoint("timing_slack_and_pairing",x,y);diag.auditCheckpoint("anchor_clusters",x,y);
        for(auto u:pi.getPlacementUnits())for(auto item:{std::make_pair(&x,true),std::make_pair(&y,false)})pi.addPseudoNetsInPlacementInfo(item.first->solverData.objectiveMatrixTripletList,item.first->solverData.objectiveMatrixDiag,item.first->solverData.objectiveVector,u,item.second?u->X():u->Y(),2.,.4,item.second,!item.second);
        diag.auditCheckpoint("anchor_self_position",x,y);diag.auditCheckpoint("anchor_region",x,y);diag.prepare(x,y,.1,-10);
        auto xp=&x,yp=&y;QPSolverWrapper::QPSolve(xp);QPSolverWrapper::QPSolve(yp);diag.candidate(x,y);diag.written(x,y);
        check(y.guardDiagnostics.converged&&!y.guardDiagnostics.rollback,"Synthetic solve failed");
        if(std::string(mode)=="enabled")reference=y.solverData.solution;
        else if(std::string(mode)=="baseline_enabled")baselineReference=y.solverData.solution;
        else check(((std::string(mode)=="baseline_disabled"?baselineReference:reference).array()==y.solverData.solution.array()).all(),"Diagnostics enabled/disabled/failure changed output");
    }
    auto archive=root/"enabled/frozen-qp/qp-1";check(std::filesystem::exists(archive/"COMPLETE.json"),"No complete integration archive");
    check(!std::filesystem::exists(root/"disabled/frozen-qp"),"Disabled audit created files");check(!std::filesystem::exists(root/"budget_failure/frozen-qp/qp-1/COMPLETE.json"),"Incomplete archive marked complete");
    check(!std::filesystem::exists(root/"marker_failure/frozen-qp/qp-1/COMPLETE.json")&&std::filesystem::exists(root/"marker_failure/frozen-qp/qp-1/COMPLETE.json.pending"),"Failed completion marker must not be accepted");
    auto f=readFrozen(archive/"original-y.bin");auto m=readModel(archive/"model.bin");check(f.targets==std::vector<uint32_t>({0,1}),"Target cell identity mapping");
    check(readFrozen(root/"baseline_enabled/frozen-qp/qp-1/original-y.bin").injectedCoefficient==0,"Baseline must record zero actual injection");
    check(m.nets.size()==3,"Full model should retain all three signal nets, exclude clock/power");check(m.evaluate(f.y.position,f.movable,2,true).gradient==f.sll,"Full incident aggregation mismatch");
    std::cout<<"PASS "<<checks<<" SLL audit integration checks; four synthetic cells, no placement flow\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
