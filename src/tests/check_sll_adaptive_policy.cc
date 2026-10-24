#include "../lib/HiFPlacer/placement/globalPlacement/SllAdaptivePolicy.h"
#include <iostream>
#include <random>
using namespace amf_sll;
int main(){try{
    size_t checks=0;auto check=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
    TauSchedule schedule;TauSchedule::Decision d;
    for(int k=1;k<=6;++k){check(schedule.begin("hpio",k,k-1)==10,"Initial six rounds tau");
        d=schedule.finish(k<=3?1000:850,true);check(schedule.tau()==10,"finish must not change current tau");}
    check(d.nextTau==5 && d.reason=="level_drop_10_percent","10 percent trigger");
    for(int k=7;k<=10;++k){check(schedule.begin("hpio",k,k-1)==5,"pending decrease applied once");
        d=schedule.finish(700,true);if(k<10)check(d.nextTau==5,"four complete rounds minimum");}
    check(d.nextTau==2.5,"next level drop");
    // Stagnation after an initial reduction can lower tau, but stagnation at the
    // original high S cannot. A rebound in the last sample blocks both triggers.
    TauSchedule plateau;
    for(int k=1;k<=12;++k){plateau.begin("hpio",k,k-1);d=plateau.finish(k<=3?1000:850,true);}
    check(d.nextTau==2.5&&d.reason=="plateau_after_initial_drop","six-sample plateau trigger");
    TauSchedule stuck;for(int k=1;k<=20;++k){stuck.begin("hpio",k,k-1);d=stuck.finish(1000,true);}
    check(stuck.tau()==10&&d.nextTau==10,"plateau without initial improvement must hold");
    TauSchedule rebound;for(int k=1;k<=6;++k){rebound.begin("hpio",k,k-1);d=rebound.finish(k<=3?1000:k==6?850:800,true);}
    check(d.nextTau==10&&d.reason=="last_round_rebound","last-round rebound blocks decrease");
    rebound.begin("slr",7,0);d=rebound.finish(700,true);check(d.nextTau==10,"new phase settling");
    rebound.begin("slr",8,1);d=rebound.finish(700,true);check(d.nextTau==10,"do not mix phase medians");
    rebound.begin("slr",9,2);d=rebound.finish(700,true);check(d.nextTau==5,"new phase comparable median");
    rebound.begin("slr",10,0);d=rebound.finish(500,true);check(d.phaseRounds==1,"same-name phase restart");
    TauSchedule invalid;for(int k=1;k<=12;++k){invalid.begin("hpio",k,k-1);d=invalid.finish(k<4?1000:100,k!=2);}
    check(d.nextTau==10,"invalid initial reference cannot silently substitute later samples");
    TauSchedule floor;for(int k=1;k<=50;++k){floor.begin("hpio",k,k-1);d=floor.finish(k<=3?1000:1,true);check(d.nextTau>=.2,"tau floor");}
    check(floor.tau()==.2,"halving reaches 0.2 without undershoot");
    TauSchedule zero;for(int k=1;k<=12;++k){zero.begin("hpio",k,k-1);d=zero.finish(0,true);}check(d.nextTau==10,"zero S no percentage division");
    bool threw=false;try{zero.finish(0,true);}catch(const std::logic_error&){threw=true;}check(threw,"duplicate finish rejected");

    Model m;m.units=4;m.boundaries={100,200,300};m.tau={10,10,10};m.gamma=.02;m.minY=0;m.maxY=400;
    // Net 10 uses macro/pin offsets: actual Y=99/101 although anchors=94/106.
    m.pins={{0,5},{1,-5},{0,0},{2,0},{2,0},{3,0}};
    m.nets={{0,2,10},{2,4,20},{4,6,30}};m.validate();
    std::vector<double> y={94,106,250,350};std::vector<unsigned char> movable={1,1,1,0};
    ShortSpanPolicy policy;std::vector<NetGeometry> geometry;
    auto full=m.evaluate(y,movable,1,true);auto counted=m.countDiscrete(y,4,&geometry);
    check(geometry[0].span==2&&geometry[0].crossings==1,"physical pin span and boundary count");
    auto u=policy.update(m,geometry,true);check(u.excluded==1&&u.entered==1&&u.excludedS==1,"entry inclusive 2");
    auto active=m.evaluate(y,movable,4,true,nullptr,&counted);
    check(full.smooth==active.smooth&&full.discrete==active.discrete&&full.seams==active.seams,"excluded net retained in full statistics");
    check(active.activeSmooth<active.smooth&&active.gradient[1]==0&&active.gradient[0]!=0,"only this net gradient removed; other net still pulls shared PU");
    for(int workers:{1,2,8,16}){auto r=m.evaluate(y,movable,workers,true);check(r.gradient==active.gradient&&r.smooth==full.smooth,"masked parallel determinism");}
    // Active-cost derivative with the same frozen mask, not derivative of the full F.
    for(size_t i=0;i<3;++i){auto yp=y,ym=y;yp[i]+=1e-4;ym[i]-=1e-4;
        double numeric=(m.evaluate(yp,movable,1,false).activeSmooth-m.evaluate(ym,movable,1,false).activeSmooth)/2e-4;
        check(std::abs(numeric-active.gradient[i])<1e-8,"masked finite difference");}
    y[1]=106.5;counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);check(u.excluded==1&&!u.entered&&!u.resumed,"hysteresis 2.5");
    // PU index remap retains identity through stable net ID, including hysteresis.
    std::swap(y[0],y[3]);for(auto&p:m.pins){if(p.unit==0)p.unit=3;else if(p.unit==3)p.unit=0;}
    counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);check(u.excluded==1,"PU remap retains net exclusion");
    y[1]=107;counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);check(!u.excluded&&u.resumed==1,"resume inclusive 3");
    y[1]=106.5;counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);check(!u.excluded,"2.5 cannot enter without history");
    y[1]=106;counted=m.countDiscrete(y,1,&geometry);policy.update(m,geometry,true);
    y[1]=104.5;counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);
    check(u.excluded==1&&u.excludedSameDie==1&&!u.excludedCrossDie&&!u.resumed,"cross to same die retains exclusion");
    y[1]=106;counted=m.countDiscrete(y,1,&geometry);policy.update(m,geometry,true);
    y[1]=450;counted=m.countDiscrete(y,1,&geometry);u=policy.update(m,geometry,true);check(!u.excluded&&counted.invalidNets==1,"invalid geometry not hidden by mask");
    y[1]=106;counted=m.countDiscrete(y,1,&geometry);policy.update(m,geometry,true);u=policy.update(m,geometry,false);
    check(!u.excluded&&m.nets[0].gradientActive,"disabled policy restores full gradient");
    check(m.slr(100)==0&&m.slr(100.0001)==1,"exact seam convention unchanged");
    check(counted.seconds>=0&&u.seconds>=0&&active.smoothGradientSeconds>=0,"separate finite clocks");

    // Inclusive entry, exclusive retention, both sides, all physical pins.
    Model compact;compact.units=3;compact.boundaries={100,200};compact.tau={10,10};
    compact.gamma=.02;compact.minY=0;compact.maxY=300;
    compact.pins={{0,0},{1,0},{2,0}};compact.nets={{0,3,123}};compact.validate();
    ShortSpanPolicy edge;std::vector<unsigned char> allMovable={1,1,1};
    auto update=[&](std::vector<double> v,bool enabled=true){
        auto fullBefore=compact.evaluate(v,allMovable,1,true);
        compact.countDiscrete(v,4,&geometry);auto result=edge.update(compact,geometry,enabled);
        auto fullAfter=compact.evaluate(v,allMovable,4,true);
        check(fullBefore.discrete==fullAfter.discrete&&fullBefore.smooth==fullAfter.smooth,"policy does not change full S/F");
        check(result.excluded==result.excludedSameDie+result.excludedCrossDie,"same/cross counts close");
        if(result.excluded)check(fullAfter.activeSmooth==0&&fullAfter.gradient==std::vector<double>(3,0),"all excluded-net contributions zero");
        return result;
    };
    check(update({98,99,100}).excludedSameDie==1,"lower same-die entry at radius/span 2");
    check(update({99,100,101}).excludedCrossDie==1,"same to cross stays excluded");
    check(update({100.25,101,102.5}).excludedSameDie==1,"radius/span below 3 retain across side change");
    check(update({100.25,101,103}).resumed==1,"farthest distance exactly 3 restores");
    check(!update({100.25,101,102.5}).excluded,"radius above 2 cannot freshly enter");
    check(update({100.25,101,102}).entered==1,"upper same-die radius 2 enters");
    check(update({98.5,100,101.5}).resumed==1,"span exactly 3 restores despite radius below 2");
    check(!update({98.75,100,101.25}).excluded,"span above 2 cannot freshly enter");
    check(update({99,100,101}).entered==1,"crossing entry");
    check(update({99,100,200}).resumed==1,"third pin cannot be ignored or use a different boundary");
    check(!update({150,150.5,151}).excluded,"short net far from seams stays active");
    check(update({199,200,201}).entered==1,"second physical boundary entry");
    check(update({97.5,98,99}).resumed==1,"old boundary is not silently replaced for retention");
    check(update({99,100,101}).entered==1,"fresh entry near different boundary allowed");
    check(!update({99,100,301}).excluded,"out of domain restores active objective");
    check(update({99,100,101}).entered==1,"valid geometry re-enters");
    compact.nets[0].weight=0;check(update({99,100,101}).resumed==1,"zero weight cannot retain");
    compact.nets[0].weight=1;compact.nets[0].optimize=false;
    check(!update({99,100,101}).excluded,"report-only net stays outside optimization mask");
    compact.nets[0].optimize=true;check(update({99,100,101}).entered==1,"optimizable again");
    check(update({99,100,101},false).resumed==1,"outside-global clears policy state");
    for(double tau:{10.,5.,.2,.1}){compact.tau={tau,tau};
        check(update({99,100,101}).excluded==1,"physical predicate independent of tau");}
    compact.countDiscrete({99,100,101},1,&geometry);
    compact.nets.clear();compact.pins.clear();edge.update(compact,{},true);
    compact.nets={{0,3,123}};compact.pins={{0,0},{1,0},{2,0}};
    check(!update({98.75,100,101.25}).excluded,"disappeared net cannot retain stale hysteresis");
    // Independent all-pin reference over a deterministic moving sequence. It
    // does not use NetGeometry or the production extrema/radius predicate.
    std::mt19937 random(20261024);std::uniform_real_distribution<double> offset(-4,4);
    bool referenceOff=false;double referenceBoundary=0;
    for(int k=0;k<1000;++k){double b=k%11==0?200:100;std::vector<double> v={b+offset(random),b+offset(random),b+offset(random)};
        if(k%7==0)v[1]=v[2]=v[0]+.5;
        bool enabled=k%29!=0;double span=*std::max_element(v.begin(),v.end())-*std::min_element(v.begin(),v.end());
        auto allNear=[&](double seam,double distance,bool inclusive){for(double py:v)
            if(inclusive?std::abs(py-seam)>distance:std::abs(py-seam)>=distance)return false;return true;};
        bool next=enabled&&referenceOff&&span<3&&allNear(referenceBoundary,3,false);
        if(enabled&&!next&&span<=2)for(double seam:compact.boundaries)if(allNear(seam,2,true)){next=true;referenceBoundary=seam;break;}
        auto actual=update(v,enabled);check(bool(actual.excluded)==next,"all-pin reference agreement");
        check(actual.entered==size_t(next&&!referenceOff)&&actual.resumed==size_t(referenceOff&&!next),"transition counts match reference");
        referenceOff=next;
    }
    std::cout<<"PASS "<<checks<<" adaptive policy checks; synthetic geometry only\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
