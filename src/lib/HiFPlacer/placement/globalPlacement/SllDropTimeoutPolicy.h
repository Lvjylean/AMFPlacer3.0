#pragma once
#include "SllQuantityModel.h"
#include <array>
#include <unordered_map>

namespace amf_sll {
// Rule version 3: complete-round S decrease OR a bounded waiting time.
// This scheduler does not control global placement's natural exit condition.
// Pending changes apply only at begin(), never between the two QPs.
class DropTimeoutTauSchedule {
    double current_=10;
    bool open_=false;
    int previousRound_=-1, completed_=0, held_=0, phaseRounds_=0;
    int level_=0, pendingLevel_=0;
    bool hasReference_=false;
    std::int64_t reference_=0, initialReference_=0, pendingReference_=0;
    std::array<std::int64_t,3> initialSamples_{};
    std::string phase_;
    static double levelTau(int level) {
        return level==6 ? 0.2 : std::ldexp(10.0,-level);
    }
public:
    struct Decision {
        double tau=10, nextTau=10;
        int completed=0, held=0, phaseRounds=0;
        int minimumHold=0, maximumHold=0;
        bool hasReference=false, dropByS=false, dropByDeadline=false;
        std::int64_t referenceS=0, initialReferenceS=0, thresholdS=0;
        std::string reason="level_hold";
    };
    double tau() const {return current_;}
    int completedRounds() const {return completed_;}
    double begin(const std::string&phase,int round,int iteration) {
        if(open_ || round<1 || round<=previousRound_)throw std::logic_error("Duplicate/out-of-order SLL round begin");
        previousRound_=round;open_=true;
        if(pendingLevel_!=level_){
            level_=pendingLevel_;current_=levelTau(level_);held_=0;
            reference_=pendingReference_;hasReference_=true;
        }
        // Phase/local-iteration restarts do not reset waiting or references.
        if(phase!=phase_ || iteration==0){phase_=phase;phaseRounds_=0;}
        return current_;
    }
    Decision finish(std::int64_t s,bool valid) {
        if(!open_)throw std::logic_error("SLL round finish without begin");
        if(!valid || s<0)throw std::runtime_error("Invalid full-round SLL geometry during drop-or-timeout tau schedule");
        open_=false;++completed_;++held_;++phaseRounds_;
        if(completed_<=3){
            initialSamples_[completed_-1]=s;
            if(completed_==3){
                auto sorted=initialSamples_;std::sort(sorted.begin(),sorted.end());
                initialReference_=reference_=sorted[1];hasReference_=true;
            }
        }
        Decision d;d.tau=d.nextTau=current_;
        d.completed=completed_;d.held=held_;d.phaseRounds=phaseRounds_;
        d.hasReference=hasReference_;d.referenceS=reference_;d.initialReferenceS=initialReference_;
        // floor(0.9 * reference) using exact integers, without multiplication
        // overflow or a floating rounding error at the 10% boundary.
        d.thresholdS=reference_-reference_/10-(reference_%10!=0);
        d.minimumHold=level_==6?0:level_==0?6:1;
        d.maximumHold=level_==6?0:level_==0?14:level_==5?3:4;
        if(level_==6)d.reason="tau_floor";
        else if(!hasReference_)d.reason="initial_reference_unavailable";
        else if(held_<d.minimumHold)d.reason="minimum_level_hold";
        else {
            d.dropByS=reference_>0 && s<=d.thresholdS;
            d.dropByDeadline=held_>=d.maximumHold;
            if(d.dropByS || d.dropByDeadline){
                pendingLevel_=level_+1;pendingReference_=s;
                d.nextTau=levelTau(pendingLevel_);
                d.reason=d.dropByS?(d.dropByDeadline?"s_drop_and_maximum_wait":"s_drop_10_percent"):"maximum_wait";
            }
        }
        return d;
    }
};


} // namespace amf_sll
