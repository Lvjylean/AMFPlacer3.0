#include "../lib/HiFPlacer/placement/placementInfo/SllVirtualModel.h"
#include <iostream>
#include <iomanip>
using namespace amf_sll_plan;
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static Channel channel(int from,int col,int row,int lane,bool down=false){
    Channel c;c.resource="b"+std::to_string(from)+"c"+std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=from+(down?1:0);c.to=from+(down?0:1);c.column=col;
    c.in={double(col),double(row+(down?20:0))};c.out={double(col),double(row+(down?0:20))};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};return p;}
static Model inventory(){
    Model m;m.jointPlacement=true;m.sourceNearInitialization=true;m.allowSplit=false;
    for(int col:{20,80})for(int row=10;row<=14;++row)for(int lane=0;lane<24;++lane)for(bool down:{false,true})
        m.channels.push_back(channel(0,col,row,lane,down));
    m.index();return m;
}
static Net net(int id,double sx=21,double sy=0,double rx=80,double ry=60,int fanout=1){
    Net n;n.id=id;n.name="n"+std::to_string(id);n.source=pin(id*100,0,sx,sy);
    for(int k=1;k<=fanout;++k)n.sinks.push_back(pin(id*100+k,1,rx,ry));return n;
}
static int tile(const Model&m,int col,int row,bool down=false){
    for(size_t i=0;i<m.tiles.size();++i){const auto&t=m.tiles[i];
        if(t.column==col&&t.from==(down?1:0)&&t.in.y==row+(down?20:0))return i;
    }throw std::runtime_error("missing test tile");
}
static void report(const std::string&name,const Model&m,int ti,size_t calls=0){
    const auto&t=m.tiles.at(ti);
    std::cout<<"RESULT\t"<<name<<"\t"<<t.column<<"\t"<<t.in.y<<"\t"<<t.out.y<<"\t"<<m.initialTileEvaluations<<"\t"<<calls<<"\n";
}
static void chosen(const Model&m,int col,double y,const char*why){
    const auto&t=m.geometry(m.plans.at(0).crossings.at(0));check(t.column==col&&t.in.y==y,why);
}
int main(){try{
    Model m=inventory();size_t delayCalls=0;
    m.local=[&](Point,Point)->double{++delayCalls;throw std::runtime_error("initial tile selection called delay model");};
    m.rebuild({net(1)});chosen(m,20,12,"nearest source X and midpoint of flat Y interval");
    check(delayCalls==0&&m.initialTileEvaluations==10,"only five nearest-column rows in each of two passes");
    report("flat_y_midpoint",m,m.plans[0].crossings[0].tile,delayCalls);
    auto right=inventory();right.rebuild({net(1,79,0,20,60)});
    chosen(right,80,12,"source X has priority over receiver X");report("source_x_priority",right,right.plans[0].crossings[0].tile);
    auto receive=inventory();receive.rebuild({net(1,21,0,80,30)});
    chosen(receive,20,10,"receiver Y constrains initial tile");report("receiver_y_low",receive,receive.plans[0].crossings[0].tile);
    receive.previous.clear();receive.rebuild({net(1,21,0,80,34)});
    chosen(receive,20,12,"receiver Y expands equivalent interval");report("receiver_y_high",receive,receive.plans[0].crossings[0].tile);
    auto many=inventory();many.rebuild({net(1,21,0,80,60,6)});
    chosen(many,20,12,"co-located fanout does not multiply shared geometry");report("fanout6_shared_box",many,many.plans[0].crossings[0].tile);
    auto balanced=inventory();balanced.rebuild({net(1),net(2)});
    check(balanced.geometry(balanced.plans[0].crossings[0]).in.y==12,"first net starts at midpoint");
    check(balanced.geometry(balanced.plans[1].crossings[0]).in.y==11,"second net chooses lower utilization before midpoint");
    report("second_net_low_utilization",balanced,balanced.plans[1].crossings[0].tile);
    auto retain=inventory();retain.rebuild({net(1)});
    retain.previous[retain.plans[0].crossings[0].key]=tile(retain,20,14);
    retain.rebuild({net(1)});chosen(retain,20,14,"equal utilization retains incumbent before center");
    report("retain_equal_incumbent",retain,retain.plans[0].crossings[0].tile);
    TileBox output;output.add({80,60});
    std::map<TileKey,size_t> used;used[m.tiles[tile(m,20,14)].key]=1;
    int ti=m.initialTile(0,1,{21,0},output,1,used,tile(m,20,14));
    check(m.tiles[ti].in.y==11,"less loaded tile beats incumbent under soft threshold");
    report("utilization_before_incumbent",m,ti);
    TileBox tightOut;tightOut.add({80,32});
    used.clear();used[m.tiles[tile(m,20,12)].key]=20;
    ti=m.initialTile(0,1,{21,12},tightOut,1,used);
    check(m.tiles[ti].in.y==11,"density price can outweigh small wirelength increase");
    report("soft_capacity_pressure",m,ti);
    used.clear();for(int row=10;row<=14;++row)used[m.tiles[tile(m,20,row)].key]=24;
    ti=m.initialTile(0,1,{21,0},output,1,used);
    check(m.tiles[ti].column==80&&m.tiles[ti].in.y==12,"full nearest column expands to next column");
    report("nearest_column_full",m,ti);
    for(int row=10;row<=14;++row)used[m.tiles[tile(m,80,row)].key]=24;
    check(m.initialTile(0,1,{21,0},output,1,used)==-1,"all full returns explicit failure");
    auto normalized=inventory();normalized.channels.clear();
    for(int row:{10,14})for(int lane=0;lane<(row==10?4:24);++lane)
        normalized.channels.push_back(channel(0,20,row,lane));
    normalized.index();normalized.initialDensityWeight=0;used.clear();
    used[normalized.tiles[tile(normalized,20,10)].key]=1;
    used[normalized.tiles[tile(normalized,20,14)].key]=3;
    ti=normalized.initialTile(0,1,{21,0},output,1,used);
    check(normalized.tiles[ti].in.y==14,"utilization uses capacity ratio, not occupancy count");
    report("normalized_utilization",normalized,ti);
    auto reversed=inventory();std::reverse(reversed.channels.begin(),reversed.channels.end());reversed.index();
    reversed.rebuild({net(1)});chosen(reversed,20,12,"stable physical ordering independent of inventory order");
    auto times=inventory();for(auto&c:times.channels)c.slowMax=c.in.y==10?0.01:10;
    for(auto&c:times.channels)c.slowMin=.001;
    times.index();times.timingWeight=1e9;times.local=[](Point,Point)->double{throw std::runtime_error("delay consulted");};
    times.rebuild({net(1)});chosen(times,20,12,"SLL delay and timing weight cannot change fixed-topology seed");
    report("timing_independence",times,times.plans[0].crossings[0].tile);
    auto shared=inventory();Net up=net(1),down=net(2,21,60,80,0);down.source.slr=1;down.sinks[0].slr=0;
    shared.rebuild({up,down});check(shared.geometry(shared.plans[0].crossings[0]).in.y==12,"up midpoint");
    check(shared.geometry(shared.plans[1].crossings[0]).in.y==31,"opposite directions share tile load");
    report("opposite_direction_load",shared,shared.plans[1].crossings[0].tile);
    auto multi=inventory();for(int col:{20,80})for(int row=70;row<=74;++row)for(int lane=0;lane<24;++lane)
        multi.channels.push_back(channel(1,col,row,lane));
    multi.index();Net chain=net(1);chain.sinks.push_back(pin(102,2,80,110));chain.sinks.push_back(pin(103,0,20,5));
    multi.local=[](Point,Point)->double{throw std::runtime_error("chain initialization called delay");};
    multi.rebuild({chain});const auto&p=multi.plans[0];
    check(p.crossings.size()==2&&p.paths.at(101).size()==1&&p.paths.at(102).size()==2&&p.paths.at(103).empty(),"multi-SLR tree preserves local and remote leaves");
    check(multi.geometry(p.crossings[0]).in.y==14&&multi.geometry(p.crossings[1]).in.y==72,"separate landing receivers and outgoing transit leg favor upper first row");
    multi.legalizeChannels();for(const auto&e:multi.plans[0].crossings)check(e.channel>=0,"final exact lane legalizes");
    multi.rebuild({chain});for(const auto&e:multi.plans[0].crossings)check(e.channel==-1,"QP stage defers exact lane");
    multi.rebuild({});check(multi.plans.empty(),"lifecycle removes obsolete crossing");
    std::cout<<"PASS initial tile geometry, capacity, source X, utilization/incumbent/midpoint Y, no timing score, multi-SLR lifecycle\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}

