#include "SllRunDiagnostics.h"
#include "FrozenQpArchive.h"
#include "PlacementInfo.h"
#include "simpleJSON.h"
#include <iostream>
using namespace amf_sll_audit;
static std::vector<std::map<std::string,std::string>> table(const std::filesystem::path&p){
    std::ifstream f(p);std::string line;std::vector<std::string> header;std::vector<std::map<std::string,std::string>> out;
    bool first=true;while(std::getline(f,line)){std::istringstream in(line);std::string v;std::vector<std::string> values;
        while(std::getline(in,v,'\t'))values.push_back(v);
        if(first){header=values;first=false;continue;}if(values.size()!=header.size())throw std::runtime_error("TSV shape mismatch: "+p.string());
        std::map<std::string,std::string> row;for(size_t i=0;i<header.size();++i)row[header[i]]=values[i];out.push_back(row);}
    return out;
}
int main(int argc,char**argv){try{
    if(argc!=3)throw std::runtime_error("usage: checkSllAdaptiveIntegration four-cell-config NEW_OUTPUT");
    std::filesystem::path root=argv[2];if(!std::filesystem::create_directory(root))throw std::runtime_error("Refusing overwrite");
    auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo pi(&design,&device,cfg);
    size_t checks=0;auto check=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
    for(auto cell:design.getCells())if(!cell->isVirtualCell()){
        auto u=new PlacementInfo::PlacementUnpackedCell(cell->getName(),pi.getPlacementUnits().size(),cell);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[cell->getCellId()]=u;
    }
    check(pi.getPlacementUnits().size()==4,"four synthetic cells");
    for(auto net:design.getNets())if(net->getName()=="C/O")net->setGlobalClock();
    pi.updateCells2PlacementUnits();pi.reloadNets();
    std::ofstream(root/"nets.txt")<<"A/O\nB/O\nD/O\n";std::ofstream(root/"cells.txt")<<"A\nB\n";
    cfg["SllQuantityEnable"]="true";cfg["SllQuantityDiagnostics"]="true";cfg["SllQuantityTau"]="10";cfg["SllQuantityGamma"]="0.02";
    cfg["SllTraceNetList"]=(root/"nets.txt").string();cfg["SllQpAuditCellsFile"]=(root/"cells.txt").string();
    cfg["SllQpAuditQps"]="7;8;13";
    for(auto mode:{"adaptive","disabled","baseline"}){
        auto c=cfg;c["SllReportDirectory"]=(root/mode).string();c["SllDynamicTauEnable"]=std::string(mode)=="disabled"?"false":"true";
        c["SllShortSpanEnable"]=c["SllDynamicTauEnable"];if(std::string(mode)=="baseline")c["SllQuantityEnable"]="false";
        {SllRunDiagnostics diag(&pi,c,.4);
        for(int round=1;round<=12;++round){
            double early[]={90,250,500,750},late[]={239,240,240,240};
            for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50+u->getId(),round<=3?early[u->getId()]:late[u->getId()]);
            diag.stage("hpio",round,round-1,"begin");
            for(int pass=0;pass<2;++pass){
                // The second QP must see the positions written since the first.
                if(pass==1&&round>=4)pi.getPlacementUnits()[1]->setAnchorLocationAndForgetTheOriginalOne(51,244);
                QPSolverWrapper x(true,false,-.5,959.5,4,false),y(true,false,-.5,959.5,4,false);
                for(auto q:{&x,&y}){q->solverData.objectiveMatrixDiag.assign(4,2);q->solverSettings.stabilityGuard=true;q->solverSettings.tolerence=1e-10;}
                for(auto u:pi.getPlacementUnits()){int id=u->getId();x.solverData.oriSolution[id]=u->X();y.solverData.oriSolution[id]=u->Y();
                    x.solverData.objectiveVector[id]=-2*u->X();y.solverData.objectiveVector[id]=-2*u->Y();}
                auto original=y.solverData.objectiveVector;auto diagonal=y.solverData.objectiveMatrixDiag;
                diag.beginModelAudit(x,y,1,.4);diag.auditCheckpoint("anchor_self_position",x,y);diag.prepare(x,y,1,-10);
                check(diagonal==y.solverData.objectiveMatrixDiag&&y.solverData.objectiveMatrixTripletList.empty(),"SLL must not change A");
                if(std::string(mode)=="baseline")check((original.array()==y.solverData.objectiveVector.array()).all(),"baseline b unchanged");
                auto xp=&x,yp=&y;QPSolverWrapper::QPSolve(xp);QPSolverWrapper::QPSolve(yp);diag.candidate(x,y);diag.written(x,y);
                check(y.guardDiagnostics.converged&&!y.guardDiagnostics.rollback,"synthetic QP convergence");
                diag.stage("hpio",round,round-1,pass==0?"after_qp1":"after_qp2");
            }
            diag.stage("hpio",round,round-1,"after_legalization");
        }
        diag.endPhase();diag.stage("final",12,-1,"amf_final");}
        auto q=table(root/mode/"qp.tsv"),rr=table(root/mode/"adaptive-rounds.tsv"),ss=table(root/mode/"short-span.tsv");
        check(q.size()==72&&rr.size()==12&&ss.size()==24,"all QP states and completed rounds logged");
        for(auto&r:q){int round=std::stoi(r["round"]);double tau=std::stod(r["tau"]);
            check(tau==(std::string(mode)=="disabled"||round<=6?10:5),"tau constant within both QPs; next-round reduction");}
        if(std::string(mode)!="disabled"){
            check(std::stoul(ss[6]["excluded_nets"])==2&&std::stoul(ss[7]["excluded_nets"])==1,"mask recomputed before second QP");
            check(ss[7]["resumed"]=="1","second QP resume logged");
            auto archive=root/mode/"frozen-qp/qp-7";auto m=readModel(archive/"model.bin");auto f=readFrozen(archive/"original-y.bin");
            auto r=m.evaluate(f.y.position,f.movable,2,true);check(r.gradient==f.sll,"masked archive gradient reproduced");
            for(auto&n:m.nets)n.gradientActive=true;auto full=m.evaluate(f.y.position,f.movable,2,true);
            check(full.discrete==r.discrete&&full.smooth==r.smooth&&full.activeSmooth>r.activeSmooth,"full S/F preserved in archive");
        }
        auto overhead=table(root/mode/"sll-overhead.tsv");std::map<std::string,double> totals;
        size_t prepareCounts=0;for(auto&r:overhead){double v=std::stod(r["wall_seconds"]);check(std::isfinite(v)&&v>=0,"finite measured overhead");totals[r["category"]]+=v;
            prepareCounts+=r["category"]=="discrete_count"&&r["state"]=="prepare";}
        check(prepareCounts==24,"one counting pass per QP preparation, no duplicate before evaluation");
        check(totals["discrete_count"]>0&&totals["smooth_and_gradient"]>0&&totals["trace_and_log"]>0,"separate populated clocks");
        if(std::string(mode)!="disabled")check(totals["short_span_list_update"]>0,"list update measured separately");
        for(auto&r:table(root/mode/"sll-overhead-summary.tsv"))if(r["category"]!="timing_log_write")check(std::abs(totals[r["category"]]-std::stod(r["wall_seconds"]))<1e-9,"timing summary closure");
    }
    std::cout<<"PASS "<<checks<<" adaptive integration checks; four synthetic cells, no placement flow\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
