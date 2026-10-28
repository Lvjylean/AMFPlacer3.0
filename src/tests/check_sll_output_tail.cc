#include "../lib/HiFPlacer/placement/placementInfo/SllVirtualModel.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace amf_sll_plan;
static void near(double got,double want,const char*what){if(std::abs(got-want)>1e-10)throw std::runtime_error(what);}
int main(){try{
    Model m;m.local=[](Point a,Point b){return std::abs(a.x-b.x);};
    Channel c;c.from=0;c.to=1;c.in={2,0};c.out={10,0};c.slowMax=.77;m.channels.push_back(c);
    Channel c2;c2.from=1;c2.to=2;c2.in={13,0};c2.out={20,0};c2.slowMax=.75;m.channels.push_back(c2);
    Plan p;Crossing one;one.channel=0;p.crossings.push_back(one);Crossing two;two.channel=1;p.crossings.push_back(two);
    p.paths[1]={0};p.paths[2]={};p.paths[3]={0,1};
    const double a=1.1430759607609453;
    near(m.pathDelay(p,1,{0,0},{13,0}),5.77,"disabled baseline");
    m.outputLongDelayCorrection=true;m.outputLongDelayThresholdNs=1.5;m.outputLongDelaySlope=a;
    near(m.pathDelay(p,2,{0,0},{5,0}),5,"local sink must remain unchanged");
    for(double length:{0.,.2,1.,1.5})
        near(m.pathDelay(p,1,{0,0},{10+length,0}),2+.77+length,"short/medium SLL exit changed");
    near(m.pathDelay(p,1,{0,0},{13,0}),5.77+a*1.5,"only long output leg is corrected");
    near(m.pathDelay(p,3,{0,0},{23,0}),2+.77+3+.75+3+a*1.5,"transit leg must not be corrected or double counted");
    // The same endpoint helper is used by the geometry fallback.
    near(m.outputDelay({10,0},{13,0}),3+a*1.5,"fallback helper");
    near(m.outputDelay({10,0},{11.5,0}),1.5,"threshold continuity");
    near(m.outputDelay({10,0},{11.5+1e-7,0}),1.5+(1+a)*1e-7,"right-hand continuity");
    near(m.delay({0,0},{4,0}),4,"original local model was modified");
    p.net.source.p={0,0};Pin sink;sink.id=1;sink.p={13,0};sink.timingBudget=6;p.net.sinks={sink};
    m.updatePlannedCriticalities(p);
    near(p.net.sinks[0].criticality,1+5.77+a*1.5-6,"criticality must consume corrected planned delay");
    double previous=0;
    for(int i=0;i<=500;++i){double value=m.outputDelay({0,0},{i*.01,0});if(value<previous)throw std::runtime_error("nonmonotonic correction");previous=value;}
    m.outputLongDelaySlope=0;near(m.pathDelay(p,1,{0,0},{13,0}),5.77,"zero slope baseline");
    std::cout<<"PASS: local/short preservation, source and SLL unchanged, long output correction, multi-boundary transit, continuity, monotonicity, criticality\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
