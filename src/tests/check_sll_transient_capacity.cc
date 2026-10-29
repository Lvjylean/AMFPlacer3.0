#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool v,const char*s){if(!v)throw std::runtime_error(s);}
static Model inventory(bool transient){
    Model m;m.jointPlacement=m.sourceNearInitialization=true;m.columnPolicy.enabled=true;
    m.allowTransientOverflow=transient;
    for(int col:{20,80})for(int row=0;row<10;++row)for(int lane=0;lane<10;++lane)for(bool down:{false,true}){
        Channel c;c.resource=std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
        c.segment=c.resource+"_a,"+c.resource+","+c.resource+"_b";c.from=down?1:0;c.to=down?0:1;c.column=col;
        c.in={double(col),double(row+(down?100:40))};c.out={double(col),double(row+(down?40:100))};
        c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;m.channels.push_back(c);
    }m.index();return m;
}
static std::vector<Net> nets(int count){
    std::vector<Net> v;
    for(int i=0;i<count;++i){
        Net n;n.id=i;n.name="n"+std::to_string(i);n.source.id=n.source.unit=i*10;
        n.source.slr=i%2;n.source.p={20,n.source.slr?130.:0.};
        Pin sink;sink.id=sink.unit=i*10+1;sink.slr=1-n.source.slr;sink.p={80,sink.slr?130.:0.};
        n.sinks={sink};v.push_back(n);
    }return v;
}
static std::vector<Point> desired(const Model&m){
    std::vector<Point> q;
    for(const auto&p:m.plans)for(const auto&e:p.crossings)q.push_back({20,m.tiles[e.tile].from?100.:40.});
    return q;
}
static void commit(Model&m,const DensityModel::Assignment&a){
    size_t i=0;for(auto&p:m.plans)for(auto&e:p.crossings){e.tile=a.tiles.at(i++);e.channel=-1;m.previous[e.key]=e.tile;}
}
int main(){try{
    auto strict=inventory(false);bool rejected=false;
    try{strict.rebuild(nets(169));}catch(const std::exception&){rejected=true;}
    check(rejected,"legacy default stays strict");
    auto normal=inventory(true),reference=inventory(false);normal.rebuild(nets(150));reference.rebuild(nets(150));
    DensityModel nd;nd.index(normal);
    check(normal.transientBoundaries.empty(),"feasible demand never relaxes");
    check(nd.current(normal)==nd.current(reference),"feasible initialization unchanged");
    check(nd.assign(normal,desired(normal)).tiles==nd.assign(reference,desired(reference)).tiles,"feasible QP projection unchanged");
    for(int count:{169,201,280}){
        auto m=inventory(true);m.rebuild(nets(count));DensityModel d;d.index(m);
        check(m.transientBoundaries==std::set<int>{0},"only overloaded boundary relaxed");
        check(m.plans.size()==size_t(count),"all overfull requests remain in optimization");
        for(const auto&p:m.plans)check(p.crossings.size()==1&&p.crossings[0].channel==-1,"one virtual crossing; no claimed physical lane");
        auto spread=d.spread(m,desired(m));check(std::isfinite(spread.energyBefore)&&std::isfinite(spread.energyAfter),"physical density remains finite and measured");
        check(d.capacity.size()==20,"physical tile inventory unchanged");
        for(const auto&e:d.capacity)check(e.second==10,"physical supply not inflated");
        commit(m,d.assign(m,desired(m)));
        rejected=false;try{d.assign(m,desired(m),true);}catch(const std::exception&){rejected=true;}
        check(rejected,"strict export refuses excess column demand");
        rejected=false;try{m.legalizeChannels();}catch(const std::exception&){rejected=true;}
        check(rejected,"precise lane allocator independently refuses overbooking");
        m.rebuild(nets(150));check(m.transientBoundaries.empty(),"relaxation ends after demand recovers");
        commit(m,d.assign(m,desired(m),true));m.legalizeChannels();std::set<std::string> used;
        for(const auto&p:m.plans)for(const auto&e:p.crossings){
            check(e.channel>=0&&!Model::conflicts(m.channels[e.channel],used),"recovered exact routes disjoint");
            Model::occupy(m.channels[e.channel],used);
        }
        m.rebuild({});check(m.plans.empty()&&m.transientBoundaries.empty(),"deleted crossings release virtual demand");
        std::cout<<"PASS transient="<<count<<" recovered=150 exact_lanes=150\n";
    }
    auto mixed=inventory(true);const auto first=mixed.channels;
    for(auto channel:first){
        channel.resource="boundary1_"+channel.resource;
        channel.segment=channel.resource+"_a,"+channel.resource+","+channel.resource+"_b";
        ++channel.from;++channel.to;channel.in.y+=200;channel.out.y+=200;mixed.channels.push_back(channel);
    }
    mixed.index();auto requests=nets(280);auto second=nets(150);
    for(auto&net:second){
        net.id+=10000;net.source.id+=100000;net.source.unit=net.source.id;++net.source.slr;net.source.p.y+=200;
        for(auto&pin:net.sinks){pin.id+=100000;pin.unit=pin.id;++pin.slr;pin.p.y+=200;}
        requests.push_back(net);
    }
    mixed.rebuild(requests);check(mixed.transientBoundaries==std::set<int>{0},"independent boundary is not relaxed");
    DensityModel md;md.index(mixed);auto assignment=md.assign(mixed,desired(mixed));
    std::map<TileKey,size_t> load;std::map<int,size_t> columns;
    for(int ti:assignment.tiles){const auto&tile=mixed.tiles[ti];
        if(std::min(tile.from,tile.to)==1){check(++load[tile.key]<=10,"unpressured boundary tile stays physical");++columns[tile.column];}
    }
    for(const auto&entry:columns)check(entry.second<=84,"unpressured boundary column stays below85");
    std::cout<<"PASS overload isolation: boundary0=280 boundary1=150\n";
    auto zero=inventory(true);for(const auto&c:zero.channels)zero.reserved.insert(c.resource);zero.indexTiles();
    rejected=false;try{zero.rebuild(nets(1));}catch(const std::exception&){rejected=true;}
    check(rejected,"no relaxation creates unavailable physical geometry");
    std::cout<<"PASS transient capacity, unchanged feasible cases, density supply, strict export and lifecycle\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
