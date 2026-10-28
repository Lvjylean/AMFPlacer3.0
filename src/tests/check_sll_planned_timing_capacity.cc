#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
static Model inventory(){
    Model m;m.jointPlacement=m.sourceNearInitialization=true;m.columnPolicy.enabled=true;
    for(int col:{20,80})for(int row=0;row<10;++row)for(int lane=0;lane<10;++lane)for(bool down:{false,true}){
        Channel c;c.resource=std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);c.segment=c.resource+"_a,"+c.resource+","+c.resource+"_b";
        c.from=down?1:0;c.to=down?0:1;c.column=col;c.in={double(col),double(row+(down?100:40))};c.out={double(col),double(row+(down?40:100))};
        c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;m.channels.push_back(c);
    }m.index();return m;
}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};return p;}
static std::vector<Net> nets(int count){
    std::vector<Net> v;for(int i=0;i<count;++i){bool down=i%2;Net n;n.id=i;n.source=pin(i*10,down?1:0,20,down?130:0);
        n.sinks={pin(i*10+1,down?0:1,20,down?0:130)};v.push_back(n);}return v;
}
static std::map<int,int> counts(const Model&m){std::map<int,int> result;for(const auto&p:m.plans)for(const auto&e:p.crossings)++result[m.tiles.at(e.tile).column];return result;}
int main(){try{
    ColumnPolicy policy;policy.enabled=true;check(policy.tier(1080,1440)==0,"75% preferred threshold");
    check(policy.tier(1081,1440)==1,"one-percent tier relaxation");
    check(policy.tier(1223,1440)!=ColumnPolicy::unavailable&&policy.tier(1224,1440)==ColumnPolicy::unavailable,"strict 85% ceiling");
    for(int size:{150,151,168}){
        auto m=inventory();m.rebuild(nets(size));auto c=counts(m);
        check(c[20]+c[80]==size&&c[20]<=84&&c[80]<=84,"initial hard column capacity");
        if(size==150)check(c[20]==75&&c[80]==75,"all preferred columns used before relaxation");
        DensityModel d;d.index(m);std::vector<Point> desired;
        for(const auto&p:m.plans)desired.push_back({20,p.net.source.slr?100.:40.});
        auto projected=d.assign(m,desired);size_t i=0;
        for(auto&p:m.plans)for(auto&e:p.crossings){e.tile=projected.tiles[i++];m.previous[e.key]=e.tile;}
        c=counts(m);check(c[20]<=84&&c[80]<=84,"post-QP hard column ceiling");
        if(size==150)check(c[20]==75&&c[80]==75,"QP cannot reconcentrate into one column");
        auto spread=d.spread(m,desired);std::map<int,int> sc;
        for(int ti:spread.tiles)++sc[m.tiles[ti].column];
        check(sc[20]<=84&&sc[80]<=84,"density targets respect hard column ceiling");
        m.legalizeChannels();std::set<std::string> lanes;
        for(const auto&p:m.plans)for(const auto&e:p.crossings){check(e.channel>=0,"parallel precise lanes assigned");check(lanes.insert(m.channels[e.channel].resource).second,"no parallel lane collision");}
        std::cout<<"CAPACITY count="<<size<<" col20="<<c[20]<<" col80="<<c[80]<<"\n";
    }
    {
        auto m=inventory();m.rebuild(nets(150));
        for(auto&p:m.plans)if(m.tiles[p.crossings[0].tile].column==80){
            auto&e=p.crossings[0];const auto old=m.tiles[e.tile];
            for(size_t ti=0;ti<m.tiles.size();++ti)if(m.tiles[ti].column==20&&m.tiles[ti].from==old.from&&m.tiles[ti].in.y==old.in.y){e.tile=ti;break;}
            break;
        }
        check(counts(m)[20]==76&&counts(m)[80]==74,"post-packing overshoot fixture");
        DensityModel d;d.index(m);std::vector<Point> desired;
        for(const auto&p:m.plans)for(const auto&e:p.crossings)desired.push_back(m.tiles[e.tile].in);
        auto final=d.assign(m,desired);std::map<int,int> c;
        for(int ti:final.tiles)++c[m.tiles[ti].column];
        check(c[20]==75&&c[80]==75,"final export must restore preferred column tier");
        std::cout<<"FINAL_RECONCILIATION before=76/74 after=75/75 moved="<<final.moved<<"\n";
    }
    auto fail=inventory();bool rejected=false;try{fail.rebuild(nets(169));}catch(const std::runtime_error&){rejected=true;}check(rejected,"cannot reach 85%");
    auto timing=inventory();timing.local=[](Point a,Point b){return std::abs(a.x-b.x)*.01;};
    auto input=nets(1);input[0].sinks[0].timingBudget=1.4;timing.rebuild(input);
    auto&p=timing.plans[0];for(size_t i=0;i<timing.tiles.size();++i)if(timing.tiles[i].from==0&&timing.tiles[i].column==80){p.crossings[0].tile=i;break;}
    timing.updatePlannedCriticalities(p);
    check(std::abs(timing.pathDelay(p,p.net.sinks[0].id,p.net.source.p,p.net.sinks[0].p)-1.97)<1e-9,"actual selected crossing delay");
    check(std::abs(p.net.sinks[0].criticality-1.57)<1e-9,"planned violation must not be hidden by alternative crossing");
    std::cout<<"TIMING planned_delay=1.97 budget=1.4 criticality=1.57; unused_alternative_delay=0.77\n";
    std::cout<<"PASS planned timing, adaptive column tiers, tile capacity, post-QP ceiling and parallel lanes\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
