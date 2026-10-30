#include "../lib/HiFPlacer/placement/placementInfo/SllWireCache.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static void close(double a,double b,const char*why){check(std::abs(a-b)<1e-9,why);}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};return p;}
static Channel channel(int from,int to,Point in,Point out,int col=20){
    Channel c;c.from=from;c.to=to;c.in=in;c.out=out;c.column=col;
    c.resource=std::to_string(from)+"_"+std::to_string(to)+"_"+std::to_string(in.y);
    c.segment=c.resource+"_in,"+c.resource+"_out";
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static double hpwl(const std::vector<Point>&p,double ratio){
    if(p.size()<2)return 0;TileBox b;for(auto x:p)b.add(x);return b.x1-b.x0+ratio*(b.y1-b.y0);
}
// Independent semantic oracle: count receiving branches and incoming wires,
// without consulting localTrees or assuming any particular term ordering.
static double reference(const Model&m,const Plan&p){
    std::vector<Point> local{p.net.source.p};double result=0;
    for(const auto&s:p.net.sinks)if(p.paths.at(s.id).empty())local.push_back(s.p);
    result+=hpwl(local,m.ratio);
    for(size_t k=0;k<p.crossings.size();++k){const auto&e=p.crossings[k];const auto&c=m.geometry(e);
        const Point root=e.parent<0?p.net.source.p:m.geometry(p.crossings.at(e.parent)).out;
        result+=length(root,c.in,m.ratio);
        std::vector<Point> landing{c.out};
        for(const auto&s:p.net.sinks){const auto&path=p.paths.at(s.id);if(!path.empty()&&path.back()==int(k))landing.push_back(s.p);}
        result+=hpwl(landing,m.ratio);
    }
    return p.net.weight*result;
}
static void checkCache(const Model&m,const Plan&p){
    WireSnapshotNet cache;
    for(const auto&tree:m.localTrees(p)){std::vector<WireSnapshotPoint> points;
        for(const auto&t:tree){if(t.isFixed)points.push_back({t.fixed,{},-1});
            else {const auto&v=m.pin(p.net,t.pin);points.push_back({v.p,v.p,v.unit});}}
        cache.trees.emplace_back(points);
    }
    for(int unit=-1;unit<12;++unit)for(double d:{-23.,0.,19.}){
        Plan moved=p;Point trial{47+d,60-d};
        if(moved.net.source.unit==unit&&unit>=0)moved.net.source.p=trial;
        for(auto&s:moved.net.sinks)if(s.unit==unit&&unit>=0)s.p=trial;
        close(cache.length(m.ratio,unit,trial)*p.net.weight,reference(m,moved),"split wire cache matches semantic oracle for trial PU");
    }
}
int main(){try{
    Model m;m.ratio=.4;
    m.channels={channel(0,1,{20,90},{20,110}),channel(1,2,{60,190},{60,210}),channel(0,-1,{45,10},{45,-10})};
    Plan p;p.net.id=1;p.net.source=pin(0,0,80,50);p.net.weight=2.5;
    p.net.sinks={pin(1,0,0,0),pin(2,0,100,100),pin(3,1,0,100),pin(4,1,100,200)};
    p.crossings={{"a",0,-1,{3,4}}};p.paths={{1,{}},{2,{}},{3,{0}},{4,{0}}};
    close(m.wireCost(p),reference(m,p),"one crossing explicit formula");
    const double before=m.wireCost(p);m.channels[0].in.x=m.channels[0].out.x=70;
    const double after=m.wireCost(p);close(before-after,2.5*50,"moving crossing toward source cannot hide inside local bbox");
    auto toward=p;toward.net.source.p.x=75;
    close(m.wireCost(p)-m.wireCost(toward),2.5*5,"moving source toward entry lowers input length despite local bbox");
    checkCache(m,p);
    for(int i=5;i<105;++i){p.net.sinks.push_back(pin(i,1,0,100));p.paths[i]={0};p.crossings[0].sinks.push_back(i);}
    close(m.wireCost(p),after,"duplicate remote receivers do not multiply the crossing leg");
    p.net.sinks.resize(4);p.paths={{1,{}},{2,{}},{3,{0}},{4,{0}}};p.crossings[0].sinks={3,4,5};
    p.net.sinks.push_back(pin(5,2,90,250));p.paths[5]={0,1};p.crossings.push_back({"b",1,0,{5}});
    p.net.sinks.push_back(pin(6,-1,10,-50));p.paths[6]={2};p.crossings.push_back({"c",2,-1,{6}});
    close(m.wireCost(p),reference(m,p),"two directions and multiple boundaries counted once");checkCache(m,p);
    auto transit=p;transit.net.sinks.erase(transit.net.sinks.begin()+2,transit.net.sinks.begin()+4);
    close(m.wireCost(transit),reference(m,transit),"empty landing receiver set still retains transit leg");checkCache(m,transit);
    auto noLocal=p;noLocal.net.sinks.erase(noLocal.net.sinks.begin(),noLocal.net.sinks.begin()+2);
    close(m.wireCost(noLocal),reference(m,noLocal),"no local receivers adds no phantom local term");
    Plan local;local.net.source=pin(0,0,10,20);local.net.sinks={pin(1,0,30,50)};local.paths[1]={};
    close(m.wireCost(local),32,"same-die net remains one ordinary HPWL");
    // Deliberate split: each crossing has its own root leg, no fanout multiple.
    auto split=noLocal;split.crossings.push_back({"split",0,-1,{7}});split.net.sinks.push_back(pin(7,1,50,150));split.paths[7]={3};
    close(m.wireCost(split),reference(m,split),"optional split has one leg per crossing");
    // Actual initializer: a wide local Y bbox used to mask the source leg.
    Model seed;seed.jointPlacement=seed.sourceNearInitialization=true;seed.initialDensityWeight=0;
    seed.channels={channel(0,1,{20,0},{20,20}),channel(0,1,{20,10},{20,30})};seed.index();
    Net n;n.id=2;n.source=pin(0,0,20,10);n.sinks={pin(1,0,0,-50),pin(2,0,100,50),pin(3,1,0,20),pin(4,1,100,40)};
    seed.local=[](Point,Point)->double{throw std::runtime_error("initializer queried timing");};
    seed.rebuild({n});check(seed.geometry(seed.plans[0].crossings[0]).in.y==10,"newborn/refined Y uses explicit source leg");
    // Direct refinement must also price each outgoing transit pair, rather
    // than enclosing child entries and receivers in a shared bounding box.
    TileBox receivers;receivers.add({0,20});receivers.add({100,40});
    std::map<TileKey,size_t> occupied;std::vector<Point> children{{20,0}};
    int selected=seed.initialTile(0,1,{20,10},receivers,1,occupied,-1,20,nullptr,children);
    check(seed.tiles.at(selected).in.y==0,"flat exact total chooses lower midpoint; receiver-only cost would differ");
    const double cost0=reference(seed,seed.plans[0]);
    seed.rebuild({});check(seed.plans.empty()&&seed.deleted==1,"crossing removal removes its terms");
    std::cout<<"PASS source masking, fanout, multi-boundary, transit, cache, initialization and lifecycle; weighted_before="<<before<<" after="<<after<<" seed_cost="<<cost0<<"\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
