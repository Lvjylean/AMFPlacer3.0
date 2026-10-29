#pragma once
#include "SllAdaptivePolicy.h"
#include "SllPeriodicPolicy.h"
#include "SllDropTimeoutPolicy.h"
namespace amf_sll {
class ConfiguredTauSchedule {
    TauSchedule adaptive; PeriodicTauSchedule periodic; DropTimeoutTauSchedule drop;
    std::string policy="adaptive";
public:
    using Decision=TauSchedule::Decision;
    void configure(const std::string &value) {
        if(value!="adaptive" && value!="periodic" && value!="drop-or-timeout") throw std::invalid_argument("Invalid SllTauPolicy");
        policy=value;
    }
    bool readyForPacking()const{return policy!="periodic" || periodic.readyForPacking();}
    double begin(const std::string&p,int r,int i) {
        return policy=="periodic"?periodic.begin(p,r,i):policy=="drop-or-timeout"?drop.begin(p,r,i):adaptive.begin(p,r,i);
    }
    Decision finish(std::int64_t s,bool valid) {
        if(policy=="adaptive")return adaptive.finish(s,valid);
        Decision d;
        if(policy=="periodic") {
            auto v=periodic.finish(s,valid);d.tau=v.tau;d.nextTau=v.nextTau;d.completed=v.completed;
            d.held=v.held;d.phaseRounds=v.phaseRounds;d.reason=v.reason;
        } else {
            auto v=drop.finish(s,valid);d.tau=v.tau;d.nextTau=v.nextTau;d.completed=v.completed;
            d.held=v.held;d.phaseRounds=v.phaseRounds;d.reason=v.reason;
            d.hasEntry=d.hasStart=v.hasReference;d.entry=v.referenceS;d.start=v.initialReferenceS;
        }
        return d;
    }
};
}
