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
    Assignment spread(const Model&m,const std::vector<Point>&desired)const {
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
                return length(desired[i],t.in,m.ratio)+weight*(penalty(b,n+1)-penalty(b,n));};
            int selected=old;double best=score(old);
            // Before QP, find a nearby density target. This is a soft anchor,
            // not a replacement optimization of the net's wirelength/timing.
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
    // After QP, legalize the solved coordinates using displacement ONLY. Soft
    // density pressure has already entered QP through spread()/the anchor.
    // Assign from scratch so full tiles can exchange occupants in one round.
    // This is deterministic greedy legalization with augmenting-path repair,
    // not a claim of globally minimum total assignment displacement.
    Assignment assign(const Model&m,const std::vector<Point>&desired)const {
        if(!m.jointPlacement)throw std::invalid_argument("Tile projection requires joint placement");
        const auto old=current(m);const size_t count=old.size();
        if(desired.size()!=count)throw std::invalid_argument("SLL proposal dimension mismatch");
        std::map<Bin,size_t> before;std::map<TileKey,size_t> initialLoads;
        for(size_t i=0;i<count;++i){
            if(!std::isfinite(desired[i].x)||!std::isfinite(desired[i].y))
                throw std::runtime_error("Non-finite virtual SLL QP proposal");
            const auto&t=m.tiles.at(old[i]);++before[bin(t)];
            if(++initialLoads[t.key]>m.tileCapacity.at(t.key))
                throw std::runtime_error("Overfull incumbent SLL tile");
        }
        Assignment result;result.tiles.assign(count,-1);result.energyBefore=energy(before);
        std::map<TileKey,std::vector<size_t>> occupants;
        auto candidates=[&](size_t i)->const std::vector<int>&{
            const auto&t=m.tiles[old[i]];return m.availableTiles.at({t.from,t.to});};
        auto distance=[&](size_t i,int ti){return length(desired[i],m.tiles[ti].in,m.ratio);};
        auto prefer=[&](size_t i,int a,int b){
            if(b<0)return true;
            const double da=distance(i,a),db=distance(i,b);
            if(da<db-1e-9)return true;
            if(db<da-1e-9)return false;
            if((a==old[i])!=(b==old[i]))return a==old[i];
            const auto&ta=m.tiles[a];const auto&tb=m.tiles[b];
            const double ua=double(occupants[ta.key].size()+1)/m.tileCapacity.at(ta.key);
            const double ub=double(occupants[tb.key].size()+1)/m.tileCapacity.at(tb.key);
            return std::make_tuple(ua,ta.column,ta.in.x,ta.in.y,ta.out.x,ta.out.y)<
                   std::make_tuple(ub,tb.column,tb.in.x,tb.in.y,tb.out.x,tb.out.y);
        };
        auto move=[&](size_t i,int ti){
            if(result.tiles[i]>=0){
                auto&v=occupants[m.tiles[result.tiles[i]].key];
                v.erase(std::find(v.begin(),v.end(),i));
            }
            result.tiles[i]=ti;occupants[m.tiles[ti].key].push_back(i);
        };
        for(size_t i=0;i<count;++i){
            int chosen=-1;
            for(int ti:candidates(i)){
                const auto&t=m.tiles[ti];
                if(occupants[t.key].size()<m.tileCapacity.at(t.key)&&prefer(i,ti,chosen))chosen=ti;
            }
            if(chosen>=0){move(i,chosen);continue;}
            // Rare asymmetric candidate domains can strand a greedy assignment.
            // Reassign an alternating path instead of violating capacity or
            // restoring a separate wire/timing-based tile decision.
            std::vector<int> parent(count,-1),via(count,-1);
            std::vector<size_t> queue{i};parent[i]=int(i);std::set<TileKey> visited;
            bool repaired=false;
            for(size_t head=0;head<queue.size()&&!repaired;++head){
                const size_t cur=queue[head];auto ranked=candidates(cur);
                std::stable_sort(ranked.begin(),ranked.end(),[&](int a,int b){
                    const auto&ta=m.tiles[a];const auto&tb=m.tiles[b];
                    return std::make_tuple(distance(cur,a),a!=old[cur],ta.column,ta.in.y,ta.in.x)<
                           std::make_tuple(distance(cur,b),b!=old[cur],tb.column,tb.in.y,tb.in.x);
                });
                for(int ti:ranked){
                    const auto&t=m.tiles[ti];if(!visited.insert(t.key).second)continue;
                    if(occupants[t.key].size()<m.tileCapacity.at(t.key)){
                        size_t node=cur;int target=ti;
                        while(true){
                            move(node,target);if(node==i)break;
                            target=via[node];node=size_t(parent[node]);
                        }
                        repaired=true;break;
                    }
                    for(size_t next:occupants[t.key])if(parent[next]<0){
                        parent[next]=int(cur);via[next]=ti;queue.push_back(next);
                    }
                }
            }
            if(!repaired)throw std::runtime_error("No capacity-feasible SLL QP projection");
        }
        std::map<Bin,size_t> after;
        for(size_t i=0;i<count;++i){++after[bin(m.tiles[result.tiles[i]])];result.moved+=result.tiles[i]!=old[i];}
        result.energyAfter=energy(after);return result;
    }
};
}
