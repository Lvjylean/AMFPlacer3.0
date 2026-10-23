#include "SllQpAudit.h"
#include "PlacementInfo.h"
#include <chrono>
#include <functional>
using namespace amf_sll_audit;
struct SllQpAudit::Impl {
    PlacementInfo*pi;bool enabled=false,active=false;size_t qid=0;int round=0,iteration=0;
    std::string phase;std::filesystem::path root,current;
    std::set<size_t> qps;std::set<std::string> names;Components components;Budget budget;
    std::vector<double> timingX,timingY;double anchorBeforeX=0,anchorBeforeY=0;
    std::ofstream index;double seconds=0;std::vector<PlacementInfo::PlacementUnit*> units;
    Impl(PlacementInfo*p,const std::map<std::string,std::string>&c):pi(p) {
        if(!c.count("SllQpAuditCellsFile")) {
            if(c.count("SllQpAuditQps")||c.count("SllQpAuditMaxBytes"))throw std::invalid_argument("QP audit options need SllQpAuditCellsFile");
            return;
        }
        qps=parseQps(c.at("SllQpAuditQps"));std::ifstream f(c.at("SllQpAuditCellsFile"));
        if(!f)throw std::invalid_argument("Cannot open QP audit cell list");
        std::string s;while(std::getline(f,s)){if(!s.empty()&&s.back()=='\r')s.pop_back();if(!s.empty()&&!names.insert(s).second)throw std::invalid_argument("Duplicate audit cell");}
        if(names.empty())throw std::invalid_argument("Empty QP audit cell list");
        s=c.count("SllQpAuditMaxBytes")?c.at("SllQpAuditMaxBytes"):"2147483648";
        if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("Invalid QP audit byte budget");
        budget.limit=std::stoull(s);if(!budget.limit)throw std::invalid_argument("Zero QP audit byte budget");
        root=std::filesystem::path(c.at("SllReportDirectory"))/"frozen-qp";
        if(!std::filesystem::create_directory(root))throw std::runtime_error("Refusing frozen-qp directory reuse");
        index.open(root/"index.tsv");index<<"qp\tstate\tbytes_total\tdiagnostic_seconds\tdetail_json\n";
        if(!index)throw std::runtime_error("Cannot open QP audit index");
        std::ofstream configuration(root/"configuration.json");configuration<<"{\"schema\":1,\"byte_budget_scope\":\"snapshot payload, excluding this small config and index\",\"executable\":"<<json(std::filesystem::read_symlink("/proc/self/exe").string())<<",\"config\":{";
        bool first=true;for(auto&v:c){if(!first)configuration<<',';first=false;configuration<<json(v.first)<<':'<<json(v.second);}configuration<<"}}\n";configuration.close();if(!configuration)throw std::runtime_error("Cannot save audit config");enabled=true;
    }
    void note(const char*state,const std::string&detail) {index<<qid<<'\t'<<state<<'\t'<<budget.used<<'\t'<<std::setprecision(17)<<seconds<<'\t'<<json(detail)<<std::endl;}
    void safe(const std::function<void()>&f) {
        if(!active)return;auto t=std::chrono::steady_clock::now();
        try{f();}catch(const std::exception&e){active=false;note("incomplete",e.what());std::cerr<<"SLL_QP_AUDIT incomplete qp="<<qid<<": "<<e.what()<<'\n';}
        seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
    }
    std::vector<double> coordinates(bool y) {std::vector<double> a(pi->getPlacementUnits().size());for(auto u:pi->getPlacementUnits())a.at(u->getId())=y?u->Y():u->X();return a;}
    void text(const char*file,const std::string&s){Writer w(current/file,budget);w.text(s);w.finish();}
    void incident(const amf_sll::Model&m) {
        std::set<int> targets(components.ids.begin(),components.ids.end());
        std::set<DesignInfo::DesignNet*> nets;std::ostringstream members;
        members<<"cell_json\tpu\tfixed\tlocked\n";
        // Resolve all cells in each target PU, including multi-cell macros.
        for(auto cell:pi->getDesignInfo()->getCells()) {
            if(cell->isVirtualCell())continue;auto pu=pi->getPlacementUnitByCellId(cell->getCellId());
            if(!pu||!targets.count(pu->getId()))continue;
            members<<json(cell->getName())<<'\t'<<pu->getId()<<'\t'<<pu->isFixed()<<'\t'<<pu->isLocked()<<'\n';
            for(auto pin:cell->getPins())if(pin&&pin->getNet())nets.insert(pin->getNet());
        }
        text("target-members.tsv",members.str());
        std::map<unsigned,size_t> models;for(size_t i=0;i<m.nets.size();++i)models[m.nets[i].id]=i;
        std::vector<DesignInfo::DesignNet*> ordered(nets.begin(),nets.end());
        std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->getElementIdInType()<b->getElementIdInType();});
        std::ostringstream nr,pr;nr<<"net_id\tnet_json\tpins\tmodel_index\toptimize\texclusion\twhole_net_cluster_factor\twhole_net_timing_factor\tpin_scope\n";
        pr<<std::setprecision(17)<<"net_id\tpin_json\tcell_json\tpu\tx\ty\toffset_x\toffset_y\tmovable\tmapped\n";
        for(auto n:ordered){auto it=models.find(n->getElementIdInType());
            std::string reason=it!=models.end()?(m.nets[it->second].optimize?(m.nets[it->second].gradientActive?"included":"short_span_this_QP"):"optimization_excluded"):(n->checkIsPowerNet()?"power":n->checkIsGlobalClock()?"global_clock":"incomplete_or_empty");
            nr<<n->getElementIdInType()<<'\t'<<json(n->getName())<<'\t'<<n->getPins().size()<<'\t'<<(it==models.end()?-1:long(it->second))<<'\t'<<(it!=models.end()&&m.nets[it->second].optimize)<<'\t'<<reason<<'\t'<<n->getOverallClusterEnhanceRatio()<<'\t'<<n->getOverallTimingEnhanceRatio()<<'\t'<<(it==models.end()?"target_member_pins_only_excluded_from_SLL":"all_pins")<<'\n';
            for(auto pin:n->getPins()){
                auto cell=pin?pin->getCell():nullptr;auto pu=cell&&!cell->isVirtualCell()?pi->getPlacementUnitByCellId(cell->getCellId()):nullptr;
                // Excluded clocks/power are inventoried with full degree and
                // exclusion reason; their unrelated device-wide pins cannot
                // affect this SLL model and are not duplicated in the archive.
                if(it==models.end()&&(!pu||!targets.count(pu->getId())))continue;
                pr<<n->getElementIdInType()<<'\t'<<json(pin?pin->getName():"")<<'\t'<<json(cell?cell->getName():"")<<'\t';
                if(!pu){pr<<"-1\tnan\tnan\tnan\tnan\t0\t0\n";continue;}
                double ox=pin->getOffsetXInCell(),oy=pin->getOffsetYInCell();if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)){ox+=macro->getCellOffsetXInMacro(cell);oy+=macro->getCellOffsetYInMacro(cell);}
                pr<<pu->getId()<<'\t'<<pu->X()+ox<<'\t'<<pu->Y()+oy<<'\t'<<ox<<'\t'<<oy<<'\t'<<(!pu->isFixed()&&!pu->isLocked())<<"\t1\n";
            }
        }
        text("incident-nets.tsv",nr.str());text("incident-pins.tsv",pr.str());
    }
};
SllQpAudit::SllQpAudit(PlacementInfo*pi,const std::map<std::string,std::string>&c):p(new Impl(pi,c)){}
SllQpAudit::~SllQpAudit()=default;
void SllQpAudit::begin(size_t qid,const std::string&phase,int round,int iteration,QPSolverWrapper&x,QPSolverWrapper&y,float weight,float ratio) {
    auto&s=*p;s.active=s.enabled&&s.qps.count(qid);s.qid=qid;s.round=round;s.iteration=iteration;s.phase=phase;s.seconds=0;
    s.safe([&]{
        s.units.clear();std::set<uint32_t> ids;for(auto name:s.names){auto cell=s.pi->getDesignInfo()->getCell(name);if(!cell)throw std::runtime_error("Audit cell missing: "+name);auto u=s.pi->getPlacementUnitByCellId(cell->getCellId());if(!u)throw std::runtime_error("Audit cell has no PU");if(ids.insert(u->getId()).second)s.units.push_back(u);}
        s.components.start({ids.begin(),ids.end()},s.coordinates(false),s.coordinates(true));
        s.timingX.assign(ids.size(),0);s.timingY=s.timingX;
        s.current=s.root/("qp-"+std::to_string(qid));if(!std::filesystem::create_directory(s.current))throw std::runtime_error("Duplicate QP snapshot");s.note("capturing",phase);
        // Re-evaluate only incident B2B nets into private buffers, with inherited
        // net/pin enhancement removed. Never mutate net weights or live solvers.
        auto n=s.pi->getPlacementUnits().size();QPSolverWrapper bx(true,false,0,1,n,false),by(true,false,0,1,n,false);
        for(auto q:{&bx,&by}){q->solverData.objectiveVector.setZero();q->solverData.objectiveMatrixDiag.assign(n,0);}
        std::set<PlacementInfo::PlacementNet*> incident;for(auto u:s.units)for(auto net:*u->getNetsSetPtr())incident.insert(net);
        const char* labels[]={"wire_b2b_unenhanced","inherited_cluster_net_enhancement","inherited_timing_net_enhancement"};
        for(unsigned mode=0;mode<3;++mode){
            for(auto q:{&bx,&by}){q->solverData.objectiveVector.setZero();q->solverData.objectiveMatrixDiag.assign(n,0);q->solverData.objectiveMatrixTripletList.clear();}
            for(auto net:s.pi->getPlacementNets())if(incident.count(net)&&!net->getDesignNet()->checkIsPowerNet()){
                for(auto item:{std::make_pair(&bx,true),std::make_pair(&by,false)})
                    if(item.second ? net->getLeftPUId()!=net->getRightPUId() : net->getTopPUId()!=net->getBottomPUId())
                    net->updateBound2BoundNetWeight(item.first->solverData.objectiveMatrixTripletList,item.first->solverData.objectiveMatrixDiag,item.first->solverData.objectiveVector,weight,ratio,item.second,!item.second,false,mode);
            }
            s.components.cumulative(labels[mode],bx,by);
        }
        s.components.cumulative("inherited_pin_pair_enhancement_and_assembly_roundoff",x,y);
    });
}
bool SllQpAudit::target(int id)const{return p->active&&std::binary_search(p->components.ids.begin(),p->components.ids.end(),uint32_t(id));}
void SllQpAudit::timingAnchor(int id,bool after,const QPSolverWrapper&x,const QPSolverWrapper&y){auto&s=*p;s.safe([&]{
    auto it=std::lower_bound(s.components.ids.begin(),s.components.ids.end(),uint32_t(id));if(it==s.components.ids.end()||*it!=uint32_t(id))return;size_t i=it-s.components.ids.begin();
    double gx=double(x.solverData.objectiveMatrixDiag[id])*s.components.px[id]+x.solverData.objectiveVector[id];
    double gy=double(y.solverData.objectiveMatrixDiag[id])*s.components.py[id]+y.solverData.objectiveVector[id];
    if(!after){s.anchorBeforeX=gx;s.anchorBeforeY=gy;}else{s.timingX[i]+=gx-s.anchorBeforeX;s.timingY[i]+=gy-s.anchorBeforeY;}
});}
void SllQpAudit::checkpoint(const char*name,const QPSolverWrapper&x,const QPSolverWrapper&y){p->safe([&]{
    auto&s=*p;s.components.cumulative(name,x,y);
    if(std::string(name)=="timing_slack_and_pairing"){
        auto&v=s.components.parts.back();for(size_t i=0;i<v.x.size();++i){v.x[i]-=s.timingX[i];v.y[i]-=s.timingY[i];}
        s.components.parts.push_back({"anchor_timing_target",s.timingX,s.timingY});
    }
});}
void SllQpAudit::freeze(const amf_sll::Model&m,const std::vector<unsigned char>&mask,const amf_sll::FrozenAxis&y0,const std::vector<double>&g,double c,bool injected,float limit,QPSolverWrapper&x,QPSolverWrapper&y) {
    auto&s=*p;
    s.safe([&]{
        Frozen f;f.y=y0;f.x=s.components.px;f.warmY.assign(y.solverData.oriSolution.data(),y.solverData.oriSolution.data()+m.units);f.warmX.assign(x.solverData.oriSolution.data(),x.solverData.oriSolution.data()+m.units);
        f.movable=mask;f.sll=g;f.targets=s.components.ids;f.coefficient=c;f.injectedCoefficient=injected?c:0;f.tolerance=y.solverSettings.tolerence;f.maxIters=y.solverSettings.maxIters;f.guarded=y.solverSettings.stabilityGuard;f.unconstrained=y.solverSettings.useUnconstrainedCG;f.solutionForward=y.solverSettings.solutionForward;f.limit=limit;
        std::ostringstream rows;rows<<std::setprecision(17)<<"pu\tcomponent\tgx\tgy\n";
        auto gx=gradient(x,s.components.px,s.components.ids),gy=gradient(y,s.components.py,s.components.ids);
        for(size_t i=0;i<f.targets.size();++i){double sx=0,sy=0;for(auto&v:s.components.parts){sx+=v.x[i];sy+=v.y[i];rows<<f.targets[i]<<'\t'<<v.name<<'\t'<<v.x[i]<<'\t'<<v.y[i]<<'\n';}
            rows<<f.targets[i]<<"\toriginal_total\t"<<gx[i]<<'\t'<<gy[i]<<'\n'<<f.targets[i]<<"\tsum_residual\t"<<sx-gx[i]<<'\t'<<sy-gy[i]<<'\n'<<f.targets[i]<<"\tsll_all_nets\t0\t"<<c*g[f.targets[i]]<<'\n';
            if(std::abs(sx-gx[i])+std::abs(sy-gy[i])>1e-8*(1+std::abs(gx[i])+std::abs(gy[i])))throw std::runtime_error("Component closure failed");
        }
        s.text("components.tsv",rows.str());s.incident(m);writeModel(s.current/"model.bin",m,s.budget);writeFrozen(s.current/"original-y.bin",f,s.budget);
        std::ostringstream meta;meta<<std::setprecision(17)<<"{\"schema\":1,\"qp\":"<<s.qid<<",\"phase\":"<<json(s.phase)<<",\"round\":"<<s.round<<",\"iteration\":"<<s.iteration<<",\"coefficient\":"<<c<<",\"scope\":\"full SLL model and Y QP; X start/warm/raw/written for 2D step inspection\",\"encoding\":\"little-endian IEEE754\"}\n";s.text("metadata.json",meta.str());
        // Called on the Y solver thread. safe() swallows diagnostic I/O failures;
        // a failure leaves no COMPLETE marker and cannot trigger solver rollback.
        y.modelObserver=[&s](const Eigen::SparseMatrix<double>&a,const Eigen::VectorXd&b){s.safe([&]{writeEffective(s.current/"effective-y.bin",a,b,s.budget);});};
    });
}
void SllQpAudit::result(const char*state,const std::vector<double>&x,const std::vector<double>&y,QPSolverWrapper&solver){
    auto&s=*p;solver.modelObserver={};
    s.safe([&]{Writer w(s.current/(std::string(state)+"-xy.bin"),s.budget);w.scalar<uint64_t>(x.size());w.array(x);w.array(y);w.finish();
        if(std::string(state)=="written"){
            if(!std::filesystem::exists(s.current/"effective-y.bin"))throw std::runtime_error("Solver did not expose an effective Y model");
            auto&d=solver.guardDiagnostics;std::ostringstream o;o<<std::setprecision(17)<<"{\"schema\":1,\"status\":\"complete\",\"qp\":"<<s.qid<<",\"diagnostic_seconds_before_marker\":"<<s.seconds<<",\"guarded\":"<<solver.solverSettings.stabilityGuard<<",\"repaired_rows\":"<<d.repairedRows<<",\"max_diagonal_delta\":"<<d.maxDiagonalDelta<<",\"converged\":"<<d.converged<<",\"rollback\":"<<d.rollback<<"}\n";s.text("COMPLETE.json.pending",o.str());std::filesystem::rename(s.current/"COMPLETE.json.pending",s.current/"COMPLETE.json");s.note("complete",s.current.string());s.active=false;
        }
    });
}
