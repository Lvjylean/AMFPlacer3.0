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
    // A fresh compact view keeps capacity/configuration changes observable.
    // Sorted bin IDs preserve the legacy floating-point energy summation order.
    struct DenseUsage {
        std::vector<size_t> tileKey, tileBin, supply, binSupply, tileColumn, columnSupply;
        DenseUsage(const Model&m,const DensityModel&d) {
            std::map<TileKey,size_t> tileIds;
            for(const auto&entry:m.tileCapacity){tileIds.emplace(entry.first,supply.size());supply.push_back(entry.second);}
            std::map<Bin,size_t> binIds;
            for(const auto&entry:d.capacity)binIds.emplace(entry.first,0);
            for(const auto&t:m.tiles)binIds.emplace(d.bin(t),0);
            for(auto&entry:binIds){entry.second=binSupply.size();auto found=d.capacity.find(entry.first);
                binSupply.push_back(found==d.capacity.end()?0:found->second);}
            std::map<ColumnKey,size_t> columnIds;
            for(const auto&entry:m.columnCapacity){columnIds.emplace(entry.first,columnSupply.size());columnSupply.push_back(entry.second);}
            for(const auto&t:m.tiles)tileColumn.push_back(columnIds.at(physicalColumn(t.key)));
            tileKey.reserve(m.tiles.size());tileBin.reserve(m.tiles.size());
            for(const auto&t:m.tiles){tileKey.push_back(tileIds.at(t.key));tileBin.push_back(binIds.at(d.bin(t)));}
        }
        double penalty(size_t b,size_t count,double target)const {
            if(!binSupply[b])return count?INFINITY:0;
            const double excess=std::max(0.,double(count)-target*binSupply[b]);
            return excess*excess/binSupply[b];
        }
        double energy(const std::vector<size_t>&loads,double target)const {
            double value=0;for(size_t i=0;i<loads.size();++i)value+=penalty(i,loads[i],target);return value;
        }
    };
    // Ordinary incumbents are feasible. On an explicitly overfull boundary,
    // density is still measured against the unchanged physical supply.
    Assignment spread(const Model&m,const std::vector<Point>&desired)const {
        if(!m.jointPlacement)throw std::invalid_argument("Tile density requires joint placement");
        Assignment result;result.tiles=current(m);
        if(desired.size()!=result.tiles.size())throw std::invalid_argument("SLL proposal dimension mismatch");
        const DenseUsage usage(m,*this);
        std::vector<size_t> loads(usage.binSupply.size()),tileLoads(usage.supply.size()),columnLoads(usage.columnSupply.size());
        for(int ti:result.tiles){++columnLoads[usage.tileColumn.at(ti)];++loads[usage.tileBin.at(ti)];const auto key=usage.tileKey.at(ti);
            if(++tileLoads[key]>usage.supply[key]&&!m.transientCapacity(m.tiles[ti].from,m.tiles[ti].to))
                throw std::runtime_error("Overfull incumbent SLL tile");}
        result.energyBefore=usage.energy(loads,targetUtilization);
        // Only the selected tile changes in this coordinate-descent pass.
        // Copying complete nets, pin names and route trees is unnecessary.
        for(size_t i=0;i<result.tiles.size();++i){
            if(!std::isfinite(desired[i].x)||!std::isfinite(desired[i].y))throw std::runtime_error("Non-finite virtual SLL QP proposal");
            const int old=result.tiles[i];const auto&base=m.tiles[old];
            --loads[usage.tileBin[old]];--tileLoads[usage.tileKey[old]];--columnLoads[usage.tileColumn[old]];
            auto score=[&](int ti){const auto&t=m.tiles[ti];const size_t b=usage.tileBin[ti],n=loads[b];
                return length(desired[i],t.in,m.ratio)+weight*(usage.penalty(b,n+1,targetUtilization)-usage.penalty(b,n,targetUtilization));};
            int selected=-1,bestTier=ColumnPolicy::unavailable;double best=INFINITY;
            for(int ti:m.availableTiles.at({base.from,base.to})){
                const auto key=usage.tileKey[ti];const bool transient=m.transientCapacity(base.from,base.to);
                if(!transient&&tileLoads[key]>=usage.supply[key])continue;
                const size_t col=usage.tileColumn[ti];const int tier=m.columnPolicy.tier(columnLoads[col]+1,usage.columnSupply[col],transient);
                if(tier==ColumnPolicy::unavailable)continue;
                const double value=score(ti);
                if(tier<bestTier||(tier==bestTier&&(value<best-1e-9||(std::abs(value-best)<=1e-9&&ti==old)))){
                    bestTier=tier;best=value;selected=ti;}
            }
            if(!std::isfinite(best))throw std::runtime_error("No legal SLL tile assignment");
            result.tiles[i]=selected;result.moved+=selected!=old;
            ++tileLoads[usage.tileKey[selected]];++loads[usage.tileBin[selected]];++columnLoads[usage.tileColumn[selected]];
        }
        result.energyAfter=usage.energy(loads,targetUtilization);return result;
    }
    // After QP, prefer the lowest feasible column tier, then nearest solved coordinates. Soft
    // density pressure has already entered QP through spread()/the anchor.
    // Assign from scratch so full tiles can exchange occupants in one round.
    // strict=true is the export gate, including after transient overbooking.
    // This is deterministic greedy legalization with augmenting-path repair,
    // not a claim of globally minimum total assignment displacement.
    Assignment assign(const Model&m,const std::vector<Point>&desired,bool strict=false)const {
        if(!m.jointPlacement)throw std::invalid_argument("Tile projection requires joint placement");
        const auto old=current(m);const size_t count=old.size();
        if(desired.size()!=count)throw std::invalid_argument("SLL proposal dimension mismatch");
        const DenseUsage usage(m,*this);
        std::vector<size_t> before(usage.binSupply.size());
        for(size_t i=0;i<count;++i){
            if(!std::isfinite(desired[i].x)||!std::isfinite(desired[i].y))
                throw std::runtime_error("Non-finite virtual SLL QP proposal");
            ++before[usage.tileBin.at(old[i])]; // Reassignment may repair an overfull incumbent.
        }
        const auto admission=m.boundaryAdmissionCapacity();
        std::map<int,size_t> demands;
        for(int ti:old)++demands[std::min(m.tiles[ti].from,m.tiles[ti].to)];
        for(const auto&entry:demands){
            auto found=admission.find(entry.first);
            if((strict||!m.transientCapacity(entry.first,entry.first+1))&&
               (found==admission.end()||entry.second>found->second))
                throw std::runtime_error("Final SLL boundary demand exceeds admissible column capacity");
        }
        auto transient=[&](int ti){return !strict&&m.transientCapacity(m.tiles[ti].from,m.tiles[ti].to);};
        Assignment result;result.tiles.assign(count,-1);result.energyBefore=usage.energy(before,targetUtilization);
        std::vector<std::vector<size_t>> occupants(usage.supply.size()),columnOccupants(usage.columnSupply.size());
        std::vector<size_t> columnLoads(usage.columnSupply.size());
        auto tier=[&](int ti){const auto c=usage.tileColumn[ti];return m.columnPolicy.tier(columnLoads[c]+1,usage.columnSupply[c],transient(ti));};
        auto candidates=[&](size_t i)->const std::vector<int>&{
            const auto&t=m.tiles[old[i]];return m.availableTiles.at({t.from,t.to});};
        auto distance=[&](size_t i,int ti){return length(desired[i],m.tiles[ti].in,m.ratio);};
        auto prefer=[&](size_t i,int a,int b){
            if(b<0)return true;
            const int taTier=tier(a),tbTier=tier(b);
            if(taTier!=tbTier)return taTier<tbTier;
            const double da=distance(i,a),db=distance(i,b);
            if(da<db-1e-9)return true;
            if(db<da-1e-9)return false;
            if((a==old[i])!=(b==old[i]))return a==old[i];
            const auto&ta=m.tiles[a];const auto&tb=m.tiles[b];
            const double ua=double(occupants[usage.tileKey[a]].size()+1)/usage.supply[usage.tileKey[a]];
            const double ub=double(occupants[usage.tileKey[b]].size()+1)/usage.supply[usage.tileKey[b]];
            return std::make_tuple(ua,ta.column,ta.in.x,ta.in.y,ta.out.x,ta.out.y)<
                   std::make_tuple(ub,tb.column,tb.in.x,tb.in.y,tb.out.x,tb.out.y);
        };
        auto move=[&](size_t i,int ti){
            if(result.tiles[i]>=0){
                auto&v=occupants[usage.tileKey[result.tiles[i]]];
                v.erase(std::find(v.begin(),v.end(),i));
                const auto c=usage.tileColumn[result.tiles[i]];--columnLoads[c];
                auto&cv=columnOccupants[c];cv.erase(std::find(cv.begin(),cv.end(),i));
            }
            result.tiles[i]=ti;occupants[usage.tileKey[ti]].push_back(i);
            ++columnLoads[usage.tileColumn[ti]];columnOccupants[usage.tileColumn[ti]].push_back(i);
        };
        for(size_t i=0;i<count;++i){
            int chosen=-1;
            for(int ti:candidates(i)){
                const auto&t=m.tiles[ti];
                if((transient(ti)||occupants[usage.tileKey[ti]].size()<usage.supply[usage.tileKey[ti]])&&tier(ti)!=ColumnPolicy::unavailable&&prefer(i,ti,chosen))chosen=ti;
            }
            if(chosen>=0){move(i,chosen);continue;}
            // Rare asymmetric candidate domains can strand a greedy assignment.
            // Reassign an alternating path instead of violating capacity or
            // restoring a separate wire/timing-based tile decision.
            std::vector<int> parent(count,-1),via(count,-1);
            std::vector<size_t> queue{i};parent[i]=int(i);std::vector<bool> visited(usage.supply.size(),false);
            bool repaired=false;
            for(size_t head=0;head<queue.size()&&!repaired;++head){
                const size_t cur=queue[head];auto ranked=candidates(cur);
                std::stable_sort(ranked.begin(),ranked.end(),[&](int a,int b){
                    const auto&ta=m.tiles[a];const auto&tb=m.tiles[b];
                    return std::make_tuple(distance(cur,a),a!=old[cur],ta.column,ta.in.y,ta.in.x)<
                           std::make_tuple(distance(cur,b),b!=old[cur],tb.column,tb.in.y,tb.in.x);
                });
                for(int ti:ranked){
                    const size_t key=usage.tileKey[ti];if(visited[key])continue;visited[key]=true;
                    if((transient(ti)||occupants[usage.tileKey[ti]].size()<usage.supply[usage.tileKey[ti]])&&tier(ti)!=ColumnPolicy::unavailable){
                        size_t node=cur;int target=ti;
                        while(true){
                            move(node,target);if(node==i)break;
                            target=via[node];node=size_t(parent[node]);
                        }
                        repaired=true;break;
                    }
                    const auto&blocked=occupants[key].size()>=usage.supply[key]?occupants[key]:columnOccupants[usage.tileColumn[ti]];
                    for(size_t next:blocked)if(parent[next]<0){
                        parent[next]=int(cur);via[next]=ti;queue.push_back(next);
                    }
                }
            }
            if(!repaired)throw std::runtime_error("No capacity-feasible SLL QP projection");
        }
        std::vector<size_t> after(usage.binSupply.size());
        for(size_t i=0;i<count;++i){++after[usage.tileBin[result.tiles[i]]];result.moved+=result.tiles[i]!=old[i];}
        for(size_t ti=0;ti<m.tiles.size();++ti){
            const size_t c=usage.tileColumn[ti];
            if(columnLoads[c]&&m.columnPolicy.tier(columnLoads[c],usage.columnSupply[c],transient(ti))==ColumnPolicy::unavailable)
                throw std::runtime_error("SLL column hard utilization ceiling violated");
        }
        result.energyAfter=usage.energy(after,targetUtilization);return result;
    }
};
}
