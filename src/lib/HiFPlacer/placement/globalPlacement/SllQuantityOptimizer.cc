#include "SllQuantityOptimizer.h"
#include "SllQuantityModel.h"
#include "PlacementInfo.h"
#include "../placementInfo/SllClipTrace.h"
#include "QPSolverWrapper.h"
#include "../../../utils/RuntimeProfiler.h"
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <set>

namespace {
bool flag(const std::map<std::string,std::string>& c,const std::string& k,bool fallback=false) {
    auto i=c.find(k); if(i==c.end())return fallback;
    if(i->second!="true" && i->second!="false")throw std::invalid_argument(k+" must be true/false");
    return i->second=="true";
}
double number(const std::map<std::string,std::string>& c,const std::string& k,double fallback) {
    auto i=c.find(k); if(i==c.end())return fallback;
    size_t end=0;double v=std::stod(i->second,&end);
    if(end!=i->second.size() || !std::isfinite(v))throw std::invalid_argument("Invalid "+k);
    return v;
}
struct FrozenAxis {
    std::vector<double> reference, gradient;
    std::vector<float> diagonal;
    size_t entries=0;
    void capture(const QPSolverWrapper &solver,const std::vector<double> &r) {
        const auto &s=solver.solverData; reference=r; diagonal=s.objectiveMatrixDiag;
        entries=s.objectiveMatrixTripletList.size(); gradient.resize(r.size());
        for(size_t i=0;i<r.size();++i)gradient[i]=s.objectiveVector[i]+diagonal[i]*r[i];
        // Symmetric part also handles round-off asymmetry in the stored matrix.
        for(const auto &e:s.objectiveMatrixTripletList) {
            gradient[e.row()]+=0.5*e.value()*r[e.col()];
            gradient[e.col()]+=0.5*e.value()*r[e.row()];
        }
    }
    double change(const QPSolverWrapper &solver,const std::vector<double> &candidate)const {
        std::vector<double>d(candidate.size()); double value=0;
        for(size_t i=0;i<d.size();++i){d[i]=candidate[i]-reference[i];value+=gradient[i]*d[i]+0.5*diagonal[i]*d[i]*d[i];}
        // The unguarded solver can append diagonal triplets: use the frozen prefix.
        for(size_t j=0;j<entries;++j){const auto &e=solver.solverData.objectiveMatrixTripletList[j];value+=0.5*e.value()*d[e.row()]*d[e.col()];}
        return value;
    }
};
}

struct SllQuantityOptimizer::Impl {
    PlacementInfo *placement;
    amf_sll::Model model;
    bool enabled=false,active=false;
    double lambda=0,curvature=0.002,maxStep=10;
    int threads=1,iteration=-1; unsigned maxFanout=0;
    std::string policy="preserve",directory,phase;
    std::ofstream trace,qp,ledger;
    std::vector<unsigned char> movable;
    std::vector<double> previousY;
    FrozenAxis baseX,baseY;
    amf_sll::Result reference;
    size_t sequence=0,released=0;
    unsigned long long lastClipX=0,lastClipY=0;

    Impl(PlacementInfo *pi,const std::map<std::string,std::string>& cfg):placement(pi) {
        enabled=flag(cfg,"SllQuantityEnable");
        // Parse the diagnostics flag even when enabled, to reject misspellings.
        flag(cfg,"SllQuantityDiagnostics");
        lambda=number(cfg,"SllQuantityWeight",0);
        curvature=number(cfg,"SllQuantityCurvature",0.002);
        maxStep=number(cfg,"SllQuantityMaxStep",10);
        const double threadValue=number(cfg,"jobs",1),fanoutValue=number(cfg,"SllQuantityMaxFanout",0);
        if(threadValue<1 || threadValue>256 || threadValue!=std::floor(threadValue) || fanoutValue<0 || fanoutValue>100000000 || fanoutValue!=std::floor(fanoutValue))
            throw std::invalid_argument("Invalid SLL thread/fanout setting");
        threads=int(threadValue);maxFanout=unsigned(fanoutValue);
        if(lambda<0 || curvature<=0 || maxStep<=0)throw std::invalid_argument("Invalid SLL QP parameters");
        if(cfg.count("SllRegionPolicy"))policy=cfg.at("SllRegionPolicy");
        if(policy!="preserve" && policy!="release-single")throw std::invalid_argument("Invalid SllRegionPolicy");
        if(cfg.count("SllReportDirectory"))directory=cfg.at("SllReportDirectory");
        if(directory.empty())throw std::invalid_argument("SllReportDirectory is required for SLL diagnostics/optimization");
        auto physical=pi->getDeviceInfo()->getPhysicalBoundaryModel();
        if(!physical)throw std::invalid_argument("SLL quantity requires physical boundary metadata");
        const auto &regions=physical->getRegions();
        std::map<int,std::pair<double,double>> spans;
        for(const auto &r:regions) {
            auto inserted=spans.emplace(r.slr,std::make_pair(r.y0,r.y1));
            if(!inserted.second && inserted.first->second!=std::make_pair(double(r.y0),double(r.y1)))
                throw std::invalid_argument("SLL requires consistent vertical SLR spans");
        }
        std::vector<std::pair<double,double>> ordered;
        for(auto &s:spans)ordered.push_back(s.second);
        std::sort(ordered.begin(),ordered.end());
        if(ordered.size()!=4)throw std::invalid_argument("First SLL implementation supports validated 1x4 topology only");
        model.minY=ordered.front().first;model.maxY=ordered.back().second;
        for(size_t i=1;i<ordered.size();++i) {
            if(ordered[i-1].second!=ordered[i].first)throw std::invalid_argument("Non-contiguous SLR topology");
            model.boundaries.push_back(ordered[i].first);
        }
        std::vector<double> supplied;
        for(const auto &b:physical->getBoundaries())if(b.active && b.kind=="SLR" && b.orientation=='Y')supplied.push_back(b.coordinate);
        std::sort(supplied.begin(),supplied.end());
        if(supplied!=model.boundaries)throw std::invalid_argument("SLR seams disagree with region metadata");
        model.tau.assign(model.boundaries.size(),number(cfg,"SllQuantityTau",30));
        model.gamma=number(cfg,"SllQuantityGamma",0.25);
        std::filesystem::create_directories(directory);
        trace.open(directory+"/stages.tsv");qp.open(directory+"/qp.tsv");ledger.open(directory+"/mapping.tsv");
        if(!trace || !qp || !ledger)throw std::runtime_error("Cannot open SLL reports");
        sll_clip_trace::enabled=true;
        trace<<std::setprecision(17)<<"seq\tphase\titeration\tstep\tS\tS_optimized\tsmooth\tinvalid_nets\tvalid_nets\tseam0\tseam1\tseam2\tpu_slr_moves\treleased_preferences\tx_projections\ty_projections\n";
        qp<<std::setprecision(17)<<"seq\tphase\titeration\taccepted\talpha\tbacktracks\tbase_qp_delta\tsmooth_delta\tcombined_delta\tS_before\tS_after\tinvalid_before\tinvalid_after\tgrad_norm\tmax_y_move\n";
        ledger<<"reload\tunits\tnets\tpins\tglobal_marked_excluded\tpower_excluded\tincomplete_nets\toptimization_excluded\tinternal_pu_nets\n";
        std::ofstream meta(directory+"/parameters.tsv");meta<<std::setprecision(17);
        meta<<"enabled\t"<<enabled<<"\nweight\t"<<lambda<<"\ncurvature\t"<<curvature<<"\ntau\t"<<model.tau[0]<<"\ngamma\t"<<model.gamma<<"\nmax_step\t"<<maxStep<<"\nthreads\t"<<threads<<"\nregion_policy\t"<<policy<<"\nmax_fanout\t"<<maxFanout<<"\nmin_y\t"<<model.minY<<"\nmax_y\t"<<model.maxY<<"\n";
        for(size_t i=0;i<model.boundaries.size();++i)meta<<"seam"<<i<<'\t'<<model.boundaries[i]<<'\n';
        for(const auto &kv:physical->getMetadata())meta<<"physical_"<<kv.first<<'\t'<<kv.second<<'\n';
    }
    std::vector<double> coordinates(bool y)const {
        std::vector<double> out(model.units);
        for(auto pu:placement->getPlacementUnits())out.at(pu->getId())=y?pu->Y():pu->X();
        return out;
    }
    void updateMovable() {
        movable.assign(model.units,0);
        for(auto pu:placement->getPlacementUnits())movable.at(pu->getId())=!pu->isFixed()&&!pu->isLocked();
    }
    void releaseSingles() {
        if(policy!="release-single")return;
        auto &preferences=placement->getRegionPreferences();
        for(auto it=preferences.begin();it!=preferences.end();) {
            auto pu=it->first;
            // Experimental control: release only heuristic preferences of a
            // movable, unpacked LUT/FF. Macro/site/cascade/fixed constraints stay.
            if(!pu->isFixed() && !pu->isLocked() &&
               dynamic_cast<PlacementInfo::PlacementUnpackedCell*>(pu) &&
               (pu->checkHasLUT() || pu->checkHasFF()) && !pu->checkHasLUTRAM()) {
                it=preferences.erase(it);++released;
            } else ++it;
        }
    }
};

SllQuantityOptimizer::SllQuantityOptimizer(PlacementInfo *pi,const std::map<std::string,std::string>& c):p(new Impl(pi,c)){}
SllQuantityOptimizer::~SllQuantityOptimizer()=default;
void SllQuantityOptimizer::reload() {
    AMF_PROFILE_FUNCTION("sll_mapping");
    auto &s=*p;auto &m=s.model;m.units=s.placement->getPlacementUnits().size();m.pins.clear();m.nets.clear();
    size_t global=0,power=0,incomplete=0,excluded=0,internal=0;
    for(auto net:s.placement->getDesignInfo()->getNets()) {
        if(net->checkIsPowerNet()){++power;continue;}
        if(net->checkIsGlobalClock()){++global;continue;}
        const size_t begin=m.pins.size();bool complete=true;std::set<unsigned> owners;
        for(auto pin:net->getPins()) {
            auto cell=pin?pin->getCell():nullptr;
            if(!cell || cell->isVirtualCell()){complete=false;break;}
            auto pu=s.placement->getPlacementUnitByCellId(cell->getCellId());
            if(!pu){complete=false;break;}
            double offset=pin->getOffsetYInCell();
            if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu))offset+=macro->getCellOffsetYInMacro(cell);
            m.pins.push_back({unsigned(pu->getId()),offset});owners.insert(pu->getId());
        }
        if(!complete){m.pins.resize(begin);++incomplete;continue;}
        if(m.pins.size()==begin)continue;
        const bool optimize=net->getDriverPins().size()==1 && m.pins.size()-begin>=2 && (!s.maxFanout || m.pins.size()-begin<=s.maxFanout);
        if(!optimize)++excluded;if(owners.size()==1)++internal;
        m.nets.push_back({begin,m.pins.size(),unsigned(net->getElementIdInType()),optimize,1.0});
    }
    m.validate();s.updateMovable();s.previousY.clear();s.active=false;
    s.ledger<<s.sequence<<'\t'<<m.units<<'\t'<<m.nets.size()<<'\t'<<m.pins.size()<<'\t'<<global<<'\t'<<power<<'\t'<<incomplete<<'\t'<<excluded<<'\t'<<internal<<std::endl;
}
void SllQuantityOptimizer::record(const char *phase,int iteration,const char *step) {
    AMF_PROFILE_FUNCTION("sll_diagnostics");
    auto &s=*p;s.phase=phase;s.iteration=iteration;s.active=true;
    if(std::string(step)=="begin" || std::string(step)=="after_timing")s.releaseSingles();
    s.updateMovable();auto y=s.coordinates(true);auto r=s.model.evaluate(y,s.movable,s.threads,false);
    size_t moved=0;
    if(s.previousY.size()==y.size())for(size_t i=0;i<y.size();++i)moved+=s.model.slr(y[i])!=s.model.slr(s.previousY[i]);
    s.previousY=y;
    s.trace<<++s.sequence<<'\t'<<phase<<'\t'<<iteration<<'\t'<<step<<'\t'<<r.discrete<<'\t'<<r.discreteOptimized<<'\t'<<r.smooth<<'\t'<<r.invalidNets<<'\t'<<r.validNets;
    for(auto v:r.seams)s.trace<<'\t'<<v;
    const auto cx=sll_clip_trace::x.load(),cy=sll_clip_trace::y.load();
    s.trace<<'\t'<<moved<<'\t'<<s.released<<'\t'<<cx-s.lastClipX<<'\t'<<cy-s.lastClipY<<std::endl;
    s.lastClipX=cx;s.lastClipY=cy;
}
void SllQuantityOptimizer::endPhase(){p->active=false;}
bool SllQuantityOptimizer::prepare(QPSolverWrapper &x,QPSolverWrapper &y) {
    AMF_PROFILE_FUNCTION("sll_qp_model");
    auto &s=*p;
    if(!s.active || !s.enabled || s.lambda==0)return false;
    s.updateMovable();auto xx=s.coordinates(false),yy=s.coordinates(true);
    s.reference=s.model.evaluate(yy,s.movable,s.threads,true);
    s.baseX.capture(x,xx);s.baseY.capture(y,yy);
    std::vector<double> diagonal(y.solverData.objectiveMatrixDiag.begin(),y.solverData.objectiveMatrixDiag.end());
    std::vector<double> linear(y.solverData.objectiveVector.data(),y.solverData.objectiveVector.data()+yy.size());
    amf_sll::addLocalModel(yy,s.reference.gradient,s.movable,s.lambda,s.curvature,diagonal,linear);
    for(size_t i=0;i<yy.size();++i){y.solverData.objectiveMatrixDiag[i]=diagonal[i];y.solverData.objectiveVector[i]=linear[i];}
    return true;
}
void SllQuantityOptimizer::accept(QPSolverWrapper &x,QPSolverWrapper &y,float displacementLimit) {
    AMF_PROFILE_FUNCTION("sll_qp_accept");
    auto &s=*p;auto rawX=s.baseX.reference,rawY=s.baseY.reference;
    const auto &solutionX=x.solverSettings.solutionForward?x.solverData.oriSolution:x.solverData.solution;
    const auto &solutionY=y.solverSettings.solutionForward?y.solverData.oriSolution:y.solverData.solution;
    // Project the solver's candidate without mutating PU positions or history.
    // The baseline's setAnchorLocation updates lastAnchor, which is used by the
    // next pseudo-net model; a trial writeback followed by correction is unsafe.
    for(auto pu:s.placement->getPlacementUnits()) {
        const size_t i=pu->getId();if(!s.movable[i])continue;
        float fx=solutionX[i],fy=solutionY[i];
        if(!std::isfinite(fx)||!std::isfinite(fy))throw std::runtime_error("Non-finite SLL QP solution");
        if(displacementLimit>0) {
            const float dx=std::fabs(fx-pu->X()),dy=std::fabs(fy-pu->Y());
            const float distance=std::sqrt(dx*dx+dy*dy);
            if(distance>displacementLimit) {
                const float ratio=displacementLimit/distance;
                fx=pu->X()+(fx-pu->X())*ratio;fy=pu->Y()+(fy-pu->Y())*ratio;
            }
        }
        s.placement->legalizeXYInArea(pu,fx,fy);rawX[i]=fx;rawY[i]=fy;
        rawY[i]=std::max(s.baseY.reference[i]-s.maxStep,std::min(s.baseY.reference[i]+s.maxStep,rawY[i]));
    }
    bool accepted=false;double alpha=1,baseDelta=0,smoothDelta=0,total=0;int bt=0;
    auto chosenX=s.baseX.reference,chosenY=s.baseY.reference;auto result=s.reference;
    for(bt=0;bt<16;++bt,alpha*=0.5) {
        auto trialX=s.baseX.reference,trialY=s.baseY.reference;
        for(auto pu:s.placement->getPlacementUnits()) {
            const size_t i=pu->getId();if(!s.movable[i])continue;
            float fx=s.baseX.reference[i]+alpha*(rawX[i]-s.baseX.reference[i]);
            float fy=s.baseY.reference[i]+alpha*(rawY[i]-s.baseY.reference[i]);
            if(!std::isfinite(fx)||!std::isfinite(fy))throw std::runtime_error("Non-finite SLL QP candidate");
            s.placement->legalizeXYInArea(pu,fx,fy);trialX[i]=fx;trialY[i]=fy;
        }
        auto candidate=s.model.evaluate(trialY,s.movable,s.threads,false);
        double q=s.baseX.change(x,trialX)+s.baseY.change(y,trialY);
        double c=candidate.smooth-s.reference.smooth;
        double f=q+s.lambda*c;
        if(candidate.invalidNets<=s.reference.invalidNets && std::isfinite(f) && f<=1e-8) {
            accepted=true;baseDelta=q;smoothDelta=c;total=f;chosenX=std::move(trialX);chosenY=std::move(trialY);result=std::move(candidate);break;
        }
    }
    if(!accepted)alpha=0;
    double gn=0,maxMove=0;
    for(auto pu:s.placement->getPlacementUnits()) {
        const size_t i=pu->getId();gn+=s.reference.gradient[i]*s.reference.gradient[i];maxMove=std::max(maxMove,std::abs(chosenY[i]-s.baseY.reference[i]));
        // Exactly one committed writeback, and none on rejection/fixed/locked PUs.
        if(accepted && s.movable[i])pu->setAnchorLocation(chosenX[i],chosenY[i]);
        x.solverData.oriSolution[i]=x.solverData.solution[i]=chosenX[i];
        y.solverData.oriSolution[i]=y.solverData.solution[i]=chosenY[i];
    }
    s.qp<<s.sequence<<'\t'<<s.phase<<'\t'<<s.iteration<<'\t'<<accepted<<'\t'<<alpha<<'\t'<<bt<<'\t'<<baseDelta<<'\t'<<smoothDelta<<'\t'<<total<<'\t'<<s.reference.discrete<<'\t'<<result.discrete<<'\t'<<s.reference.invalidNets<<'\t'<<result.invalidNets<<'\t'<<std::sqrt(gn)<<'\t'<<maxMove<<std::endl;
}
