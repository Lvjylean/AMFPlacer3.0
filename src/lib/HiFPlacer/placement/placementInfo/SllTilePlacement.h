#pragma once
// Definitions follow Model in SllVirtualModel.h.
namespace amf_sll_plan {
inline void Model::indexTiles() {
    tiles.clear();availableTiles.clear();tileCapacity.clear();
    std::map<TileKey,std::set<std::string>> supply;
    std::map<std::string,TileKey> identities;
    for(const auto&range:geometryBuckets)for(const auto&group:range.second){
        Tile t;static_cast<Channel&>(t)=channels[group.front()];
        t.key={std::min(t.from,t.to),t.column,std::min(t.in.y,t.out.y)};
        t.resource.clear();t.segment.clear();t.resources.clear();
        t.slowMax=t.fastMax=0;t.slowMin=t.fastMin=INFINITY;
        for(int ci:group){
            const auto&c=channels[ci];
            auto inserted=identities.emplace(c.resource,t.key);
            if(!inserted.second && inserted.first->second!=t.key)
                throw std::invalid_argument("SLL directions disagree on tile identity");
            if(conflicts(c,reserved))continue;
            t.members.push_back(ci);supply[t.key].insert(c.resource);
            // Global timing uses an envelope, independent of the eventual lane.
            t.slowMax=std::max(t.slowMax,c.slowMax);t.fastMax=std::max(t.fastMax,c.fastMax);
            t.slowMin=std::min(t.slowMin,c.slowMin);t.fastMin=std::min(t.fastMin,c.fastMin);
        }
        if(t.members.empty())continue;
        availableTiles[range.first].push_back(int(tiles.size()));tiles.push_back(std::move(t));
    }
    for(const auto&v:supply)tileCapacity[v.first]=v.second.size();
}
inline void Model::rebuildTiles(std::vector<Net> nets) {
    for(auto&n:nets){n.sinkLookup.clear();for(size_t i=0;i<n.sinks.size();++i)n.sinkLookup[n.sinks[i].id]=i;}
    std::stable_sort(nets.begin(),nets.end(),[](const Net&a,const Net&b){
        double wa=1,wb=1;for(auto&s:a.sinks)wa=std::max(wa,s.criticality);for(auto&s:b.sinks)wb=std::max(wb,s.criticality);
        return wa==wb?a.id<b.id:wa>wb;
    });
    std::map<TileKey,size_t> occupied;std::vector<Plan> next;std::map<std::string,int> nextIdentity;
    for(const auto&n:nets){
        std::vector<std::vector<int>> groups(2);
        for(const auto&s:n.sinks)if(s.slr!=n.source.slr)groups[s.slr>n.source.slr?1:0].push_back(s.id);
        if(groups[0].empty()&&groups[1].empty())continue;
        auto constructTiles=[&](const std::vector<std::vector<int>>&partition){
            Plan p;p.net=n;for(const auto&s:n.sinks)p.paths[s.id]={};
            auto used=occupied;
            for(size_t g=0;g<partition.size();++g){
                if(partition[g].empty())continue;
                int dir=pin(n,partition[g][0]).slr>n.source.slr?1:-1,last=n.source.slr,parent=-1;
                for(int id:partition[g])last=dir>0?std::max(last,pin(n,id).slr):std::min(last,pin(n,id).slr);
                Point root=n.source.p;
                for(int from=n.source.slr;from!=last;from+=dir){
                    std::vector<int> downstream;for(int id:partition[g])
                        if(dir>0?pin(n,id).slr>=from+dir:pin(n,id).slr<=from+dir)downstream.push_back(id);
                    const std::string key=std::to_string(n.id)+":"+std::to_string(g)+":"+std::to_string(from)+":"+std::to_string(from+dir);
                    auto old=previous.find(key);int selected=-1;double best=INFINITY,nearest=INFINITY;
                    for(int ti:availableTiles.at({from,from+dir})){
                        const auto&t=tiles[ti];if(used[t.key]>=tileCapacity.at(t.key))continue;
                        double x0=t.out.x,x1=t.out.x,y0=t.out.y,y1=t.out.y,td=0;
                        for(int id:downstream){const auto&s=pin(n,id);x0=std::min(x0,s.p.x);x1=std::max(x1,s.p.x);
                            if(s.slr==t.to){y0=std::min(y0,s.p.y);y1=std::max(y1,s.p.y);
                                td=std::max(td,s.criticality*(delay(root,t.in)+t.slowMax+delay(t.out,s.p)));}
                        }
                        const double score=n.weight*(length(root,t.in,ratio)+x1-x0+ratio*(y1-y0))+timingWeight*td;
                        const double sx=std::abs(root.x-t.in.x);
                        if(sourceNearInitialization && old==previous.end()){
                            if(sx<nearest || (sx==nearest && score<best)){nearest=sx;best=score;selected=ti;}
                        }else if(score<best){best=score;selected=ti;}
                    }
                    if(old!=previous.end()){
                        const auto&t=tiles.at(old->second);
                        if(t.from==from&&t.to==from+dir&&used[t.key]<tileCapacity.at(t.key))selected=old->second;
                    }
                    if(selected<0)throw std::runtime_error("SLL tile allocator exhausted capacity");
                    Crossing e;e.key=key;e.parent=parent;e.sinks=downstream;e.tile=selected;
                    int k=p.crossings.size();p.crossings.push_back(e);++used[tiles[selected].key];
                    for(int id:downstream)p.paths[id].push_back(k);
                    parent=k;root=tiles[selected].out;
                }
            }
            // Once the whole tree exists, include both adjacent local trees and
            // all downstream timing paths when choosing newborn Y positions.
            for(size_t k=0;k<p.crossings.size();++k){
                auto&e=p.crossings[k];if(previous.count(e.key))continue;
                const auto base=tiles[e.tile];--used[base.key];int selected=e.tile;double best=cost(p);
                for(int ti:availableTiles.at({base.from,base.to})){
                    const auto&t=tiles[ti];
                    if(sourceNearInitialization && t.column!=base.column)continue;
                    if(used[t.key]>=tileCapacity.at(t.key))continue;
                    e.tile=ti;double value=cost(p);if(value<best-1e-9){best=value;selected=ti;}
                }
                e.tile=selected;++used[tiles[selected].key];
            }
            return p;
        };
        Plan p=constructTiles(groups);
        if(allowSplit)for(int dir=0;dir<2;++dir)if(groups[dir].size()>1){
            auto ids=groups[dir];std::stable_sort(ids.begin(),ids.end(),[&](int a,int b){return pin(n,a).p.x<pin(n,b).p.x;});
            std::vector<std::vector<int>> alternatives{std::vector<int>(ids.begin(),ids.begin()+ids.size()/2)};
            double mx=1;for(int id:ids)mx=std::max(mx,pin(n,id).criticality);
            std::vector<int> critical;for(int id:ids)if(pin(n,id).criticality>=mx*.8)critical.push_back(id);
            if(!critical.empty()&&critical.size()<ids.size())alternatives.push_back(critical);
            for(const auto&first:alternatives){
                auto proposal=groups;proposal[dir]=first;std::vector<int> rest;
                for(int id:ids)if(std::find(first.begin(),first.end(),id)==first.end())rest.push_back(id);
                proposal.push_back(rest);
                try{auto candidate=constructTiles(proposal);if(cost(candidate)<cost(p)-1e-9)p=std::move(candidate);}
                catch(const std::runtime_error&){/* Preserve the feasible shared tree. */}
            }
        }
        for(const auto&e:p.crossings){++occupied[tiles[e.tile].key];nextIdentity[e.key]=e.tile;}
        next.push_back(std::move(p));
    }
    created=retained=deleted=0;
    for(const auto&v:nextIdentity){if(previous.count(v.first))++retained;else ++created;}
    for(const auto&v:previous)if(!nextIdentity.count(v.first))++deleted;
    plans=std::move(next);previous=std::move(nextIdentity);
}
inline void Model::legalizeChannels() {
    if(!jointPlacement)return;
    // Exact channels are assigned only here, after tile planning. Group by the
    // physical pair so upward/downward crossings compete for the same lanes.
    struct Item {size_t plan,crossing;std::vector<int> candidates;};
    std::map<TileKey,std::vector<Item>> groups;
    for(size_t pi=0;pi<plans.size();++pi)for(size_t k=0;k<plans[pi].crossings.size();++k){
        const auto&t=tiles.at(plans[pi].crossings[k].tile);
        groups[t.key].push_back({pi,k,t.members});
    }
    auto result=plans;std::set<std::string> used=reserved;
    for(auto&group:groups){
        auto&items=group.second;
        std::stable_sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.candidates.size()<b.candidates.size();});
        size_t visits=0;
        std::function<bool(size_t)> search=[&](size_t i){
            if(i==items.size())return true;
            if(++visits>100000)throw std::runtime_error("SLL lane legalization search limit; tile plan not exported");
            const auto&item=items[i];
            for(int ci:item.candidates)if(!conflicts(channels[ci],used)){
                occupy(channels[ci],used);result[item.plan].crossings[item.crossing].channel=ci;
                if(search(i+1))return true;
                for(const auto&r:footprint(channels[ci]))used.erase(r);
                result[item.plan].crossings[item.crossing].channel=-1;
            }
            return false;
        };
        if(!search(0))throw std::runtime_error("SLL tile plan has no compatible exact-channel assignment; not exported");
    }
    plans=std::move(result);
}
}

