#include "../lib/HiFPlacer/placement/placementInfo/SllDensityModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
static Pin pin(int id,int slr,double x,double y){Pin p;p.id=p.unit=id;p.slr=slr;p.p={x,y};p.criticality=id%2?64:1;return p;}
static Model inventory(){
    Model m;m.jointPlacement=true;m.sourceNearInitialization=true;
    check(!m.allowSplit,"default split policy must be disabled");
    for(int boundary=0;boundary<3;++boundary)for(int col:{20,80})for(int row=10;row<=14;++row)for(int lane=0;lane<24;++lane)for(bool down:{false,true}){
        Channel c;c.resource="b"+std::to_string(boundary)+"c"+std::to_string(col)+"r"+std::to_string(row)+"l"+std::to_string(lane);
        c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
        c.from=boundary+(down?1:0);c.to=boundary+(down?0:1);c.column=col;
        c.in={double(col),double(boundary*100+row+(down?20:0))};c.out={double(col),double(boundary*100+row+(down?0:20))};
        c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.1;m.channels.push_back(c);
    }
    m.index();
    m.local=[](Point,Point)->double{throw std::runtime_error("no-split initialization must not score timing alternatives");};
    return m;
}
static void verify(const Model&m,const Net&n){
    const auto&p=m.plans.at(0);std::map<int,int> boundaryCount;
    int low=n.source.slr,high=low;
    for(const auto&s:n.sinks){low=std::min(low,s.slr);high=std::max(high,s.slr);
        check(p.paths.at(s.id).size()==size_t(std::abs(s.slr-n.source.slr)),"each sink crosses only necessary boundaries");}
    check(p.crossings.size()==size_t(high-low),"one shared crossing for each required boundary");
    for(const auto&e:p.crossings){const auto&c=m.geometry(e);check(++boundaryCount[std::min(c.from,c.to)]==1,"no duplicate crossing on the same boundary");}
}
int main(){try{
    for(int fanout:{2,8,64,4096}){
        auto m=inventory();Net n;n.id=1;n.source=pin(0,0,21,0);
        for(int i=1;i<=fanout;++i)n.sinks.push_back(pin(i,1,i%2?20:80,31+i%20));
        for(int i=1;i<=8;++i)n.sinks.push_back(pin(fanout+i,0,30+i,2));
        m.rebuild({n});verify(m,n);
        DensityModel d;d.index(m);const auto&t=m.geometry(m.plans[0].crossings[0]);
        auto assigned=d.assign(m,{{80,t.in.y}});
        m.plans[0].crossings[0].tile=assigned.tiles[0];m.previous[m.plans[0].crossings[0].key]=assigned.tiles[0];
        m.rebuild({n});verify(m,n);m.legalizeChannels();verify(m,n);
        check(m.plans[0].crossings[0].channel>=0,"exact lane assignment succeeds");
        std::cout<<"PASS fanout="<<fanout<<" local=8 shared_crossings=1 after refresh, QP projection and legalization\n";
    }
    auto m=inventory();Net n;n.id=2;n.source=pin(0,1,21,120);
    for(int slr=0;slr<4;++slr)for(int i=0;i<8;++i)n.sinks.push_back(pin(1+slr*8+i,slr,i%2?20:80,slr*100+5));
    m.rebuild({n});verify(m,n);check(m.plans[0].crossings.size()==3,"opposite directions and multi-SLR use one per boundary");
    m.legalizeChannels();verify(m,n);
    std::cout<<"PASS multi-SLR source=1 sinks=0,1,2,3 crossings=3 local_paths=empty\n";
    m.rebuild({});check(m.plans.empty(),"obsolete virtual crossings removed");
    std::cout<<"PASS no-split lifecycle\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
