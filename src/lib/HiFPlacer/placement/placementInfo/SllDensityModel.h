#pragma once
#include "SllVirtualModel.h"

namespace amf_sll_plan {
// SLL density has its own supply. A paired crossing consumes ONE canonical
// channel, including when many leaves share it. Opposite directions compete.
// Bins contain an unordered boundary, physical column, and lower-side row band.
class DensityModel {
public:
    using Bin=std::tuple<int,int,int>;
    // One tile row by default; paired directions share the same lower-side bin.
    double rowHeight=1,targetUtilization=.8,weight=256;
    std::map<Bin,size_t> capacity;
    std::vector<Bin> channelBins;
    struct Assignment {
        std::vector<int> channels;
        double energyBefore=0,energyAfter=0;
        size_t moved=0;
    };
    Bin bin(const Channel&c)const {
        return {std::min(c.from,c.to),c.column,int(std::floor(std::min(c.in.y,c.out.y)/rowHeight))};
    }
    void index(const Model&m) {
        if(!(rowHeight>0)||!std::isfinite(rowHeight)||!(targetUtilization>0&&targetUtilization<=1)||
           !(weight>=0)||!std::isfinite(weight))throw std::invalid_argument("Invalid SLL density parameters");
        capacity.clear();channelBins.clear();std::map<Bin,std::set<std::string>> resources;
        std::map<std::string,Bin> identities;
        for(const auto&c:m.channels){
            Bin b=bin(c);channelBins.push_back(b);
            auto inserted=identities.emplace(c.resource,b);
            if(!inserted.second && inserted.first->second!=b)throw std::invalid_argument("Opposite SLL directions disagree on density bin");
            if(!Model::conflicts(c,m.reserved))resources[b].insert(c.resource);
        }
        for(const auto&b:resources)capacity[b.first]=b.second.size();
    }
    double penalty(const Bin&b,size_t count)const {
        auto it=capacity.find(b);if(it==capacity.end()||!it->second)return count?INFINITY:0;
        const double excess=std::max(0.,double(count)-targetUtilization*it->second);
        return excess*excess/it->second;
    }
    double energy(const std::map<Bin,size_t>&loads)const {
        double e=0;for(const auto&v:loads)e+=penalty(v.first,v.second);return e;
    }
    std::vector<int> current(const Model&m)const {
        std::vector<int> v;for(const auto&p:m.plans)for(const auto&e:p.crossings)v.push_back(e.channel);return v;
    }
    // The incumbent assignment is feasible. One-coordinate updates always
    // retain it as a fallback, so projection cannot destroy feasibility merely
    // because a greedy full reassignment picked channels in a bad order.
    Assignment assign(const Model&m,const std::vector<Point>&desired)const {
        Assignment result;result.channels=current(m);
        if(desired.size()!=result.channels.size())throw std::invalid_argument("SLL proposal dimension mismatch");
        std::set<std::string> used=m.reserved;std::map<Bin,size_t> loads;
        for(int ci:result.channels){
            if(Model::conflicts(m.channels.at(ci),used))throw std::runtime_error("Conflicting incumbent SLL assignment");
            Model::occupy(m.channels[ci],used);++loads[channelBins.at(ci)];
        }
        result.energyBefore=energy(loads);
        for(size_t i=0;i<desired.size();++i){
            if(!std::isfinite(desired[i].x)||!std::isfinite(desired[i].y))throw std::runtime_error("Non-finite virtual SLL QP proposal");
            const int old=result.channels[i];const auto&base=m.channels[old];
            for(const auto&r:Model::footprint(base))used.erase(r);
            --loads[channelBins[old]];
            auto distance=[&](const Channel&c){double dx=desired[i].x-c.in.x,dy=desired[i].y-c.in.y;return dx*dx+m.ratio*dy*dy;};
            auto candidates=m.candidates(base.from,base.to,used,distance,old);
            int selected=old;double best=INFINITY;
            for(int ci:candidates){
                const auto&b=channelBins[ci];const size_t n=loads[b];
                const double score=distance(m.channels[ci])+weight*(penalty(b,n+1)-penalty(b,n))+
                                   (ci==old?0:m.switchCost);
                if(score<best){best=score;selected=ci;}
            }
            if(!std::isfinite(best))throw std::runtime_error("No legal SLL density projection");
            result.channels[i]=selected;result.moved+=selected!=old;
            Model::occupy(m.channels[selected],used);++loads[channelBins[selected]];
        }
        result.energyAfter=energy(loads);return result;
    }
};
}
