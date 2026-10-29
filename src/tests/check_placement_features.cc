#include "PlacementFeatureConfig.h"
#include "SllTauPolicies.h"
#include <iostream>
static void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using namespace amf_config;
    for(int q=0;q<2;++q)for(int p=0;p<2;++p)for(int t=0;t<2;++t)for(int r=0;r<2;++r){
        FeatureMap c{{"SllQuantityEnable",q?"true":"false"},{"SllPlanningEnable",p?"true":"false"},
          {"SllTimingCorrectionEnable",t?"true":"false"},{"SllRouteConstraintEnable",r?"true":"false"}};
        bool valid=true;try{validatePlacementFeatures(c);}catch(const std::invalid_argument&){valid=false;}
        check(valid==bool(p||(!t&&!r)),"dependency truth table");
        check(sllRoute(c)==bool(p&&r)&&sllTiming(c)==bool(p&&t),"effective child switches");
    }
    for(std::string policy:{"adaptive","periodic","drop-or-timeout"}){
        amf_sll::ConfiguredTauSchedule merged;merged.configure(policy);
        amf_sll::TauSchedule a;amf_sll::PeriodicTauSchedule p;amf_sll::DropTimeoutTauSchedule d;
        for(int i=1;i<=60;++i){
            std::string phase=i<17?"first":"second";int iteration=i<17?i-1:i-17;
            auto actual=merged.begin(phase,i,iteration);
            auto expected=policy=="adaptive"?a.begin(phase,i,iteration):policy=="periodic"?p.begin(phase,i,iteration):d.begin(phase,i,iteration);
            check(actual==expected,"merged tau equals original strategy each round");
            int s=1000-12*i+(i%7==0?200:0); // Includes improvements, rebounds, phase restart and deadlines.
            auto result=merged.finish(s,true);double next;std::string reason;
            if(policy=="adaptive"){auto x=a.finish(s,true);next=x.nextTau;reason=x.reason;}
            else if(policy=="periodic"){auto x=p.finish(s,true);next=x.nextTau;reason=x.reason;}
            else {auto x=d.finish(s,true);next=x.nextTau;reason=x.reason;}
            check(result.nextTau==next && result.reason==reason,"scheduler decisions preserved");
            if(policy=="periodic")check(merged.readyForPacking()==(i>=30),"periodic minimum 30 complete rounds");
        }
    }
    std::cout<<"PASS feature dependency matrix and 180 original tau decisions\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
