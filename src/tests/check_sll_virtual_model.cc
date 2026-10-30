#include "../lib/HiFPlacer/placement/placementInfo/SllVirtualModel.h"
#include <iostream>
using namespace amf_sll_plan;
static void check(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
static Channel channel(int from,int to,int x,int lane=0) {
    Channel c;c.resource="b"+std::to_string(std::min(from,to))+"x"+std::to_string(x)+"l"+std::to_string(lane);
    c.segment=c.resource+"_in,"+c.resource+","+c.resource+"_out";
    c.from=from;c.to=to;c.column=x;c.in={double(x),double(from*100+(to>from?90:10))};c.out={double(x),double(to*100+(to>from?10:90))};
    c.slowMax=.77;c.slowMin=.2;c.fastMax=.5;c.fastMin=.15;return c;
}
static Pin pin(int id,int slr,double x){Pin p;p.id=id;p.unit=id;p.slr=slr;p.p={x,slr*100.+50};p.name="p"+std::to_string(id);p.movable=true;return p;}
int main(){try {
    Model m;m.allowSplit=false;m.local=[](Point a,Point b){return length(a,b,.4)*.01;};
    for(int b=0;b<3;++b)for(int x:{20,50,80})for(int lane=0;lane<2;++lane){m.channels.push_back(channel(b,b+1,x,lane));m.channels.push_back(channel(b+1,b,x,lane));}m.index();
    Net n;n.id=1;n.source=pin(0,1,80);n.sinks={pin(1,1,20),pin(2,1,50),pin(3,2,20),pin(4,2,50),pin(5,3,50),pin(6,3,80)};
    m.rebuild({n});check(m.plans.size()==1&&m.plans[0].crossings.size()==2,"share one crossing per boundary");
    const auto p=m.plans[0];check(p.paths.at(1).empty()&&p.paths.at(2).empty(),"local sinks must never cross");check(p.paths.at(3).size()==1&&p.paths.at(5).size()==2,"near/far ordered paths");
    auto first=m.channels[p.crossings[0].channel],second=m.channels[p.crossings[1].channel];
    double expected=m.local(n.source.p,first.in)+first.slowMax+m.local(first.out,second.in)+second.slowMax+m.local(second.out,n.sinks[4].p);
    check(std::abs(m.pathDelay(p,5,n.source.p,n.sinks[4].p)-expected)<1e-12,"paired endpoints and segment timing");
    auto moved=n.sinks[4].p;moved.x+=10;check(m.pathDelay(p,5,n.source.p,moved)!=expected,"receiver side movement affects timing");
    size_t trees=m.localTrees(p).size();check(trees==5,"three receiver trees plus two explicit crossing input legs");m.rebuild({n});check(m.created==0&&m.retained==2&&m.deleted==0,"stable object lifecycle");
    for(auto&s:n.sinks)s.slr=1;m.rebuild({n});check(m.plans.empty()&&m.deleted==2,"release resources when crossings vanish");
    n.sinks={pin(1,2,50)};m.rebuild({n});check(m.created==1,"crossing birth");
    auto n2=n;n2.id=2;n2.source.id=20;n2.sinks[0].id=21;m.rebuild({n,n2});check(m.plans.size()==2,"multiple nets allocated");std::set<std::string> used;for(auto&p2:m.plans)for(auto&e:p2.crossings)check(used.insert(m.channels[e.channel].resource).second,"capacity collision");
    Model one;one.channels={channel(0,1,20),channel(1,0,20)};one.index();one.allowSplit=false;
    auto up=n;up.source=pin(30,0,20);up.sinks={pin(31,1,20)};auto down=n2;down.source=pin(40,1,20);down.sinks={pin(41,0,20)};
    bool failed=false;try{one.rebuild({up,down});}catch(const std::runtime_error&){failed=true;}check(failed,"opposite directions share physical capacity");
    check(one.plans.empty(),"failed allocation does not commit partial plan");
    Model conflict;auto a=channel(0,1,20),b=channel(0,1,80);a.segment="shared_input,a";b.segment="shared_input,b";conflict.channels={a,b};conflict.index();conflict.allowSplit=false;
    failed=false;try{conflict.rebuild({up,Net{2,"",pin(50,0,80),{pin(51,1,80)},1}});}catch(const std::runtime_error&){failed=true;}check(failed,"ingress conflict is capacity, too");
    Model invalid;invalid.channels={channel(0,2,20)};failed=false;try{invalid.index();}catch(const std::invalid_argument&){failed=true;}check(failed,"nonadjacent channel rejected");
    // Translating both endpoints to the right changes the selected column;
    // there is no built-in preference for smaller physical COLUMN.
    Model x;x.channels={channel(0,1,20),channel(0,1,80)};x.index();x.allowSplit=false;x.local=m.local;
    up.source=pin(30,0,20);up.sinks={pin(31,1,20)};x.rebuild({up});check(x.channels[x.plans[0].crossings[0].channel].column==20,"left geometry");
    up.source.p.x=80;up.sinks[0].p.x=80;x.rebuild({up});check(x.channels[x.plans[0].crossings[0].channel].column==80,"right geometry");
    std::cout<<"PASS virtual SLL sharing, paired delay, lifecycle, capacity, conflicts, and column response\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
