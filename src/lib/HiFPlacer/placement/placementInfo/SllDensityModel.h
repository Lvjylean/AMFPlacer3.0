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
        std::vector<int> tiles;
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
        std::vector<int> v;for(const auto&p:m.plans)for(const auto&e:p.crossings)v.push_back(e.tile);return v;
    }
    // The incumbent assignment is feasible. One-coordinate updates always
    // retain it as a fallback, so projection cannot destroy feasibility merely
    // because a greedy full reassignment picked channels in a bad order.
    Assignment assign(const Model&m,const std::vector<Point>&desired)const {
        if(!m.jointPlacement)throw std::invalid_argument("Tile density requires joint placement");
        Assignment result;result.tiles=current(m);
        if(desired.size()!=result.tiles.size())throw std::invalid_argument("SLL proposal dimension mismatch");
        std::map<Bin,size_t> loads;std::map<TileKey,size_t> tileLoads;
        for(int ti:result.tiles){const auto&t=m.tiles.at(ti);++loads[bin(t)];
            if(++tileLoads[t.key]>m.tileCapacity.at(t.key))throw std::runtime_error("Overfull incumbent SLL tile");}
        result.energyBefore=energy(loads);
        auto plans=m.plans;size_t i=0;
        for(auto&plan:plans)for(auto&e:plan.crossings)e.channel=-1;
        for(auto&plan:plans)for(auto&e:plan.crossings){
            if(!std::isfinite(desired[i].x)||!std::isfinite(desired[i].y))throw std::runtime_error("Non-finite virtual SLL QP proposal");
            const int old=e.tile;const auto&base=m.tiles[old];
            --loads[bin(base)];--tileLoads[base.key];
            auto score=[&](int ti){const auto&t=m.tiles[ti];const auto b=bin(t);const size_t n=loads[b];
                e.tile=ti;
                return m.cost(plan)+weight*(penalty(b,n+1)-penalty(b,n));};
            int selected=old;double best=score(old);
            // Evaluate complete same-die trees and path delays at every tile.
            // QP proposal displacement is diagnostic only, never a physical cost.
            for(int ti:m.availableTiles.at({base.from,base.to})){
                const auto&t=m.tiles[ti];if(tileLoads[t.key]>=m.tileCapacity.at(t.key))continue;
                const double value=score(ti);
                if(value<best-1e-9){best=value;selected=ti;}
            }
            if(!std::isfinite(best))throw std::runtime_error("No legal SLL tile assignment");
            e.tile=selected;result.tiles[i++]=selected;result.moved+=selected!=old;
            const auto&t=m.tiles[selected];++tileLoads[t.key];++loads[bin(t)];
        }
        result.energyAfter=energy(loads);return result;
    }
};
}
