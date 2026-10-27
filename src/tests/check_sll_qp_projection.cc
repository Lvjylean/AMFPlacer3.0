#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include "../lib/HiFPlacer/placement/placementInfo/SllQpMotion.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static Channel channel(int col,int row,int lane,bool down=false){
    Channel c;c.resource="c"+std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=down?1:0;c.to=down?0:1;c.column=col;
    c.in={double(col),double(row+(down?20:0))};c.out={double(col),double(row+(down?0:20))};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};return p;}
static Net net(int id,double x=42,bool down=false){
    Net n;n.id=id;n.source=pin(id*10,down?1:0,x,down?150:50);
    n.sinks={pin(id*10+1,down?0:1,x,down?50:150)};return n;
}
static Model inventory(int lanes=24){
    Model m;m.jointPlacement=true;m.sourceNearInitialization=true;m.allowSplit=false;
    for(int col:{42,54,62})for(int row:{90,91,92})for(int k=0;k<lanes;++k)for(bool down:{false,true})
        m.channels.push_back(channel(col,row,k,down));
    m.index();return m;
}
static void commit(Model&m,const DensityModel::Assignment&a){
    size_t i=0;for(auto&p:m.plans)for(auto&e:p.crossings){e.tile=a.tiles.at(i++);e.channel=-1;m.previous[e.key]=e.tile;}
}
static void show(const char*name,const Model&m,Point q,int ti){
    const auto&t=m.tiles.at(ti);std::cout<<"RESULT\t"<<name<<'\t'<<q.x<<'\t'<<q.y<<'\t'<<t.in.x<<'\t'<<t.in.y<<'\n';
}
int main(){try{
    for(int gap:{11,12})for(int direction:{1,-1}){
        const double left=gap==11?14:42,right=left+gap;
        const double start=direction>0?left:right,destination=direction>0?right:left;
        Model bounded;bounded.jointPlacement=bounded.sourceNearInitialization=true;bounded.allowSplit=false;
        bounded.channels={channel(int(left),90,0),channel(int(right),90,0)};bounded.index();
        bounded.rebuild({net(1,start)});DensityModel bd;bd.index(bounded);
        const Point origin=bounded.geometry(bounded.plans[0].crossings[0]).in;
        const Point raw{start+direction*100,origin.y},limited=limitedQpPosition(origin,raw,10);
        check(std::abs(std::hypot(limited.x-origin.x,limited.y-origin.y)-10)<1e-9,"continuous displacement clipped to 10");
        const auto projected=bd.assign(bounded,{limited});const auto&tile=bounded.tiles[projected.tiles[0]];
        check(tile.in.x==destination,"adjacent 11/12-gap column remains reachable after clipping");
        check(std::abs(tile.in.x-origin.x)==gap,"no post-legalization displacement clipping");
        std::cout<<"LIMIT_RESULT\t"<<gap<<'\t'<<direction<<'\t'<<origin.x<<'\t'<<raw.x<<'\t'<<limited.x<<'\t'<<tile.in.x<<'\n';
    }
    const Point origin{42,90};const auto diagonal=limitedQpPosition(origin,{72,130},10);
    check(diagonal.x==48&&diagonal.y==98,"2D L2 displacement, not independent axis clipping");
    const auto stationary=limitedQpPosition(origin,origin,10);
    check(stationary.x==42&&stationary.y==90,"zero displacement is finite");
    check(limitedQpPosition(origin,{48,98},10).x==48,"exact boundary unchanged");
    check(limitedQpPosition(origin,{45,94},10).y==94,"small movement unchanged");
    check(limitedQpPosition(origin,{142,90},-10).x==142&&limitedQpPosition(origin,{142,90},0).x==142,"disabled limit preserves legacy behavior");
    bool invalid=false;try{limitedQpPosition(origin,{NAN,90},10);}catch(const std::runtime_error&){invalid=true;}
    check(invalid,"non-finite movement rejected");
    auto m=inventory();Net n=net(1);m.rebuild({n});DensityModel d;d.index(m);
    // Ordinary wire/timing costs favor X=42, but legalization must obey QP.
    m.local=[](Point,Point)->double{throw std::runtime_error("projection queried timing");};
    m.timingWeight=m.switchCost=1e12;
    for(const auto&q:std::vector<Point>{{47.9,91},{48.1,91},{52,91},{61,91.8}}){
        auto a=d.assign(m,{q});const auto&t=m.tiles[a.tiles[0]];
        const int expected=q.x<48?42:(q.x<58?54:62);
        check(t.in.x==expected&&t.in.y==(q.y>91.5?92:91),"nearest solved position controls both axes");
        show("qp_scan_fixed_endpoints",m,q,a.tiles[0]);
    }
    auto a=d.assign(m,{{52,91}});
    check(m.tiles[a.tiles[0]].in.x==54,"10-unit relaxed move crosses a 12-unit legal-column gap");
    commit(m,a);m.rebuild({n});check(d.current(m)==a.tiles&&m.initialTileEvaluations==0,"refresh preserves solved X and Y");
    n.source.p={42,90};n.sinks[0].p={42,110};m.rebuild({n});
    check(d.current(m)==a.tiles,"updated endpoints do not reseed existing crossings");
    m.legalizeChannels();check(d.current(m)==a.tiles,"exact lane legalization preserves QP tile");
    check(m.plans[0].crossings[0].channel>=0,"exact channel allocated only at final legalization");
    m.rebuild({});check(m.plans.empty(),"obsolete virtual cell removed");
    auto tied=inventory();tied.rebuild({net(1)});DensityModel td;td.index(tied);
    auto tie=td.assign(tied,{{48,91}});check(tied.tiles[tie.tiles[0]].in.x==42,"equidistant incumbent retained");
    auto cap=inventory(1);std::vector<Net> nets;
    for(int i=0;i<9;++i)nets.push_back(net(i,42,i%2));
    cap.rebuild(nets);DensityModel cd;cd.index(cap);
    std::vector<Point> q;for(const auto&p:cap.plans)q.push_back({42,p.net.source.slr?110.:90.});
    auto full=cd.assign(cap,q);commit(cap,full);std::map<TileKey,int> used;
    for(int ti:full.tiles)check(++used[cap.tiles[ti].key]<=1,"up/down requests share canonical tile capacity");
    cap.legalizeChannels();std::set<std::string> paths;
    for(const auto&p:cap.plans)for(const auto&e:p.crossings){check(!Model::conflicts(cap.channels[e.channel],paths),"no exact lane conflict");Model::occupy(cap.channels[e.channel],paths);}
    std::cout<<"PASS full capacity, mixed directions and precise lanes\n";
    // A full two-slot inventory must allow both crossings to exchange columns.
    Model swap;swap.jointPlacement=swap.sourceNearInitialization=true;swap.allowSplit=false;
    swap.channels={channel(42,90,0),channel(54,90,0)};swap.index();swap.rebuild({net(1,42),net(2,54)});
    DensityModel sd;sd.index(swap);auto exchanged=sd.assign(swap,{{54,90},{42,90}});
    check(swap.tiles[exchanged.tiles[0]].in.x==54&&swap.tiles[exchanged.tiles[1]].in.x==42,"simultaneous full-tile swap is possible");
    std::cout<<"PASS simultaneous full-tile exchange\n";
    // Up can use A/B; down can use only A. A greedy early choice needs repair.
    Model asym;asym.jointPlacement=asym.sourceNearInitialization=true;asym.allowSplit=false;
    asym.channels={channel(42,90,0),channel(42,90,0,true),channel(54,90,0)};asym.index();
    asym.rebuild({net(1,54),net(2,42,true)});DensityModel ad;ad.index(asym);
    auto repair=ad.assign(asym,{{42,90},{42,110}});
    check(asym.tiles[repair.tiles[0]].in.x==54&&asym.tiles[repair.tiles[1]].in.x==42,"augmenting path repairs asymmetric greedy conflict");
    std::cout<<"PASS augmenting-path capacity repair\n";
    bool bad=false;try{sd.assign(swap,{{NAN,90},{42,90}});}catch(const std::runtime_error&){bad=true;}
    check(bad,"invalid QP proposal rejected");
    check(sd.current(swap)!=exchanged.tiles,"projection remains transactional until commit");
    std::cout<<"PASS QP projection, refresh retention, no physical rescoring, capacity and lifecycle\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
