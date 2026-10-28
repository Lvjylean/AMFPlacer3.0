#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool x,const char*s){if(!x)throw std::runtime_error(s);}
static Channel channel(int col,int row,int lane,bool down){
    Channel c;c.resource="c"+std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=down?1:0;c.to=down?0:1;c.column=col;
    c.in={double(col),double(row+(down?20:0))};c.out={double(col),double(row+(down?0:20))};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};p.movable=true;return p;}
static Model inventory(){
    Model m;m.jointPlacement=true;m.sourceNearInitialization=true;m.allowSplit=false;
    for(int col:{20,80})for(int row=90;row<105;++row)for(int lane=0;lane<24;++lane)for(bool down:{false,true})
        m.channels.push_back(channel(col,row,lane,down));
    m.index();return m;
}
static void commit(Model&m,const DensityModel::Assignment&a){
    size_t i=0;for(auto&p:m.plans)for(auto&e:p.crossings){e.tile=a.tiles.at(i++);e.channel=-1;m.previous[e.key]=e.tile;}
}
int main(){try{
    Model m=inventory();m.switchCost=1e12;std::vector<Net> nets;
    for(int i=0;i<24;++i){
        Net n;n.id=i;n.source=pin(i*10,0,20,50);
        for(int k=1;k<=6;++k)n.sinks.push_back(pin(i*10+k,1,80,150));
        nets.push_back(n);
    }
    m.rebuild(nets);DensityModel d;d.index(m);
    check(d.capacity.size()==30,"distinct tile density bins");
    for(const auto&v:d.capacity)check(v.second==24,"24 lanes shared by both directions");
    for(const auto&p:m.plans)for(const auto&e:p.crossings){
        check(e.channel==-1,"no exact lane during global placement");
        check(m.tiles[e.tile].column==20,"source-near initial X");
    }
    // The simplified initializer already balances equal-cost Y rows. Force a
    // legal hotspot explicitly so this remains a post-QP density regression.
    const int hotspot=m.plans[0].crossings[0].tile;
    for(auto&p:m.plans)for(auto&e:p.crossings){e.tile=hotspot;m.previous[e.key]=hotspot;}
    const auto hot=d.bin(m.geometry(m.plans[0].crossings[0]));
    check(d.penalty(hot,19)==0&&d.penalty(hot,20)>0,"80 percent tile target");
    std::vector<Point> desired(24,m.tiles[hotspot].in);auto spread=d.spread(m,desired);
    check(spread.moved==5&&spread.energyBefore>0&&spread.energyAfter==0,"tile hotspot spreads at default weights");
    auto projected=d.assign(m,desired);
    check(projected.moved==0&&projected.energyAfter==projected.energyBefore,"QP projection does not repeat soft density optimization");
    DensityModel coarse;coarse.rowHeight=15;coarse.index(m);auto control=coarse.spread(m,desired);
    check(control.moved==0&&control.energyBefore==0,"coarse density misses single-tile hotspot");
    commit(m,spread);m.legalizeChannels();std::set<std::string> used;
    for(const auto&p:m.plans)for(const auto&e:p.crossings){
        check(e.channel>=0,"final legalization selects exact lane");
        check(!Model::conflicts(m.channels[e.channel],used),"final exact paths are disjoint");
        Model::occupy(m.channels[e.channel],used);
        check(m.channels[e.channel].in.y==m.tiles[e.tile].in.y,"lane legalization preserves tile");
    }
    m.rebuild(nets);check(m.retained==24&&m.created==0,"refresh retains crossing identities");
    for(const auto&p:m.plans)for(const auto&e:p.crossings)check(e.channel==-1,"refresh releases lane choice");
    Model y=inventory();y.timingWeight=0;y.switchCost=1e12;
    Net n;n.id=1;n.source=pin(1,0,20,92);n.sinks={pin(2,1,80,110)};
    y.rebuild({n});auto old=y.plans[0].crossings[0].tile;
    check(y.tiles[old].in.y==91,"initial receiver-side Y uses tied interval midpoint");
    y.plans[0].net.sinks[0].p.y=114;DensityModel yd;yd.index(y);auto move=yd.assign(y,{y.tiles[old].in});
    check(y.tiles[move.tiles[0]].in.y==91,"receiver Y alone cannot override solved virtual coordinates");
    move=yd.assign(y,{{20,92}});
    check(y.tiles[move.tiles[0]].in.y==92,"QP Y changes tile choice");
    check(y.tiles[move.tiles[0]].column==20,"source-side favorable X retained");
    y.previous.clear();n.sinks[0].p.y=114;y.rebuild({n});
    check(y.geometry(y.plans[0].crossings[0]).in.y==93,"newborn Y includes output side and tied interval midpoint");
    Model shared;shared.jointPlacement=true;shared.allowSplit=false;shared.initialColumnUtilizationLimit=1;
    for(int lane=0;lane<2;++lane)for(bool down:{false,true})shared.channels.push_back(channel(20,90,lane,down));
    shared.index();Net up{1,"up",pin(1,0,20,50),{pin(2,1,20,150)},1},down{2,"down",pin(3,1,20,150),{pin(4,0,20,50)},1};
    shared.rebuild({up,down});shared.legalizeChannels();
    check(shared.channels[shared.plans[0].crossings[0].channel].resource!=shared.channels[shared.plans[1].crossings[0].channel].resource,"opposite directions share physical SLL supply");
    std::cout<<"PASS tile planning: capacity=24, density target moved=5, QP coordinates control projection, deferred exact legalization\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
