#include "SllRunDiagnostics.h"
#include "SllDiagnosticMath.h"
#include "SllAdaptivePolicy.h"
#include "SllQpAudit.h"
#include "PlacementInfo.h"
#include "../../../utils/RuntimeProfiler.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <numeric>
#include <chrono>

namespace {
bool flag(const std::map<std::string,std::string>& c,const std::string& key,bool fallback=false) {
    auto it=c.find(key);if(it==c.end())return fallback;
    if(it->second!="true" && it->second!="false")throw std::invalid_argument(key+" must be true/false");
    return it->second=="true";
}
double number(const std::map<std::string,std::string>& c,const std::string& key,double fallback) {
    auto it=c.find(key);if(it==c.end())return fallback;
    size_t end=0;double v=std::stod(it->second,&end);
    if(end!=it->second.size() || !std::isfinite(v))throw std::invalid_argument(key+" must be finite");
    return v;
}
std::string safe(std::string s) {for(char& c:s)if(c=='\t'||c=='\n'||c=='\r')c=' ';return s;}
double percentile(std::vector<double> v,double f) {
    if(v.empty())return 0;size_t i=size_t(f*(v.size()-1));std::nth_element(v.begin(),v.begin()+i,v.end());return v[i];
}
double norm(const std::vector<double>&v,const std::vector<unsigned char>&m) {
    double s=0;for(size_t i=0;i<v.size();++i)if(m[i])s+=v[i]*v[i];return std::sqrt(s);
}
uint64_t stableHash(const std::string&s) {uint64_t h=1469598103934665603ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}return h;}
}

struct SllRunDiagnostics::Impl {
    PlacementInfo* placement;amf_sll::Model model;
    bool enabled=false, globalActive=false, prepared=false, dynamicTau=false, shortSpan=false;
    amf_sll::TauSchedule schedule;
    amf_sll::ShortSpanPolicy shortPolicy;
    amf_sll::ShortSpanPolicy::Update exclusion;
    bool haveRoundS=false;std::int64_t previousRoundS=0;
    std::map<std::string,std::pair<size_t,double>> timingTotals;
    double timingLogSeconds=0;
    int threads=1,round=0,iteration=-1;size_t qid=0,event=0;
    double ratio=1,coefficient=60,pseudoWeight=0,limit=-10;
    std::string directory,samplePath,phase="initial";
    std::vector<unsigned char> movable;
    std::vector<DesignInfo::DesignNet*> designNets;
    std::vector<DesignInfo::DesignPin*> designPins;
    std::vector<double> xOffsets,rawX,rawY,strength;
    std::set<std::string> fixedNames;std::set<size_t> selected,top;
    std::unique_ptr<SllQpAudit> audit;
    amf_sll::FrozenAxis x,y;amf_sll::Result before;
    std::ofstream qp,pins,nets,stages,solver,mapping,rounds,shortReport,perf;
    std::chrono::steady_clock::time_point solveStarted,writebackStarted;
    std::chrono::steady_clock::time_point origin=std::chrono::steady_clock::now();
    double seconds()const{return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();}
    Impl(PlacementInfo*pi,const std::map<std::string,std::string>&cfg,double r):placement(pi),ratio(r) {
        enabled=flag(cfg,"SllQuantityEnable");flag(cfg,"SllQuantityDiagnostics");
        dynamicTau=flag(cfg,"SllDynamicTauEnable");shortSpan=flag(cfg,"SllShortSpanEnable");
        amf_sll::rejectDeprecatedParameters(cfg);coefficient=amf_sll::firstOrderCoefficient(r);
        if(cfg.count("SllQuantityMaxStep"))throw std::invalid_argument("SllQuantityMaxStep is unsupported in original-writeback diagnostic mode");
        if(cfg.count("SllRegionPolicy") && cfg.at("SllRegionPolicy")!="preserve")
            throw std::invalid_argument("This visualization experiment preserves existing region policy; release-single remains in its separate experimental source");
        double jobs=number(cfg,"jobs",12);if(jobs<1||jobs>256||jobs!=std::floor(jobs))throw std::invalid_argument("Invalid jobs");threads=int(jobs);
        auto physical=pi->getDeviceInfo()->getPhysicalBoundaryModel();if(!physical)throw std::invalid_argument("SLL needs physical boundaries");
        std::map<int,std::pair<double,double>> spans;
        for(const auto&rgn:physical->getRegions()) {
            auto v=std::make_pair(double(rgn.y0),double(rgn.y1));auto in=spans.emplace(rgn.slr,v);
            if(!in.second && in.first->second!=v)throw std::invalid_argument("Inconsistent SLR spans");
        }
        std::vector<std::pair<double,double>> ordered;for(auto&s:spans)ordered.push_back(s.second);
        std::sort(ordered.begin(),ordered.end());if(ordered.size()!=4)throw std::invalid_argument("Requires validated 1x4 topology");
        model.minY=ordered.front().first;model.maxY=ordered.back().second;
        for(size_t i=1;i<ordered.size();++i){if(ordered[i-1].second!=ordered[i].first)throw std::invalid_argument("Noncontiguous SLR spans");model.boundaries.push_back(ordered[i].first);}
        std::vector<double> seams;for(const auto&b:physical->getBoundaries())if(b.active&&b.kind=="SLR"&&b.orientation=='Y')seams.push_back(b.coordinate);
        std::sort(seams.begin(),seams.end());if(seams!=model.boundaries)throw std::invalid_argument("SLR boundary mismatch");
        model.tau.assign(model.boundaries.size(),number(cfg,"SllQuantityTau",10));model.gamma=number(cfg,"SllQuantityGamma",0.02);
        if(dynamicTau && model.tau[0]!=10)throw std::invalid_argument("Dynamic tau rule v1 must start at tau=10");
        directory=cfg.at("SllReportDirectory");samplePath=cfg.at("SllTraceNetList");
        std::filesystem::create_directories(directory);
        audit.reset(new SllQpAudit(pi,cfg));
        auto open=[&](std::ofstream&out,const char*file,const char*header){
            std::string path=directory+"/"+file;
            if(std::filesystem::exists(path))throw std::runtime_error("Refusing to overwrite diagnostics "+path);
            out.open(path);out<<std::setprecision(17)<<header<<'\n';if(!out)throw std::runtime_error("Cannot open "+path);
        };
        open(qp,"qp.tsv","qp\tphase\tround\titeration\tstate\tseconds\tinjected\tpseudo_weight\toriginal_limit\tF\tS\tinvalid_nets\tdQx\tdQy\tdF\tc_dF\tdJ\tlinear_model_delta\tbase_gradient_dot_step\tsll_gradient_dot_step\tcombined_gradient_dot_step\tbase_gradient_y_norm\tsll_gradient_y_norm\tcombined_gradient_y_norm\tsll_gradient_y_max\tsll_gradient_y_p95\topposed_y_coordinates\tsll_dominant_y_coordinates\tmax_xy_move\tp95_xy_move\tmax_y_move\tp95_y_move\traw_to_written_changed\traw_exceeds_original_limit\ttau\texcluded_nets\tF_active_frozen_mask\tdF_active_frozen_mask\tc_dF_active_frozen_mask\tdJ_active_frozen_mask");
        open(pins,"net-pins.tsv","event\tqp\tphase\tround\titeration\tstate\tseconds\tselection\tnet_id\tnet\tpin_index\tpin\tcell\tpu\tmovable\tx\ty\toffset_y\tz\tdz_dy\tp_plus\tp_minus\tdF_dpin_y\tc_dF_dpin_y\tpu_sll_gradient_at_qp_start\tpu_base_gradient_at_qp_start\tpu_dy_from_qp_start\tnet_F\tnet_S\textremum\tsll_gradient_active\tc_active_dF_dpin_y");
        open(nets,"net-stages.tsv","event\tqp\tphase\tround\titeration\tstate\tselection\tnet_id\tnet\tpins\tF\tS\tweighted_hpwl\tmin_y\tmax_y\tsll_gradient_active");
        open(stages,"stages.tsv","event\tqp\tphase\tround\titeration\tstep\tseconds\tF\tS\tS_optimized\tinvalid_nets\tvalid_nets\tseam0\tseam1\tseam2\tmapped_hpwl_x_plus_r_y\thpwl_plus_cF\ttau\texcluded_nets_at_last_qp");
        open(solver,"solver.tsv","qp\tphase\tround\titeration\taxis\titerations\trelative_error\tconverged\trollback\trepaired_rows\tmax_diagonal_delta\tconfigured_tolerance\tconfigured_max_iterations\tbase_rhs_norm\tsll_b_increment_norm");
        open(mapping,"mapping.tsv","reload\tunits\tnets\tpins\tpower_excluded\tglobal_excluded\tincomplete\toptimization_excluded");
        open(rounds,"adaptive-rounds.tsv","round\tphase\titeration\ttau\tnext_tau\tS\tdelta_S\tinvalid_nets\tF_full\tH\tH_plus_cF_full\texcluded_nets_at_last_qp\tmedian_S\tentry_median_S\tinitial_median_S\tcompleted_rounds\theld_rounds\tphase_rounds\tdecision");
        open(shortReport,"short-span.tsv","qp\tphase\tround\titeration\ttau\tpolicy_applied\texcluded_nets\tentered\tresumed\texcluded_S\tS_full\tinvalid_nets\tdiscrete_count_seconds\tlist_update_seconds");
        open(perf,"sll-overhead.tsv","event\tqp\tphase\tround\titeration\tstate\ttau\tcategory\twall_seconds");
        std::ofstream meta(directory+"/parameters.tsv");meta<<std::setprecision(17)
            <<"model\tsigmoid_wa_first_order_original_writeback_v1\nenabled\t"<<enabled<<"\ntau\t"<<model.tau[0]<<"\ngamma\t"<<model.gamma
            <<"\nsll_length_tiles\t60\ny2x_ratio\t"<<ratio<<"\nlength_coefficient\t"<<coefficient<<"\nextra_y_limit\tfalse\nbacktracking\tfalse\nJ_increase_policy\trecord_only\ninjection_scope\tglobal_placement_after_common_initial_QP\nregion_policy\tpreserve\nnet_selection\t64_common_stratified_plus_8_dynamic\n"
            <<"dynamic_tau_enabled\t"<<dynamicTau<<"\nshort_span_enabled\t"<<shortSpan
            <<"\nadaptive_rule_version\t1\ntau_floor\t0.2\ntau_factor\t0.5\ninitial_hold_rounds\t6\nminimum_level_hold_rounds\t4\nphase_settling_rounds\t2\nmedian_window_rounds\t3\nlevel_drop_fraction\t0.1\nplateau_fraction\t0.01\nshort_span_enter_y\t2\nshort_span_resume_y\t3\nshort_span_required_crossings\t1\nmask_update\tbefore_each_QP_frozen_through_writeback\nfull_statistics\tinclude_temporarily_excluded_nets\nround_statistic\tafter_legalization\nS_meaning\tdiscrete_vertical_boundary_demand_not_routed_usage\ntiming_scope\twall_seconds_nonoverlapping_categories_except_explicit_total\n";
    }
    ~Impl() {
        // Destructor must not throw. Per-event measurements remain in the TSV.
        std::ofstream summary(directory+"/sll-overhead-summary.tsv");summary<<std::setprecision(17)<<"category\tcalls\twall_seconds\n";
        for(const auto&t:timingTotals)summary<<t.first<<'\t'<<t.second.first<<'\t'<<t.second.second<<'\n';
        summary<<"timing_log_write\t"<<0<<'\t'<<timingLogSeconds<<'\n';
    }
    void timing(const char*category,const char*state,double elapsed) {
        auto started=std::chrono::steady_clock::now();auto&t=timingTotals[category];++t.first;t.second+=elapsed;
        perf<<event<<'\t'<<qid<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<state<<'\t'<<model.tau[0]<<'\t'<<category<<'\t'<<elapsed<<std::endl;
        if(!perf)throw std::runtime_error("SLL overhead report write failed");
        timingLogSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    }
    void evaluationTiming(const char*state,const amf_sll::Result&r) {
        timing("discrete_count",state,r.discreteSeconds);timing("smooth_and_gradient",state,r.smoothGradientSeconds);
    }
    void roundComplete(const amf_sll::Result&r,double h) {
        auto d=dynamicTau?schedule.finish(r.discrete,r.invalidNets==0):amf_sll::TauSchedule::Decision{};
        if(!dynamicTau){d.tau=d.nextTau=model.tau[0];d.reason="disabled";}
        rounds<<round<<'\t'<<phase<<'\t'<<iteration<<'\t'<<d.tau<<'\t'<<d.nextTau<<'\t'<<r.discrete<<'\t';
        if(haveRoundS)rounds<<r.discrete-previousRoundS;else rounds<<"nan";
        rounds<<'\t'<<r.invalidNets<<'\t'<<r.smooth<<'\t'<<h<<'\t'<<h+coefficient*r.smooth<<'\t'<<exclusion.excluded<<'\t';
        if(d.hasMedian)rounds<<d.median;else rounds<<"nan";rounds<<'\t';
        if(d.hasEntry)rounds<<d.entry;else rounds<<"nan";rounds<<'\t';
        if(d.hasStart)rounds<<d.start;else rounds<<"nan";
        rounds<<'\t'<<d.completed<<'\t'<<d.held<<'\t'<<d.phaseRounds<<'\t'<<d.reason<<std::endl;
        if(!rounds)throw std::runtime_error("SLL round report write failed");
        haveRoundS=r.invalidNets==0;previousRoundS=r.discrete;
    }
    std::vector<double> coordinates(bool isY)const {
        std::vector<double> a(model.units);for(auto pu:placement->getPlacementUnits())a.at(pu->getId())=isY?pu->Y():pu->X();return a;
    }
    void mask(){movable.assign(model.units,0);for(auto pu:placement->getPlacementUnits())movable.at(pu->getId())=!pu->isFixed()&&!pu->isLocked();}
    double hpwl(size_t ni,const std::vector<double>&xx,const std::vector<double>&yy)const {
        const auto&n=model.nets[ni];double xmin=INFINITY,xmax=-INFINITY,ymin=INFINITY,ymax=-INFINITY;
        for(size_t j=n.begin;j<n.end;++j){auto&p=model.pins[j];double px=xx[p.unit]+xOffsets[j],py=yy[p.unit]+p.offsetY;xmin=std::min(xmin,px);xmax=std::max(xmax,px);ymin=std::min(ymin,py);ymax=std::max(ymax,py);}
        return n.begin==n.end?0:xmax-xmin+ratio*(ymax-ymin);
    }
    void select(const std::vector<double>&yy) {
        if(!fixedNames.empty())return;
        if(std::filesystem::exists(samplePath)) {
            std::ifstream f(samplePath);std::string name;while(std::getline(f,name))if(!name.empty())fixedNames.insert(name);
            if(fixedNames.empty())throw std::runtime_error("Empty common net sample");
        } else {
            std::map<int,std::vector<std::pair<uint64_t,size_t>>> buckets;
            for(size_t i=0;i<model.nets.size();++i){auto&n=model.nets[i];size_t count=n.end-n.begin;if(!n.optimize||count<2||count>128)continue;
                int lo=4,hi=0;double nearest=INFINITY;
                for(size_t j=n.begin;j<n.end;++j){double py=yy[model.pins[j].unit]+model.pins[j].offsetY;lo=std::min(lo,model.slr(py));hi=std::max(hi,model.slr(py));for(double b:model.boundaries)nearest=std::min(nearest,std::abs(py-b));}
                int fan=count==2?0:(count<=8?1:2), key=(fan*4+(hi-lo))*2+(nearest<=3*model.tau[0]);
                buckets[key].push_back({stableHash(designNets[i]->getName()),i});
            }
            for(auto&b:buckets)std::sort(b.second.begin(),b.second.end());
            for(size_t depth=0;fixedNames.size()<64;++depth){bool any=false;for(auto&b:buckets)if(depth<b.second.size()&&fixedNames.size()<64){fixedNames.insert(designNets[b.second[depth].second]->getName());any=true;}if(!any)break;}
            std::ofstream f(samplePath);for(auto&name:fixedNames)f<<name<<'\n';if(!f)throw std::runtime_error("Cannot write common net sample");
        }
        selected.clear();for(size_t i=0;i<designNets.size();++i)if(fixedNames.count(designNets[i]->getName()))selected.insert(i);
        if(selected.size()!=fixedNames.size())throw std::runtime_error("Common net sample identity mismatch");
    }
    void details(const char*state,const std::vector<double>&xx,const std::vector<double>&yy) {
        std::set<size_t> all=selected;all.insert(top.begin(),top.end());
        for(size_t ni:all){auto&n=model.nets[ni];auto d=amf_sll::detail(model,n,yy);auto net=designNets[ni];const char*kind=selected.count(ni)?"fixed":"dynamic";
            nets<<event<<'\t'<<qid<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<state<<'\t'<<kind<<'\t'<<n.id<<'\t'<<safe(net->getName())<<'\t'<<n.end-n.begin<<'\t'<<d.value<<'\t'<<d.discrete<<'\t'<<hpwl(ni,xx,yy)<<'\t'<<d.minimum<<'\t'<<d.maximum<<'\t'<<n.gradientActive<<'\n';
            // Dynamic high-fanout records retain all pins; no truncated gradient is used.
            for(size_t j=0;j<d.y.size();++j){auto&p=model.pins[n.begin+j];auto pin=designPins[n.begin+j];
                bool ref=prepared && before.gradient.size()==model.units;
                pins<<event<<'\t'<<qid<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<state<<'\t'<<seconds()<<'\t'<<kind<<'\t'<<n.id<<'\t'<<safe(net->getName())<<'\t'<<j<<'\t'<<safe(pin->getName())<<'\t'<<safe(pin->getCell()->getName())<<'\t'<<p.unit<<'\t'<<int(movable[p.unit])<<'\t'<<xx[p.unit]+xOffsets[n.begin+j]<<'\t'<<d.y[j]<<'\t'<<p.offsetY<<'\t'<<d.z[j]<<'\t'<<d.dz[j]<<'\t'<<d.pplus[j]<<'\t'<<d.pminus[j]<<'\t'<<d.gradient[j]<<'\t'<<coefficient*d.gradient[j]<<'\t';
                if(ref)pins<<coefficient*before.gradient[p.unit]<<'\t'<<y.gradient[p.unit]<<'\t'<<yy[p.unit]-y.position[p.unit];else pins<<"nan\tnan\tnan";
                pins<<'\t'<<d.value<<'\t'<<d.discrete<<'\t'<<(d.y[j]==d.minimum&&d.y[j]==d.maximum?"both":d.y[j]==d.minimum?"min":d.y[j]==d.maximum?"max":"interior")<<'\t'<<n.gradientActive<<'\t'<<(n.gradientActive?coefficient*d.gradient[j]:0)<<'\n';
            }
        }
        pins.flush();nets.flush();if(!pins||!nets)throw std::runtime_error("SLL trace write failed");
    }
    void result(const char*state,const std::vector<double>&xx,const std::vector<double>&yy) {
        amf_sll::Result evaluated;const bool reuse=std::string(state)=="before";
        if(!reuse){evaluated=model.evaluate(yy,movable,threads,false);evaluationTiming(state,evaluated);}
        const auto&f=reuse?before:evaluated;auto metricsStarted=std::chrono::steady_clock::now();double qx=x.change(xx),qy=y.change(yy),baseDot=0,sllDot=0;
        std::vector<double> moves,ymoves,gs,gq,gt;gs.resize(model.units);gq=y.gradient;gt.resize(model.units);
        size_t opposed=0,dominant=0,changed=0,exceeds=0;
        for(size_t i=0;i<model.units;++i)if(movable[i]) {
            double dx=xx[i]-x.position[i],dy=yy[i]-y.position[i];
            baseDot+=x.gradient[i]*dx+y.gradient[i]*dy;sllDot+=coefficient*before.gradient[i]*dy;
            gs[i]=coefficient*before.gradient[i];gt[i]=gq[i]+gs[i];opposed+=gq[i]*gs[i]<0;dominant+=std::abs(gs[i])>std::abs(gq[i]);
            moves.push_back(std::hypot(dx,dy));ymoves.push_back(std::abs(dy));
            if(rawX.size()==model.units){changed+=xx[i]!=rawX[i]||yy[i]!=rawY[i];exceeds+=limit>0&&std::hypot(rawX[i]-x.position[i],rawY[i]-y.position[i])>limit;}
        }
        std::vector<double> absGs;for(size_t i=0;i<gs.size();++i)if(movable[i])absGs.push_back(std::abs(gs[i]));
        double df=f.smooth-before.smooth,adf=f.activeSmooth-before.activeSmooth;
        timing("qp_metrics",state,std::chrono::duration<double>(std::chrono::steady_clock::now()-metricsStarted).count());
        auto logStarted=std::chrono::steady_clock::now();
        qp<<qid<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<state<<'\t'<<seconds()<<'\t'<<(enabled&&globalActive)<<'\t'<<pseudoWeight<<'\t'<<limit<<'\t'<<f.smooth<<'\t'<<f.discrete<<'\t'<<f.invalidNets<<'\t'<<qx<<'\t'<<qy<<'\t'<<df<<'\t'<<coefficient*df<<'\t'<<qx+qy+coefficient*df<<'\t'<<qx+qy+sllDot<<'\t'<<baseDot<<'\t'<<sllDot<<'\t'<<baseDot+sllDot<<'\t'<<norm(gq,movable)<<'\t'<<norm(gs,movable)<<'\t'<<norm(gt,movable)<<'\t'<<percentile(absGs,1)<<'\t'<<percentile(absGs,.95)<<'\t'<<opposed<<'\t'<<dominant<<'\t'<<percentile(moves,1)<<'\t'<<percentile(moves,.95)<<'\t'<<percentile(ymoves,1)<<'\t'<<percentile(ymoves,.95)<<'\t'<<changed<<'\t'<<exceeds<<'\t'<<model.tau[0]<<'\t'<<exclusion.excluded<<'\t'<<f.activeSmooth<<'\t'<<adf<<'\t'<<coefficient*adf<<'\t'<<qx+qy+coefficient*adf<<std::endl;
        if(!qp)throw std::runtime_error("SLL QP report write failed");details(state,xx,yy);
        timing("trace_and_log",state,std::chrono::duration<double>(std::chrono::steady_clock::now()-logStarted).count());
    }
};

SllRunDiagnostics::SllRunDiagnostics(PlacementInfo*pi,const std::map<std::string,std::string>&cfg,double ratio):p(new Impl(pi,cfg,ratio)){reload();}
SllRunDiagnostics::~SllRunDiagnostics()=default;
void SllRunDiagnostics::reload(){
    AMF_PROFILE_FUNCTION("sll_mapping");auto&s=*p;auto&m=s.model;m.units=s.placement->getPlacementUnits().size();m.pins.clear();m.nets.clear();s.designNets.clear();s.designPins.clear();s.xOffsets.clear();s.top.clear();s.selected.clear();s.prepared=false;
    size_t power=0,global=0,incomplete=0,excluded=0;
    for(auto net:s.placement->getDesignInfo()->getNets()) {
        if(net->checkIsPowerNet()){++power;continue;}if(net->checkIsGlobalClock()){++global;continue;}
        size_t begin=m.pins.size();bool complete=true;
        for(auto pin:net->getPins()) {
            auto cell=pin?pin->getCell():nullptr;if(!cell||cell->isVirtualCell()){complete=false;break;}
            auto pu=s.placement->getPlacementUnitByCellId(cell->getCellId());if(!pu){complete=false;break;}
            double oy=pin->getOffsetYInCell(),ox=pin->getOffsetXInCell();
            if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)){oy+=macro->getCellOffsetYInMacro(cell);ox+=macro->getCellOffsetXInMacro(cell);}
            m.pins.push_back({unsigned(pu->getId()),oy});s.xOffsets.push_back(ox);s.designPins.push_back(pin);
        }
        if(!complete){m.pins.resize(begin);s.xOffsets.resize(begin);s.designPins.resize(begin);++incomplete;continue;}
        if(m.pins.size()==begin)continue;
        bool optimize=net->getDriverPins().size()==1&&m.pins.size()-begin>=2;if(!optimize)++excluded;
        m.nets.push_back({begin,m.pins.size(),unsigned(net->getElementIdInType()),optimize,1.0});s.designNets.push_back(net);
    }
    for(auto&n:m.nets)n.gradientActive=!s.shortPolicy.excluded(n.id);
    m.validate();s.mask();
    for(size_t i=0;i<s.designNets.size();++i)if(s.fixedNames.count(s.designNets[i]->getName()))s.selected.insert(i);
    if(!s.fixedNames.empty()&&s.selected.size()!=s.fixedNames.size())throw std::runtime_error("Tracked net disappeared during PU remapping");
    s.mapping<<s.event<<'\t'<<m.units<<'\t'<<m.nets.size()<<'\t'<<m.pins.size()<<'\t'<<power<<'\t'<<global<<'\t'<<incomplete<<'\t'<<excluded<<std::endl;
}
void SllRunDiagnostics::stage(const char*phase,int round,int iteration,const char*step){
    AMF_PROFILE_FUNCTION("sll_stage_diagnostics");auto&s=*p;s.phase=phase;s.round=round;s.iteration=iteration;s.globalActive=s.phase!="initial"&&s.phase!="final"&&s.phase!="outside_global";++s.event;
    if(s.dynamicTau && s.globalActive && std::string(step)=="begin"){
        double tau=s.schedule.begin(s.phase,round,iteration);std::fill(s.model.tau.begin(),s.model.tau.end(),tau);
    }
    auto started=std::chrono::steady_clock::now();s.mask();auto xx=s.coordinates(false),yy=s.coordinates(true);
    if(s.globalActive)s.select(yy);
    s.timing("coordinates_and_selection",step,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    auto r=s.model.evaluate(yy,s.movable,s.threads,false);s.evaluationTiming(step,r);
    started=std::chrono::steady_clock::now();double h=0;for(size_t i=0;i<s.model.nets.size();++i)h+=s.hpwl(i,xx,yy);
    s.timing("hpwl_scan",step,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    started=std::chrono::steady_clock::now();
    s.stages<<s.event<<'\t'<<s.qid<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<step<<'\t'<<s.seconds()<<'\t'<<r.smooth<<'\t'<<r.discrete<<'\t'<<r.discreteOptimized<<'\t'<<r.invalidNets<<'\t'<<r.validNets;
    for(auto n:r.seams)s.stages<<'\t'<<n;s.stages<<'\t'<<h<<'\t'<<h+s.coefficient*r.smooth<<'\t'<<s.model.tau[0]<<'\t'<<s.exclusion.excluded<<std::endl;
    if(s.globalActive && std::string(step)=="after_legalization")s.roundComplete(r,h);
    s.details(step,xx,yy);if(!s.stages)throw std::runtime_error("SLL stage report write failed");
    s.timing("trace_and_log",step,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
}
void SllRunDiagnostics::beginModelAudit(QPSolverWrapper&x,QPSolverWrapper&y,float weight,float ratio){auto&s=*p;s.audit->begin(s.qid+1,s.phase,s.round,s.iteration,x,y,weight,ratio);}
bool SllRunDiagnostics::auditTarget(int id)const{return p->audit->target(id);}
void SllRunDiagnostics::auditTimingAnchor(int id,bool after,const QPSolverWrapper&x,const QPSolverWrapper&y){p->audit->timingAnchor(id,after,x,y);}
void SllRunDiagnostics::auditCheckpoint(const char*name,const QPSolverWrapper&x,const QPSolverWrapper&y){p->audit->checkpoint(name,x,y);}
void SllRunDiagnostics::endPhase(){p->globalActive=false;p->prepared=false;p->phase="outside_global";}
void SllRunDiagnostics::prepare(QPSolverWrapper&x,QPSolverWrapper&y,float weight,float limit){
    AMF_PROFILE_FUNCTION("sll_qp_diagnostics");auto&s=*p;++s.qid;s.mask();s.pseudoWeight=weight;s.limit=limit;
    auto started=std::chrono::steady_clock::now();auto xx=s.coordinates(false),yy=s.coordinates(true);s.x.capture(x,xx);s.y.capture(y,yy);
    s.timing("coordinates_and_base_capture","prepare",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    if(s.shortSpan){
        std::vector<amf_sll::NetGeometry> geometry;auto counted=s.model.countDiscrete(yy,s.threads,&geometry);
        s.exclusion=s.shortPolicy.update(s.model,geometry,s.globalActive);
        s.before=s.model.evaluate(yy,s.movable,s.threads,true,&s.strength,&counted);
    }else{s.exclusion={};s.before=s.model.evaluate(yy,s.movable,s.threads,true,&s.strength);}
    s.evaluationTiming("prepare",s.before);s.timing("short_span_list_update","prepare",s.exclusion.seconds);
    started=std::chrono::steady_clock::now();
    s.shortReport<<s.qid<<'\t'<<s.phase<<'\t'<<s.round<<'\t'<<s.iteration<<'\t'<<s.model.tau[0]<<'\t'<<(s.shortSpan&&s.globalActive)<<'\t'<<s.exclusion.excluded<<'\t'<<s.exclusion.entered<<'\t'<<s.exclusion.resumed<<'\t'<<s.exclusion.excludedS<<'\t'<<s.before.discrete<<'\t'<<s.before.invalidNets<<'\t'<<s.before.discreteSeconds<<'\t'<<s.exclusion.seconds<<std::endl;
    if(!s.shortReport)throw std::runtime_error("Short-span report write failed");
    s.timing("trace_and_log","mask_counts",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    started=std::chrono::steady_clock::now();s.prepared=true;s.rawX.clear();s.rawY.clear();
    std::vector<size_t> ids(s.model.nets.size());std::iota(ids.begin(),ids.end(),0);size_t count=std::min(size_t(8),ids.size());
    std::partial_sort(ids.begin(),ids.begin()+count,ids.end(),[&](size_t a,size_t b){return s.strength[a]!=s.strength[b]?s.strength[a]>s.strength[b]:s.model.nets[a].id<s.model.nets[b].id;});
    s.top.clear();for(size_t i=0;i<count;++i)if(s.strength[ids[i]]>0)s.top.insert(ids[i]);
    s.timing("gradient_net_selection","prepare",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    s.result("before",xx,yy);
    started=std::chrono::steady_clock::now();
    s.audit->freeze(s.model,s.movable,s.y,s.before.gradient,s.coefficient,s.enabled&&s.globalActive,limit,x,y);
    s.timing("frozen_archive","prepare",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    started=std::chrono::steady_clock::now();
    if(s.enabled&&s.globalActive){std::vector<double> linear(y.solverData.objectiveVector.data(),y.solverData.objectiveVector.data()+yy.size());amf_sll::addFirstOrderModel(s.before.gradient,s.movable,s.coefficient,linear);for(size_t i=0;i<linear.size();++i)y.solverData.objectiveVector[i]=linear[i];}
    s.timing("gradient_injection","prepare",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    s.solveStarted=std::chrono::steady_clock::now();
}
void SllRunDiagnostics::candidate(QPSolverWrapper&x,QPSolverWrapper&y){
    auto&s=*p;if(!s.prepared)return;
    s.timing("qp_solve_window","candidate",std::chrono::duration<double>(std::chrono::steady_clock::now()-s.solveStarted).count());
    const auto&xx=x.solverSettings.solutionForward?x.solverData.oriSolution:x.solverData.solution;
    const auto&yy=y.solverSettings.solutionForward?y.solverData.oriSolution:y.solverData.solution;
    s.rawX=s.x.position;s.rawY=s.y.position;
    for(size_t i=0;i<s.model.units;++i)if(s.movable[i]){s.rawX[i]=xx[i];s.rawY[i]=yy[i];if(!std::isfinite(xx[i])||!std::isfinite(yy[i]))throw std::runtime_error("Non-finite QP output before original writeback");}
    s.result("raw",s.rawX,s.rawY);
    auto archiveStarted=std::chrono::steady_clock::now();s.audit->result("raw",s.rawX,s.rawY,y);
    s.timing("frozen_archive","raw",std::chrono::duration<double>(std::chrono::steady_clock::now()-archiveStarted).count());
    for(auto pair:{std::make_pair("X",&x),std::make_pair("Y",&y)}){
        auto&v=*pair.second;auto&d=v.guardDiagnostics;bool guard=v.solverSettings.stabilityGuard;
        const auto&f=pair.second==&x?s.x:s.y;double rhs=0,inc=0;for(size_t i=0;i<f.linear.size();++i){rhs+=f.linear[i]*f.linear[i];if(pair.second==&y&&s.enabled&&s.globalActive)inc+=s.coefficient*s.coefficient*s.before.gradient[i]*s.before.gradient[i];}
        s.solver<<s.qid<<'\t'<<s.phase<<'\t'<<s.round<<'\t'<<s.iteration<<'\t'<<pair.first<<'\t'<<(guard?d.iterations:v.CGSolver.iterations())<<'\t'<<(guard?d.relativeError:v.CGSolver.error())<<'\t'<<(guard?d.converged:v.CGSolver.info()==Eigen::Success)<<'\t'<<d.rollback<<'\t'<<d.repairedRows<<'\t'<<d.maxDiagonalDelta<<'\t'<<v.solverSettings.tolerence<<'\t'<<v.solverSettings.maxIters<<'\t'<<std::sqrt(rhs)<<'\t'<<std::sqrt(inc)<<std::endl;
    }
    s.writebackStarted=std::chrono::steady_clock::now();
}
void SllRunDiagnostics::written(QPSolverWrapper&,QPSolverWrapper&y){auto&s=*p;if(s.prepared){
    s.timing("original_writeback_window","written",std::chrono::duration<double>(std::chrono::steady_clock::now()-s.writebackStarted).count());
    auto xx=s.coordinates(false),yy=s.coordinates(true);s.result("after_writeback",xx,yy);
    auto started=std::chrono::steady_clock::now();s.audit->result("written",xx,yy,y);
    s.timing("frozen_archive","written",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
}}
