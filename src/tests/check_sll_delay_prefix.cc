#include "../lib/HiFPlacer/placement/placementInfo/SllVirtualModel.h"
#include <cstring>
#include <iostream>
#include <random>
using namespace amf_sll_plan;
static void check(bool ok,const char*msg){if(!ok)throw std::runtime_error(msg);}
static bool identical(double a,double b){return std::memcmp(&a,&b,sizeof(a))==0;}
int main(){try {
    Model m;size_t calls=0;
    m.local=[&](Point a,Point b){++calls;double x=std::abs(a.x-b.x),y=std::abs(a.y-b.y);
        return .05+.003*std::pow(x,.3)+.007*std::pow(y,.5)+.01*(x+.4*y);};
    Plan p;p.net.source.p={12.25,20.5};
    for(int k=0;k<4;++k){Channel c;c.in={23.1+10*k,80.+k*100};c.out={c.in.x,100.+k*100};c.slowMax=.731+0.017*k;
        m.channels.push_back(c);Crossing e;e.channel=k;e.parent=(k==0||k==3)?-1:k-1;p.crossings.push_back(e);}
    std::mt19937 rng(37);
    for(int i=0;i<2000;++i){Pin s;s.id=i;s.p={double(rng()%2000)/7,double(rng()%3000)/7};s.timingBudget=double(rng()%150)/20;
        p.net.sinks.push_back(s);p.paths[i]=i%5==0?std::vector<int>{}:i%5==1?std::vector<int>{0}:
            i%5==2?std::vector<int>{0,1}:i%5==3?std::vector<int>{0,1,2}:std::vector<int>{3};}
    DelayPrefix prefix;size_t checked=0;
    for(bool tail:{false,true})for(int epoch=0;epoch<8;++epoch){
        m.outputLongDelayCorrection=tail;m.channels[0].in.x+=.125;p.net.source.p.x+=.0625;
        m.prepareDelayPrefix(p,p.net.source.p,prefix);
        for(auto&s:p.net.sinks){for(double delta:{0.,.125}){
            Point source=p.net.source.p;source.x+=delta;
            const double expected=m.pathDelay(p,s.id,source,s.p);
            check(identical(expected,m.pathDelayWithPrefix(p,s.id,source,s.p,prefix)),"bitwise path delay/fallback mismatch");++checked;}}
        std::vector<double> expected;
        for(auto&s:p.net.sinks){const double d=m.pathDelay(p,s.id,p.net.source.p,s.p);
            expected.push_back(std::min(64.,1.+std::max(0.,d-s.timingBudget)));}
        m.updatePlannedCriticalities(p);
        for(size_t i=0;i<expected.size();++i)check(identical(expected[i],p.net.sinks[i].criticality),"criticality changed");
    }
    // Valid historical hand-built paths need not have parent metadata.
    p.crossings[1].parent=-1;m.prepareDelayPrefix(p,p.net.source.p,prefix);
    check(identical(m.pathDelay(p,2,p.net.source.p,p.net.sinks[2].p),
        m.pathDelayWithPrefix(p,2,p.net.source.p,p.net.sinks[2].p,prefix)),"non-tree fallback changed");
    Plan fanout;fanout.net.source.p={5,10};fanout.crossings={p.crossings[0]};
    for(int i=0;i<1000;++i){Pin s;s.id=i;s.p={30.+i/7.,200};fanout.net.sinks.push_back(s);fanout.paths[i]={0};}
    calls=0;for(const auto&s:fanout.net.sinks)m.pathDelay(fanout,s.id,fanout.net.source.p,s.p);const size_t oldCalls=calls;
    calls=0;m.prepareDelayPrefix(fanout,fanout.net.source.p,prefix);
    for(const auto&s:fanout.net.sinks)m.pathDelayWithPrefix(fanout,s.id,fanout.net.source.p,s.p,prefix);
    check(oldCalls==2000&&calls==1001,"shared input leg not reused");
    std::cout<<"PASS "<<checked<<" bitwise queries; multi-boundary/branch/local/candidate fallback, criticality, geometry refresh; local calls "<<oldCalls<<" -> "<<calls<<"\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
