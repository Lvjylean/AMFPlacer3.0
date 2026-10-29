#pragma once
#include "SllQuantityModel.h"
#include <unordered_map>

namespace amf_sll {
// Rule version 2: complete-round periodic annealing, independent of S.
// Pending tau changes apply at begin(), never between QPs in the same round.
class PeriodicTauSchedule {
    double current_=10;
    bool open_=false;
    int previousRound_=-1, completed_=0, held_=0, phaseRounds_=0;
    std::string phase_;
public:
    struct Decision {
        double tau=10, nextTau=10, median=0, entry=0, start=0;
        int completed=0, held=0, phaseRounds=0;
        bool hasMedian=false, hasStart=false, hasEntry=false;
        std::string reason="level_hold";
    };
    static double tauForRound(int round) {
        if(round<1)throw std::invalid_argument("Periodic tau round must be positive");
        if(round<=6)return 10;
        const int level=1+(round-7)/4;
        return level>=6 ? 0.2 : std::ldexp(10.0,-level);
    }
    static void validateNominalBudget(int iterations) {
        const long long n=iterations;
        // Existing ordinary budgets in AMFPlacer; independent SLR rounds are
        // deliberately not needed for satisfying this lower bound.
        if(n<=0 || n/3+3*(2*n/9)+n/2<30)
            throw std::invalid_argument("Periodic tau needs at least 30 nominal global rounds before packing");
    }
    double tau() const {return current_;}
    int completedRounds() const {return completed_;}
    bool readyForPacking() const {return !open_ && current_==0.2 && completed_>=30 && held_>=4;}
    double begin(const std::string&phase,int round,int iteration) {
        if(open_ || round<=previousRound_)throw std::logic_error("Duplicate/out-of-order SLL round begin");
        previousRound_=round;open_=true;
        const double next=tauForRound(completed_+1);
        if(next!=current_){current_=next;held_=0;}
        // This counter is diagnostic only. It cannot reset the annealing clock.
        if(phase!=phase_ || iteration==0){phase_=phase;phaseRounds_=0;}
        return current_;
    }
    Decision finish(std::int64_t s,bool valid) {
        if(!open_)throw std::logic_error("SLL round finish without begin");
        if(!valid || s<0)throw std::runtime_error("Invalid full-round SLL geometry during periodic tau schedule");
        open_=false;++completed_;++held_;++phaseRounds_;
        Decision d;d.tau=current_;d.nextTau=tauForRound(completed_+1);
        d.completed=completed_;d.held=held_;d.phaseRounds=phaseRounds_;
        d.reason=d.nextTau<current_?"periodic_drop":current_==0.2?"tau_floor":completed_<6?"initial_six_rounds":"level_hold";
        return d;
    }
};


} // namespace amf_sll
