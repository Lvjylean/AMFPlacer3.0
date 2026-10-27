#include "SllDensityModel.h"
#define DensityModel LegacyDensityModel
#include "fixtures/sll_density_legacy_a4aad90.h"
#undef DensityModel
#include <chrono>
#include <iostream>
#include <random>

using namespace amf_sll_plan;
static size_t cases=0,proposals=0;static double oldSeconds=0,newSeconds=0;
static void check(bool value){if(!value)throw std::runtime_error("Density assignment differs from legacy implementation");}
static Channel channel(int x,int y,int lane,bool down){
    Channel c;c.resource="X"+std::to_string(x)+"Y"+std::to_string(y)+"L"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=down?1:0;c.to=down?0:1;c.column=x;c.in={double(x),double(y+(down?80:0))};
    c.out={double(x),double(y+(down?0:80))};c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;return c;
}
static void compare(Model&m,const std::vector<Point>&desired,double rows,double target,double weight){
    DensityModel dense;LegacyDensityModel legacy;
    dense.rowHeight=legacy.rowHeight=rows;dense.targetUtilization=legacy.targetUtilization=target;
    dense.weight=legacy.weight=weight;dense.index(m);legacy.index(m);
    const auto original=dense.current(m);
    for(bool projection:{false,true}){
        auto started=std::chrono::steady_clock::now();
        const auto expected=projection?legacy.assign(m,desired):legacy.spread(m,desired);
        oldSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        started=std::chrono::steady_clock::now();
        const auto actual=projection?dense.assign(m,desired):dense.spread(m,desired);
        newSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        check(expected.tiles==actual.tiles&&expected.moved==actual.moved&&
              expected.energyBefore==actual.energyBefore&&expected.energyAfter==actual.energyAfter);
        check(dense.current(m)==original);++cases;proposals+=desired.size();
    }
}
int main(){
    Model m;m.jointPlacement=true;
    const int xs[]={5,14,25,33,42,54,62,71,81,90,99,107,118,126,137,147};
    for(int x:xs)for(int y=90;y<150;++y)for(int lane=0;lane<24;++lane)for(bool down:{false,true})
        m.channels.push_back(channel(x,y,lane,down));
    m.index();
    // Include unavailable lanes; both directions share the physical supply.
    for(size_t i=0;i<m.channels.size();i+=194)m.reserved.insert(m.channels[i].resource);
    m.indexTiles();std::map<TileKey,std::vector<int>> byKey;
    for(size_t i=0;i<m.tiles.size();++i)byKey[m.tiles[i].key].push_back(int(i));
    std::mt19937 rng(140729);std::vector<std::vector<int>> slots;
    for(const auto&entry:byKey)for(size_t i=0;i<m.tileCapacity.at(entry.first);++i)slots.push_back(entry.second);
    const size_t sizes[]={0,1,2,6,24,120,1200,8000,16000};
    for(int frame=0;frame<18;++frame){
        std::shuffle(slots.begin(),slots.end(),rng);m.plans.clear();std::vector<Point> desired;
        m.ratio=frame%5==0?0:.4;
        for(size_t i=0;i<sizes[frame%9];++i){
            Plan p;p.net.id=int(i);p.net.name="test_net_"+std::to_string(i);
            for(int j=0;j<8;++j){Pin pin;pin.name=std::string(80,'s')+std::to_string(j);p.net.sinks.push_back(pin);}
            Crossing e;e.tile=slots[i][rng()%slots[i].size()];e.channel=frame%2?m.tiles[e.tile].members[0]:-1;
            p.crossings.push_back(e);const auto&t=m.tiles[e.tile];m.plans.push_back(std::move(p));
            desired.push_back(frame%3==0?t.in:Point{double(int(rng()%250)-40),double(int(rng()%120)-10+(t.from?170:90))});
        }
        if(frame%4==0)for(auto&entry:m.availableTiles)std::shuffle(entry.second.begin(),entry.second.end(),rng);
        compare(m,desired,frame%3==0?.5:(frame%3==1?1:3),frame%2?.8:.5,frame%4?256:0);
    }
    // Force augmenting-path repair: up can use A/B, down can only use A.
    Model asym;asym.jointPlacement=true;
    asym.channels={channel(42,90,0,false),channel(42,90,0,true),channel(54,90,0,false)};asym.index();
    Plan up,down;Crossing a,b;
    for(size_t i=0;i<asym.tiles.size();++i){const auto&t=asym.tiles[i];
        if(t.from==0&&t.column==54)a.tile=int(i);if(t.from==1)b.tile=int(i);}
    up.crossings.push_back(a);down.crossings.push_back(b);asym.plans={up,down};
    compare(asym,{{42,90},{42,170}},1,.8,256);
    std::cout<<"PASS exact density cases="<<cases<<" crossing proposals="<<proposals
             <<" legacy_seconds="<<oldSeconds<<" dense_seconds="<<newSeconds<<"\n";
}
