#include "SllPlanner.h"
#include "PlacementInfo.h"
#include "../../../utils/RuntimeProfiler.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
using namespace amf_sll_plan;
namespace {
std::vector<std::string> split(const std::string&s,char delim='\t') {std::vector<std::string> v;std::istringstream in(s);std::string t;while(std::getline(in,t,delim))v.push_back(t);return v;}
std::string quote(const std::string&s){std::string v="\"";for(char c:s){if(c=='\\'||c=='"'||c=='$'||c=='['||c==']')v+='\\';if(c=='\n')v+="\\n";else if(c=='\r')v+="\\r";else v+=c;}return v+'"';}
double number(const std::map<std::string,std::string>&cfg,const std::string&key,double fallback){auto i=cfg.find(key);if(i==cfg.end())return fallback;size_t used=0;double x=std::stod(i->second,&used);if(used!=i->second.size()||!std::isfinite(x)||x<0)throw std::invalid_argument("Invalid "+key);return x;}
}
struct SllPlanner::Impl {
    PlacementInfo*pi;Model m;std::map<int,size_t> netIndex;
    std::map<int,std::pair<size_t,int>> sinkIndex;
    std::map<std::pair<int,int>,std::vector<int>> cellPairs;
    std::vector<PhysicalBoundaryModel::Region> regions;
    std::map<std::pair<int,int>,std::vector<int>> timingCandidates;
    std::ofstream log;size_t generation=0;
    explicit Impl(PlacementInfo*placement,const std::map<std::string,std::string>&cfg):pi(placement) {
        if(pi->getIncrementalContext().enabled)throw std::invalid_argument("E2 SLL planning currently requires full-design placement");
        auto physical=pi->getDeviceInfo()->getPhysicalBoundaryModel();if(!physical)throw std::invalid_argument("SLL planner requires physical boundary metadata");
        regions=physical->getRegions();
        if(cfg.count("ProPrimitiveTiming") && cfg.at("ProPrimitiveTiming")=="true")throw std::invalid_argument("E2 baseline requires ProPrimitiveTiming=false");
        m.ratio=number(cfg,"y2xRatio",.4);m.timingWeight=number(cfg,"SllPlanningTimingWeight",16);
        m.extraCrossingCost=number(cfg,"SllPlanningExtraCrossingCost",24);m.switchCost=number(cfg,"SllPlanningSwitchCost",.1);
        if(cfg.count("SllPlanningAllowSplit")){const auto&s=cfg.at("SllPlanningAllowSplit");if(s!="true"&&s!="false")throw std::invalid_argument("SllPlanningAllowSplit must be boolean");m.allowSplit=s=="true";}
        std::ifstream in(cfg.at("SllChannelFile"));if(!in)throw std::invalid_argument("Cannot read SllChannelFile");
        std::string row;bool part=false,coordinate=false,header=false;
        while(std::getline(in,row)) {
            if(row.rfind("#part\t",0)==0){part=true;if(row.substr(6)!=cfg.at("physical device part"))throw std::invalid_argument("SLL channel part mismatch");continue;}
            if(row.rfind("#coordinate_sha256\t",0)==0){coordinate=true;if(row.substr(19)!=cfg.at("SllCoordinateMapSha256"))throw std::invalid_argument("SLL coordinate map mismatch");continue;}
            if(row.empty()||row[0]=='#')continue;
            if(!header){if(row!="resource\tfrom\tto\tcolumn\tin_x\tin_y\tout_x\tout_y\tslow_max\tslow_min\tfast_max\tfast_min\tsegment")throw std::invalid_argument("Bad SLL table schema");header=true;continue;}
            auto f=split(row);if(f.size()!=13)throw std::invalid_argument("Malformed SLL channel row");
            Channel c;c.resource=f[0];c.from=std::stoi(f[1]);c.to=std::stoi(f[2]);c.column=std::stoi(f[3]);c.in={std::stod(f[4]),std::stod(f[5])};c.out={std::stod(f[6]),std::stod(f[7])};c.slowMax=std::stod(f[8]);c.slowMin=std::stod(f[9]);c.fastMax=std::stod(f[10]);c.fastMin=std::stod(f[11]);c.segment=f[12];
            if(slr(c.in)!=c.from||slr(c.out)!=c.to)throw std::invalid_argument("SLL endpoint coordinate/SLR mismatch: "+c.resource);
            m.channels.push_back(std::move(c));
        }
        if(!part||!coordinate)throw std::invalid_argument("SLL table lacks part/coordinate identity");m.index();
        if(cfg.count("SllReservedResourceFile")) {
            std::ifstream r(cfg.at("SllReservedResourceFile"));if(!r)throw std::invalid_argument("Cannot read reserved SLL resources");
            std::set<std::string> all;for(auto&c:m.channels)all.insert(c.resource);
            while(std::getline(r,row))if(!row.empty()&&row[0]!='#'){if(!all.count(row))throw std::invalid_argument("Unknown reserved SLL resource");m.reserved.insert(row);}
        }
        for(const auto&bucket:m.available) {
            std::map<std::tuple<double,double,double,double>,int> representatives;
            for(int ci:bucket.second) {if(m.reserved.count(m.channels[ci].resource))continue;const auto&c=m.channels[ci];auto key=std::make_tuple(c.in.x,c.in.y,c.out.x,c.out.y);auto it=representatives.find(key);if(it==representatives.end() || m.channels[it->second].slowMax<c.slowMax)representatives[key]=ci;}
            for(const auto&v:representatives)timingCandidates[bucket.first].push_back(v.second);
        }
        if(cfg.count("SllPlanningReportDirectory")) {
            std::filesystem::create_directories(cfg.at("SllPlanningReportDirectory"));
            const auto path=cfg.at("SllPlanningReportDirectory")+"/sll-planning.tsv";
            if(std::filesystem::exists(path))throw std::runtime_error("Refusing to overwrite SLL planner diagnostics");
            log.open(path);log<<"generation\tnets\tvirtual_cells\tcreated\tretained\tdeleted\tlocal_hpwl\n";
        }
    }
    int slr(Point p)const {for(auto&r:regions)if(r.contains(p.x,p.y))return r.slr;throw std::runtime_error("SLL endpoint outside physical model");}
    Point position(DesignInfo::DesignPin*pin)const {
        auto cell=pin->getCell();auto it=pi->getCellId2PlacementUnit().find(cell->getCellId());
        if(it==pi->getCellId2PlacementUnit().end())throw std::runtime_error("SLL pin without placement unit");
        auto pu=it->second;Point pos{pu->X()+pin->getOffsetXInCell(),pu->Y()+pin->getOffsetYInCell()};
        if(pu->getType()==PlacementInfo::PlacementUnitType_Macro){auto mac=static_cast<PlacementInfo::PlacementMacro*>(pu);pos.x+=mac->getCellOffsetXInMacro(cell);pos.y+=mac->getCellOffsetYInMacro(cell);}
        return pos;
    }
    Pin makePin(DesignInfo::DesignPin*pin)const {
        auto pu=pi->getCellId2PlacementUnit().at(pin->getCell()->getCellId());Pin p;
        p.id=pin->getElementIdInType();p.unit=pu->getId();p.name=pin->getName();p.p=position(pin);p.slr=slr(p.p);p.offset={p.p.x-pu->X(),p.p.y-pu->Y()};p.movable=!pu->isFixed();return p;
    }
    void addPair(const Pin*a,Point av,const Pin*b,Point bv,double weight,std::vector<Eigen::Triplet<float>>&trip,std::vector<float>&diag,Eigen::VectorXd&rhs,bool x)const {
        double pa=x?av.x:av.y,pb=x?bv.x:bv.y;
        int ia=a?a->unit:-1,ib=b?b->unit:-1;
        // Two fixed virtual endpoints have no QP variables, even though both
        // use sentinel -1. Their connection still enters tree/candidate cost.
        if(!a&&!b)return;
        pi->addB2BNetInPlacementInfo(trip,diag,rhs,ia,ib,a?(x?av.x-a->offset.x:av.y-a->offset.y):pa,b?(x?bv.x-b->offset.x:bv.y-b->offset.y):pb,
            a?(x?a->offset.x:a->offset.y):0,b?(x?b->offset.x:b->offset.y):0,a&&a->movable,b&&b->movable,weight);
    }
};
SllPlanner::SllPlanner(PlacementInfo*pi,const std::map<std::string,std::string>&cfg):p(new Impl(pi,cfg)){}
SllPlanner::~SllPlanner()=default;
const Model&SllPlanner::model()const{return p->m;}
void SllPlanner::setLocalDelay(LocalDelay f){p->m.local=std::move(f);}
bool SllPlanner::handles(int id)const{return p->netIndex.count(id);}
bool SllPlanner::crosses(Point a,Point b)const{return p->slr(a)!=p->slr(b);}
void SllPlanner::refresh() {
    AMF_PROFILE_SCOPE("sll_position_planning","virtual SLL lifecycle and capacity allocation");
    std::vector<Net> nets;auto&di=*p->pi->getDesignInfo();
    auto graph=p->pi->getTimingInfo()->getSimplePlacementTimingGraph();
    for(auto pn:p->pi->getPlacementNets()) {
        auto dn=pn->getDesignNet();if(dn->checkIsPowerNet()||dn->checkIsGlobalClock()||dn->getDriverPins().size()!=1)continue;
        auto driver=dn->getDriverPins()[0];if(!driver->getCell())continue;
        Net n;n.id=dn->getElementIdInType();n.name=dn->getName();n.source=p->makePin(driver);n.weight=dn->getOverallEnhanceRatio();
        bool crossing=false;
        for(auto pin:dn->getPinsBeDriven()) {
            if(!pin->getCell())continue;auto s=p->makePin(pin);
            if(graph) {
                const auto&timing=graph->getNodes();
                auto a=timing.at(driver->getCell()->getCellId()),b=timing.at(pin->getCell()->getCellId());
                const double budget=b->getRequiredArrivalTime()-a->getLatestOutputArrival();
                if(std::isfinite(budget)&&budget<1e5){double d=geometryDelay(n.source.p,s.p);s.criticality=std::min(64.0,1.0+std::max(0.0,d-budget));}
            }
            crossing|=s.slr!=n.source.slr;n.sinks.push_back(std::move(s));
        }
        if(crossing)nets.push_back(std::move(n));
    }
    p->m.rebuild(std::move(nets));p->netIndex.clear();p->sinkIndex.clear();p->cellPairs.clear();
    size_t cells=0;double wire=0;
    for(size_t i=0;i<p->m.plans.size();++i) {
        const auto&plan=p->m.plans[i];p->netIndex[plan.net.id]=i;cells+=plan.crossings.size();wire+=p->m.wireCost(plan);
        const int sourceCell=di.getPins().at(plan.net.source.id)->getCell()->getCellId();
        for(auto&s:plan.net.sinks){p->sinkIndex[s.id]={i,plan.net.source.id};int dest=di.getPins().at(s.id)->getCell()->getCellId();p->cellPairs[{sourceCell,dest}].push_back(s.id);}
    }
    if(p->log)p->log<<++p->generation<<'\t'<<p->m.plans.size()<<'\t'<<cells<<'\t'<<p->m.created<<'\t'<<p->m.retained<<'\t'<<p->m.deleted<<'\t'<<std::setprecision(17)<<wire<<std::endl;
}
void SllPlanner::addWireModel(int id,std::vector<Eigen::Triplet<float>>&trip,std::vector<float>&diag,Eigen::VectorXd&rhs,float general,float ratio,bool x)const {
    const auto&plan=p->m.plans.at(p->netIndex.at(id));
    for(const auto&tree:p->m.localTrees(plan)) {
        if(tree.size()<2)continue;
        std::vector<Point> pts;for(auto&t:tree) {if(t.isFixed)pts.push_back(t.fixed);else {const auto&pin=p->m.pin(plan.net,t.pin);auto pu=p->pi->getPlacementUnits().at(pin.unit);pts.push_back({pu->X()+pin.offset.x,pu->Y()+pin.offset.y});}}
        auto value=[&](size_t i){return x?pts[i].x:pts[i].y;};size_t lo=0,hi=0;
        for(size_t i=1;i<pts.size();++i){if(value(i)<value(lo))lo=i;if(value(i)>value(hi))hi=i;}
        if(lo==hi)hi=(lo+1)%pts.size();
        double w=2.0*general*plan.net.weight/(pts.size()-1)*(x?1:ratio);
        auto edge=[&](size_t a,size_t b){const auto&ta=tree[a];const auto&tb=tree[b];p->addPair(ta.isFixed?nullptr:&p->m.pin(plan.net,ta.pin),pts[a],tb.isFixed?nullptr:&p->m.pin(plan.net,tb.pin),pts[b],w/std::max(.5,std::abs(value(a)-value(b))),trip,diag,rhs,x);};
        edge(lo,hi);for(size_t i=0;i<pts.size();++i)if(i!=lo&&i!=hi){edge(i,lo);edge(i,hi);}
    }
}
bool SllPlanner::pinDelay(int source,int sink,Point a,Point b,double&result)const {
    auto it=p->sinkIndex.find(sink);if(it==p->sinkIndex.end()||it->second.second!=source)return false;
    const auto&plan=p->m.plans.at(it->second.first);const auto&s=p->m.pin(plan.net,sink);
    if(p->slr(a)!=plan.net.source.slr||p->slr(b)!=s.slr)return false;
    result=p->m.pathDelay(plan,sink,a,b);return true;
}
bool SllPlanner::cellDelay(int source,int sink,Point a,Point b,double&result)const {
    auto it=p->cellPairs.find({source,sink});if(it==p->cellPairs.end())return false;
    // Ambiguous cell pairs can have different pin-level trees. Caller must use
    // exact pin IDs; never select an arbitrary net between the two cells.
    if(it->second.size()!=1)return false;int id=it->second[0];return pinDelay(p->sinkIndex.at(id).second,id,a,b,result);
}
double SllPlanner::geometryDelay(Point a,Point b)const {
    int from=p->slr(a),to=p->slr(b);if(from==to)return p->m.delay(a,b);
    const int dir=to>from?1:-1;double d=0;Point cur=a;
    for(int s=from;s!=to;s+=dir){auto it=p->m.available.find({s,s+dir});if(it==p->m.available.end())throw std::runtime_error("Missing SLL timing direction");double best=INFINITY;int chosen=-1;
        std::vector<std::pair<double,int>> near;
        for(int ci:p->timingCandidates.at({s,s+dir})){const auto&c=p->m.channels[ci];if(!p->m.reserved.count(c.resource))near.push_back({length(cur,c.in,p->m.ratio)+std::abs(c.out.x-b.x),ci});}
        size_t count=std::min(size_t(16),near.size());std::partial_sort(near.begin(),near.begin()+count,near.end());
        for(size_t j=0;j<count;++j){int ci=near[j].second;const auto&c=p->m.channels[ci];double v=p->m.delay(cur,c.in)+c.slowMax+std::abs(c.out.x-b.x)*.01;if(v<best){best=v;chosen=ci;}}
        if(chosen<0)throw std::runtime_error("No SLL timing candidate");const auto&c=p->m.channels[chosen];d+=p->m.delay(cur,c.in)+c.slowMax;cur=c.out;
    }
    return d+p->m.delay(cur,b);
}
void SllPlanner::addTimingModel(std::vector<Eigen::Triplet<float>>&trip,std::vector<float>&diag,Eigen::VectorXd&rhs,float w,bool x)const {
    for(const auto&plan:p->m.plans) {
        std::map<int,double> shared;
        for(const auto&s:plan.net.sinks) {
            double strength=w*(s.criticality-1);if(strength<=0)continue;
            const auto&path=plan.paths.at(s.id);
            if(path.empty()){p->addPair(&plan.net.source,plan.net.source.p,&s,s.p,strength/std::max(.5,length(plan.net.source.p,s.p,p->m.ratio)),trip,diag,rhs,x);continue;}
            shared[path.front()]=std::max(shared[path.front()],strength);
            Point root=p->m.channels[plan.crossings[path.back()].channel].out;
            p->addPair(nullptr,root,&s,s.p,strength/std::max(.5,length(root,s.p,p->m.ratio)),trip,diag,rhs,x);
        }
        for(auto&v:shared){Point end=p->m.channels[plan.crossings[v.first].channel].in;p->addPair(&plan.net.source,plan.net.source.p,nullptr,end,v.second/std::max(.5,length(plan.net.source.p,end,p->m.ratio)),trip,diag,rhs,x);}
    }
}
void SllPlanner::exportPlan(const std::string&path) {
    refresh();std::ofstream out(path);if(!out)throw std::runtime_error("Cannot export SLL plan");
    out<<"# E2 physical-only SLL trees; apply after final placement.\nset e2_sll_plan [list";
    for(const auto&plan:p->m.plans) {
        std::function<std::string(int)> children=[&](int parent){std::string text="[list";
            for(const auto&s:plan.net.sinks){const auto&route=plan.paths.at(s.id);if((route.empty()?-1:route.back())==parent)text+=" [dict create pin "+quote(s.name)+" slr "+std::to_string(s.slr)+"]";}
            for(size_t k=0;k<plan.crossings.size();++k)if(plan.crossings[k].parent==parent){const auto&c=p->m.channels[plan.crossings[k].channel];text+=" [dict create resource "+quote(c.resource)+" from "+std::to_string(c.from)+" to "+std::to_string(c.to)+" segment [list";for(auto&node:split(c.segment,','))text+=" "+quote(node);text+="] children "+children(k)+"]";}
            return text+"]";};
        out<<" \\\n [dict create net "<<quote(plan.net.name)<<" driver "<<quote(plan.net.source.name)<<" source_slr "<<plan.net.source.slr<<" tree "<<children(-1)<<"]";
    }
    out<<"]\n";if(!out)throw std::runtime_error("SLL export failed");
}

double SllPlanner::wireLength(int id,double ratio,int trialUnit,Point trial)const {
    const auto&plan=p->m.plans.at(p->netIndex.at(id));double total=0;
    for(const auto&tree:p->m.localTrees(plan)) {
        if(tree.size()<2)continue;double x0=INFINITY,x1=-INFINITY,y0=INFINITY,y1=-INFINITY;
        for(const auto&t:tree) {
            Point v=t.fixed;
            if(!t.isFixed) {
                auto pin=p->pi->getDesignInfo()->getPins().at(t.pin);v=p->position(pin);
                auto pu=p->pi->getCellId2PlacementUnit().at(pin->getCell()->getCellId());
                if(pu->getId()==trialUnit){v.x+=trial.x-pu->X();v.y+=trial.y-pu->Y();}
            }
            x0=std::min(x0,v.x);x1=std::max(x1,v.x);y0=std::min(y0,v.y);y1=std::max(y1,v.y);
        }
        total+=x1-x0+ratio*(y1-y0);
    }
    return total;
}
