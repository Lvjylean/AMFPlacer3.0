#pragma once
#include "SllQuantityModel.h"
#include <unordered_map>

namespace amf_sll {
// Rule version 1: decisions use complete GP rounds, never raw QP positions.
// A pending reduction takes effect at the next begin(), keeping both QPs equal.
class TauSchedule {
    double current_=10, next_=10, start_=0, entry_=0;
    bool haveStart_=false, haveEntry_=false, validPrevious_=false, open_=false;
    int previousRound_=-1, completed_=0, held_=0, phaseRounds_=0;
    std::string phase_;
    std::int64_t previousS_=0;
    std::vector<double> initial_, window_;
    static double median3(const std::vector<double>&v,size_t i) {
        double a=v.at(i),b=v.at(i+1),c=v.at(i+2);
        return std::max(std::min(a,b),std::min(std::max(a,b),c));
    }
public:
    struct Decision {
        double tau=10, nextTau=10, median=0, entry=0, start=0;
        int completed=0, held=0, phaseRounds=0;
        bool hasMedian=false, hasStart=false, hasEntry=false;
        std::string reason="hold";
    };
    double tau() const {return current_;}
    double begin(const std::string&phase,int round,int iteration) {
        if(open_ || round<=previousRound_)throw std::logic_error("Duplicate/out-of-order SLL round begin");
        previousRound_=round;open_=true;
        if(next_<current_){current_=next_;held_=0;window_.clear();}
        if(phase!=phase_ || iteration==0){phase_=phase;phaseRounds_=0;window_.clear();validPrevious_=false;}
        return current_;
    }
    Decision finish(std::int64_t s,bool valid) {
        if(!open_)throw std::logic_error("SLL round finish without begin");open_=false;
        ++completed_;++held_;++phaseRounds_;
        Decision d;d.tau=current_;d.nextTau=current_;d.completed=completed_;d.held=held_;d.phaseRounds=phaseRounds_;
        if(!valid || s<0){window_.clear();validPrevious_=false;d.reason="invalid_count";}
        else {
            // Only the first three complete rounds define the initial reference.
            // An invalid initial sample disables lowering rather than substituting a later one.
            if(completed_<=3)initial_.push_back(double(s));
            if(completed_==3 && initial_.size()==3){start_=median3(initial_,0);entry_=start_;haveStart_=haveEntry_=true;}
            window_.push_back(double(s));if(window_.size()>6)window_.erase(window_.begin());
            if(window_.size()>=3){d.hasMedian=true;d.median=median3(window_,window_.size()-3);}
            if(!haveStart_)d.reason="initial_reference_unavailable";
            else if(completed_<6)d.reason="initial_six_rounds";
            else if(held_<4)d.reason="minimum_level_hold";
            else if(phaseRounds_<2)d.reason="phase_settling";
            else if(validPrevious_ && s>previousS_)d.reason="last_round_rebound";
            else if(current_<=0.2)d.reason="tau_floor";
            else if(!d.hasMedian)d.reason="need_three_comparable_rounds";
            else {
                bool improvement=entry_>0 && d.median<=0.9*entry_;
                bool plateau=false;
                if(window_.size()==6){double old=median3(window_,0);double gain=old>0?(old-d.median)/old:-1;
                    plateau=gain>=0 && gain<0.01 && start_>0 && d.median<=0.9*start_;}
                if(improvement || plateau){next_=std::max(0.2,current_*0.5);d.nextTau=next_;
                    d.reason=improvement?"level_drop_10_percent":"plateau_after_initial_drop";}
                else d.reason="no_trigger";
            }
            previousS_=s;validPrevious_=true;
        }
        d.hasStart=haveStart_;d.hasEntry=haveEntry_;d.start=start_;d.entry=entry_;
        if(d.nextTau<current_){entry_=d.median;haveEntry_=true;}
        return d;
    }
};

class ShortSpanPolicy {
    // Stable design-net ID -> physical boundary at entry. PU indices may change
    // on remapping. Retention uses this SAME boundary, independently of tau.
    std::unordered_map<unsigned,double> excluded_;
public:
    struct Update {
        size_t excluded=0,entered=0,resumed=0;
        size_t excludedSameDie=0,excludedCrossDie=0;
        std::int64_t excludedS=0;
        double seconds=0;
    };
    bool excluded(unsigned id)const{return excluded_.count(id)!=0;}
    Update update(Model&m,const std::vector<NetGeometry>&geometry,bool enabled) {
        if(geometry.size()!=m.nets.size())throw std::invalid_argument("SLL short-span geometry mismatch");
        const auto started=std::chrono::steady_clock::now();Update out;
        {std::unordered_map<unsigned,double> next;
        for(size_t i=0;i<m.nets.size();++i){auto&n=m.nets[i];const auto&g=geometry[i];bool old=excluded(n.id);
            bool off=false;double boundary=0;
            const bool eligible=enabled && n.optimize && n.weight>0 && g.crossings>=0 &&
                std::isfinite(g.minimumY) && std::isfinite(g.maximumY) &&
                std::isfinite(g.span) && g.span>=0 && g.minimumY<=g.maximumY;
            auto radius=[&](double b){return std::max(std::abs(g.minimumY-b),std::abs(g.maximumY-b));};
            if(eligible){
                if(old){boundary=excluded_.at(n.id);
                    off=g.span<3.0 && radius(boundary)<3.0;
                }
                // Pin extrema suffice: ALL pins must be near ONE boundary.
                // No discrete crossing-count filter. Fresh entry is inclusive.
                if(!off && g.span<=2.0){
                    double nearest=INFINITY;
                    for(double b:m.boundaries){double r=radius(b);
                        if(r<nearest){nearest=r;boundary=b;}}
                    off=nearest<=2.0;
                }
            }
            n.gradientActive=!off;
            if(off){next.emplace(n.id,boundary);++out.excluded;out.excludedS+=g.crossings;
                if(g.crossings==0)++out.excludedSameDie;else ++out.excludedCrossDie;
            }
            out.entered+=off&&!old;out.resumed+=old&&!off;
        }
        // Replace state so disappeared/filtered nets cannot leave stale exclusions.
        excluded_.swap(next); } // Include destruction of the previous set in the update timer.
        out.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return out;
    }
};
} // namespace amf_sll
