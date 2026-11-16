#include "SllQuantityModel.h"
#include "FrozenQpArchive.h"
#include "PlacementFeatureConfig.h"
#include "LegacySllQuantityModel.h"
#include <iostream>
#include <random>
#include <numeric>
#include <iomanip>
#include <filesystem>
using namespace amf_sll;
static size_t checks=0;
static void check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
static bool close(double a,double b){return std::abs(a-b)<=1e-12*(1+std::abs(a)+std::abs(b));}
static Model base(size_t n){Model m;m.sllSTE=true;m.units=n;m.boundaries={239.5,479.5,719.5};m.tau={10,10,10};m.gamma=.02;m.minY=-.5;m.maxY=959.5;return m;}
static std::vector<unsigned char> movable(size_t n){return std::vector<unsigned char>(n,1);}
// Independent binary-side oracle: fixed pins participate in counts, not gradients.
static std::vector<double> oracle(const Model&m,const std::vector<double>&y,const std::vector<unsigned char>&mask){
    std::vector<double> g(m.units,0);
    for(const auto&n:m.nets){if(!n.optimize || !n.gradientActive)continue;
        for(size_t b=0;b<m.boundaries.size();++b){std::vector<size_t> low,high;
            for(size_t j=n.begin;j<n.end;++j)(y[m.pins[j].unit]+m.pins[j].offsetY>m.boundaries[b]?high:low).push_back(j);
            if(low.empty()||high.empty())continue;
            for(const auto&side:{low,high})for(size_t j:side){auto p=m.pins[j];if(!mask[p.unit])continue;
                double u=(y[p.unit]+p.offsetY-m.boundaries[b])/m.tau[b];
                double e=std::exp(-std::abs(u)),derivative=e/((1+e)*(1+e)*m.tau[b]);
                g[p.unit]+=n.weight*(u>0?1:-1)*derivative/side.size();
            }
        }
    }return g;
}
static void verify(const Model&m,const std::vector<double>&y,const std::vector<unsigned char>&mask){
    m.validate();auto a=m.evaluate(y,mask,1,true);auto expected=oracle(m,y,mask);
    for(size_t i=0;i<m.units;++i)check(close(a.gradient[i],expected[i]),"independent STE gradient oracle");
    for(int jobs:{2,4,12}){auto b=m.evaluate(y,mask,jobs,true);check(a.gradient==b.gradient && a.smooth==b.smooth && a.seams==b.seams,"deterministic thread reduction");}
    check(m.evaluate(y,mask,2,false).smooth==a.smooth,"cost-only exact objective");
    auto counted=m.countDiscrete(y,2);auto reused=m.evaluate(y,mask,2,true,nullptr,&counted);
    check(reused.gradient==a.gradient && reused.smooth==a.smooth && reused.discrete==a.discrete,"same-position count reuse");
    std::vector<double> detailSum(m.units,0);
    for(auto&n:m.nets){auto d=detail(m,n,y);if(n.optimize&&n.gradientActive)for(size_t j=0;j<d.gradient.size();++j)if(mask[m.pins[n.begin+j].unit])detailSum[m.pins[n.begin+j].unit]+=d.gradient[j];}
    for(size_t i=0;i<m.units;++i)check(close(a.gradient[i],detailSum[i]),"reported pin gradient matches evaluator");
}
int main(int argc,char**argv){try{
    if(argc!=2)throw std::runtime_error("usage: check-sll-ste NEW_OUTPUT");
    std::filesystem::path out=argv[1];if(!std::filesystem::create_directory(out))throw std::runtime_error("refuse overwrite");
    check(!Model{}.sllSTE,"switch defaults off");
    amf_config::validatePlacementFeatures({{"sllSTE","true"}});amf_config::validatePlacementFeatures({{"sllSTE","false"}});
    bool rejected=false;try{amf_config::validatePlacementFeatures({{"sllSTE","1"}});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid switch rejected");
    for(unsigned bits=1;bits<16;++bits){auto m=base(4);int lo=4,hi=-1;
        for(unsigned i=0;i<4;++i)if(bits&(1u<<i)){m.pins.push_back({i,0});lo=std::min(lo,int(i));hi=std::max(hi,int(i));}
        m.nets={{0,m.pins.size(),bits}};std::vector<double> y={100,350,600,850};verify(m,y,movable(4));
        auto a=m.evaluate(y,movable(4),1,true);check(a.smooth==hi-lo && a.discrete==hi-lo,"all 15 die occupancy masks");
    }
    for(double seam:{239.5,479.5,719.5}){
        auto m=base(3);m.pins={{0,0},{1,0},{2,0}};m.nets={{0,3,1}};
        double below=std::nextafter(seam,-INFINITY),above=std::nextafter(seam,INFINITY);
        for(auto y:{std::vector<double>{below,seam,seam},std::vector<double>{above,above,above}}){
            auto a=m.evaluate(y,movable(3),1,true);check(a.smooth==0 && a.gradient==std::vector<double>(3,0),"same side at exact or adjacent floating seam has zero pull");verify(m,y,movable(3));}
        auto y=std::vector<double>{below,seam,above};verify(m,y,movable(3));auto a=m.evaluate(y,movable(3),1,true);
        check(a.smooth==1 && a.gradient[0]<0 && a.gradient[1]<0 && a.gradient[2]>0,"exact seam lower, crossed seam signs");
    }
    // Representative pre-QP same-die net; unrelated boundaries cannot pull it.
    auto representative=base(3);representative.pins={{0,0},{1,0},{2,0}};representative.nets={{0,3,484041}};
    std::vector<double> ry={471.3274536133,470.9161987305,479.4347839355};
    auto ste=representative.evaluate(ry,movable(3),1,true);representative.sllSTE=false;auto legacy=representative.evaluate(ry,movable(3),1,true);
    check(legacy.smooth>0 && legacy.gradient[0]<0 && ste.smooth==0 && ste.gradient==std::vector<double>(3,0),"observed false same-die quantity force removed");representative.sllSTE=true;
    // One driver, a local sink and remote sinks: one shared net per seam, not pairwise.
    auto mixed=base(4);mixed.pins={{0,0},{1,0},{2,0},{3,0}};mixed.nets={{0,4,8,true,1.7}};
    std::vector<double> my={471,479.4,480,481};verify(mixed,my,movable(4));auto ma=mixed.evaluate(my,movable(4),1,true);
    check(ma.smooth==1.7 && ma.discrete==1,"mixed fanout counted once");
    auto fixed=movable(4);fixed[2]=0;verify(mixed,my,fixed);check(mixed.evaluate(my,fixed,1,true).gradient[2]==0,"fixed endpoint no writeback gradient");
    // At a truly crossed lower seam, approaching the unused upper seam adds no force.
    auto one=base(2);one.pins={{0,0},{1,0}};one.nets={{0,2,9}};
    auto oa=one.evaluate({239,479.4},movable(2),1,true);
    check(oa.gradient[1]>=0 && oa.gradient[1]<1e-10,"no spurious upward pull at unused upper seam");
    // Macro offsets and multiple pins of one PU accumulate; state changes are not cached.
    auto macro=base(3);macro.pins={{0,-1},{0,1},{1,0},{2,240}};macro.nets={{0,4,10,true,.6}};
    verify(macro,{479.5,480,480},movable(3));verify(macro,{460,461,200},movable(3));
    auto inactive=macro;inactive.nets[0].gradientActive=false;auto ia=inactive.evaluate({479.5,480,480},movable(3),1,true);
    check(ia.smooth>0 && ia.activeSmooth==0 && ia.gradient==std::vector<double>(3,0),"short-span inactive mask retained");
    inactive.nets[0].optimize=false;auto ex=inactive.evaluate({479.5,480,480},movable(3),1,true);check(ex.discrete>0 && ex.smooth==0,"optimization exclusion retained in hard statistics");
    auto bad=one.evaluate({239,1000},movable(2),1,true);check(bad.invalidNets==1 && bad.smooth>0,"invalid geometry not silently erased from objective");
    auto zero=base(1);zero.validate();check(zero.evaluate({100},{1},1,true).smooth==0,"empty nets");
    std::mt19937 gen(42);std::uniform_real_distribution<double> pos(1,950);
    auto many=base(96);
    for(unsigned n=0;n<500;++n){size_t begin=many.pins.size();for(unsigned j=0;j<2+n%13;++j)many.pins.push_back({(n*11+j*7)%96,double(int(j%3)-1)});many.nets.push_back({begin,many.pins.size(),n,true,1.+.1*(n%3),n%11!=0});}
    std::vector<double> yy(96);for(auto&y:yy)y=pos(gen);verify(many,yy,movable(96));
    // Compare disabled branch bit-for-bit against the parent implementation, not a rewritten oracle.
    legacy_sll::Model parent;parent.units=many.units;parent.boundaries=many.boundaries;parent.tau=many.tau;parent.gamma=many.gamma;parent.minY=many.minY;parent.maxY=many.maxY;
    for(auto p:many.pins)parent.pins.push_back({p.unit,p.offsetY});for(auto n:many.nets)parent.nets.push_back({n.begin,n.end,n.id,n.optimize,n.weight,n.gradientActive});
    many.sllSTE=false;for(int trial=0;trial<16;++trial){for(auto&y:yy)y=pos(gen);auto a=many.evaluate(yy,movable(96),4,true);auto b=parent.evaluate(yy,movable(96),4,true);
        check(a.gradient==b.gradient && a.smooth==b.smooth && a.activeSmooth==b.activeSmooth && a.discrete==b.discrete && a.seams==b.seams,"sllSTE=false exact parent equivalence");}
    // Diagnostic archive must retain the selected model and activity mask; old bytes stay supported.
    for(bool enabled:{false,true})for(bool masked:{false,true}){auto m=many;m.sllSTE=enabled;for(auto&n:m.nets)n.gradientActive=!masked || n.id%2;
        auto path=out/(std::string(enabled?"ste":"legacy")+(masked?"-masked.bin":".bin"));amf_sll_audit::Budget budget{0,10000000};amf_sll_audit::writeModel(path,m,budget);
        auto back=amf_sll_audit::readModel(path);check(back.sllSTE==enabled && back.evaluate(yy,movable(96),2,true).gradient==m.evaluate(yy,movable(96),2,true).gradient,"model archive roundtrip");}
    // Native QP consumes only b changes; solve a strongly anchored local fixture.
    for(bool enabled:{false,true}){representative.sllSTE=enabled;auto g=representative.evaluate(ry,movable(3),1,true).gradient;
        QPSolverWrapper q(true,false,-.5,959.5,3,false);q.solverData.objectiveMatrixDiag.assign(3,2);q.solverSettings.stabilityGuard=true;q.solverSettings.tolerence=1e-10;
        std::vector<double> b(3);for(size_t i=0;i<3;++i){b[i]=-2*ry[i];q.solverData.oriSolution[i]=ry[i];}
        addFirstOrderModel(g,movable(3),firstOrderCoefficient(.4),b);for(size_t i=0;i<3;++i)q.solverData.objectiveVector[i]=b[i];
        auto qp=&q;QPSolverWrapper::QPSolve(qp);check(q.guardDiagnostics.converged&&!q.guardDiagnostics.rollback,"actual QP converges");
        check(q.solverData.objectiveMatrixDiag==std::vector<float>(3,2) && q.solverData.objectiveMatrixTripletList.empty(),"SLL has no Hessian or constraint changes");
        for(size_t i=0;i<3;++i)check(std::abs(q.solverData.solution[i]+b[i]/2)<1e-6,"QP first-order injection sign");}
    auto fan=base(2);for(unsigned i=0;i<100001;++i)fan.pins.push_back({i?1u:0u,0});fan.nets={{0,fan.pins.size(),99}};
    auto fr=fan.evaluate({479,480},movable(2),1,true);check(fr.smooth==1 && close(fr.gradient[0],-fr.gradient[1]),"100000 local duplicates share one boundary demand, no pin-pair expansion");
    // Bounded ABBA, identical netlists/threads per batch. No claim about full-design runtime.
    std::ofstream perf(out/"benchmark.tsv");perf<<"workload\tbatch\tmode\tqueries\tseconds\n";volatile double sink=0;
    for(int workload=0;workload<3;++workload){auto m=workload==2?fan:many;std::vector<double> y=workload==2?std::vector<double>{479,480}:yy;
        if(workload==0)std::fill(y.begin(),y.end(),471.0);auto mask=movable(m.units);int repeats=workload==2?12:120;
        for(int warm=0;warm<4;++warm){m.sllSTE=warm%2;sink+=m.evaluate(y,mask,1,true).smooth;}
        for(int batch=0;batch<12;++batch)for(bool on:{false,true,true,false}){m.sllSTE=on;auto t=std::chrono::steady_clock::now();
            for(int i=0;i<repeats;++i){auto r=m.evaluate(y,mask,1,true);sink+=r.smooth+r.gradient[0];}
            double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();perf<<workload<<'\t'<<batch<<'\t'<<on<<'\t'<<repeats<<'\t'<<std::setprecision(17)<<elapsed<<'\n';}}
    std::cout<<"PASS "<<checks<<" SLL STE model/QP/archive checks; checksum="<<sink<<'\n';
    std::ofstream(out/"result.json")<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"native_exit_code\":0}\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
