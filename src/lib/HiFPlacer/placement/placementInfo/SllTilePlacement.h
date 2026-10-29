#pragma once
// Definitions follow Model in SllVirtualModel.h.
namespace amf_sll_plan {
inline void Model::indexTiles() {
    columnPolicy.validate();
    tiles.clear();availableTiles.clear();tileCapacity.clear();columnCapacity.clear();tileColumns.clear();
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
        tileColumns[range.first][t.in.x].push_back(int(tiles.size()));
        availableTiles[range.first].push_back(int(tiles.size()));tiles.push_back(std::move(t));
    }
    for(const auto&v:supply){
        tileCapacity[v.first]=v.second.size();
        columnCapacity[physicalColumn(v.first)]+=v.second.size();
    } // Count physical lanes once; upward/downward views share this supply.
}
inline int Model::initialTile(int from,int to,Point root,const TileBox&input,const TileBox&output,
                             double weight,const std::map<TileKey,size_t>&occupied,
                             int incumbent,int fixedColumn,const std::map<ColumnKey,size_t>*occupiedColumns)const {
    if(!(initialTargetUtilization>0&&initialTargetUtilization<=1)||
       !std::isfinite(initialTargetUtilization)||!(initialDensityWeight>=0)||!std::isfinite(initialDensityWeight))
        throw std::invalid_argument("Invalid SLL initialization density parameters");
    if(!(initialColumnUtilizationLimit>0&&initialColumnUtilizationLimit<=1)||!std::isfinite(initialColumnUtilizationLimit))
        throw std::invalid_argument("SllInitialColumnUtilizationLimit must be in (0,1]");
    auto load=[&](const Tile&t){auto i=occupied.find(t.key);return i==occupied.end()?size_t(0):i->second;};
    const bool transient=transientCapacity(from,to);
    auto feasible=[&](int ti){const auto&t=tiles[ti];return transient||load(t)<tileCapacity.at(t.key);};
    std::map<ColumnKey,size_t> computedColumnLoads;
    if(fixedColumn<0&&!occupiedColumns){
        for(const auto&v:occupied)computedColumnLoads[physicalColumn(v.first)]+=v.second;
        occupiedColumns=&computedColumnLoads;
    }
    int preferredTier=ColumnPolicy::unavailable;
    auto tier=[&](int ti){
        const auto key=physicalColumn(tiles[ti].key);
        const auto load=occupiedColumns->find(key);
        const size_t count=load==occupiedColumns->end()?0:load->second;
        if(columnPolicy.enabled)return columnPolicy.tier(count+1,columnCapacity.at(key),transient);
        return double(count)+1<=initialColumnUtilizationLimit*columnCapacity.at(key)+1e-9?0:ColumnPolicy::unavailable;
    };
    std::vector<int> candidates;
    const auto&columns=tileColumns.at({from,to});
    if(fixedColumn>=0){
        // Refining Y consumes no additional column slot. Keep the already
        // admitted column, without applying admission a second time.
        for(const auto&column:columns)for(int ti:column.second)
            if(tiles[ti].column==fixedColumn&&feasible(ti))candidates.push_back(ti);
    } else {
        std::vector<std::pair<double,double>> order;
        for(const auto&column:columns)order.push_back({std::abs(root.x-column.first),column.first});
        for(const auto&col:columns)for(int ti:col.second)if(feasible(ti)){
            preferredTier=std::min(preferredTier,tier(ti));break;
        }
        std::sort(order.begin(),order.end());double nearest=INFINITY;
        for(const auto&column:order){
            if(sourceNearInitialization&&column.first>nearest+1e-9)break;
            const auto&rows=columns.at(column.second);
            if(rows.empty()||preferredTier==ColumnPolicy::unavailable||tier(rows.front())!=preferredTier)continue;
            for(int ti:rows)if(feasible(ti)){
                candidates.push_back(ti);nearest=column.first;
            }
        }
    }
    if(candidates.empty())return -1;
    double best=INFINITY,bestUtilization=INFINITY;std::vector<int> equal;
    for(int ti:candidates){
        ++initialTileEvaluations;const auto&t=tiles[ti];const double n=load(t),c=tileCapacity.at(t.key);
        auto penalty=[&](double count){const double e=std::max(0.,count-initialTargetUtilization*c);return e*e/c;};
        const double score=weight*(input.with(t.in,ratio)+output.with(t.out,ratio))+
                           initialDensityWeight*(penalty(n+1)-penalty(n));
        const double utilization=(n+1)/c;
        if(score<best-1e-9 || (std::abs(score-best)<=1e-9&&utilization<bestUtilization-1e-12)){
            best=score;bestUtilization=utilization;equal={ti};
        }else if(std::abs(score-best)<=1e-9&&std::abs(utilization-bestUtilization)<=1e-12)equal.push_back(ti);
    }
    if(equal.empty())throw std::runtime_error("Non-finite SLL initialization score");
    if(std::find(equal.begin(),equal.end(),incumbent)!=equal.end())return incumbent;
    double lo=INFINITY,hi=-INFINITY;
    for(int ti:equal){lo=std::min(lo,tiles[ti].in.y);hi=std::max(hi,tiles[ti].in.y);}
    const double middle=(lo+hi)*.5;
    // Physical coordinates provide a stable final order even if inventory input
    // order changes. An even-sized interval uses its lower middle row.
    return *std::min_element(equal.begin(),equal.end(),[&](int a,int b){
        const auto&x=tiles[a];const auto&y=tiles[b];
        return std::make_tuple(std::abs(x.in.y-middle),x.in.y,x.column,x.in.x,x.out.x,x.out.y)<
               std::make_tuple(std::abs(y.in.y-middle),y.in.y,y.column,y.in.x,y.out.x,y.out.y);
    });
}
inline void Model::rebuildTiles(std::vector<Net> nets) {
    initialTileEvaluations=0;
    boundaryDemand.clear();transientBoundaries.clear();
    for(const auto&net:nets){
        int lo=net.source.slr,hi=lo;
        for(const auto&sink:net.sinks){lo=std::min(lo,sink.slr);hi=std::max(hi,sink.slr);}
        for(int boundary=lo;boundary<hi;++boundary)++boundaryDemand[boundary];
    }
    if(allowTransientOverflow){
        const auto capacity=boundaryAdmissionCapacity();
        for(const auto&entry:boundaryDemand){
            auto found=capacity.find(entry.first);
            if(found!=capacity.end() && found->second && entry.second>found->second)
                transientBoundaries.insert(entry.first);
        }
    }
    // The priority is immutable during this sort; compute it once per net.
    std::vector<std::pair<double,size_t>> order(nets.size());
    // Each net owns its lookup and priority; the deterministic capacity pass
    // below starts only after these independent preparations have joined.
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t k=0;k<nets.size();++k){auto&n=nets[k];n.sinkLookup.clear();n.sinkLookup.reserve(n.sinks.size());double priority=1;
        for(size_t i=0;i<n.sinks.size();++i){n.sinkLookup[n.sinks[i].id]=i;priority=std::max(priority,n.sinks[i].criticality);}
        order[k]={priority,k};
    }
    std::stable_sort(order.begin(),order.end(),[&](const auto&a,const auto&b){
        return a.first==b.first?nets[a.second].id<nets[b.second].id:a.first>b.first;
    });
    std::vector<Net> sorted;sorted.reserve(nets.size());
    for(const auto&item:order)sorted.push_back(std::move(nets[item.second]));nets=std::move(sorted);
    std::map<TileKey,size_t> occupied;std::map<ColumnKey,size_t> occupiedColumns;
    std::vector<Plan> next;std::map<std::string,int> nextIdentity;
    for(auto&input:nets){
        Plan p;p.net=std::move(input);const auto&n=p.net;
        std::vector<std::vector<int>> groups(2);
        for(const auto&s:n.sinks)if(s.slr!=n.source.slr)groups[s.slr>n.source.slr?1:0].push_back(s.id);
        if(groups[0].empty()&&groups[1].empty())continue;
        auto constructTiles=[&](Plan&p,const std::vector<std::vector<int>>&partition){
            const auto&n=p.net;p.paths.reserve(n.sinks.size());for(const auto&s:n.sinks)p.paths[s.id]={};
            // Candidate construction is serial. Record only its few changes
            // and restore the baseline on both normal return and exceptions.
            struct OccupancyUndo {
                std::map<TileKey,size_t>&used;
                std::map<ColumnKey,size_t>&columns;
                struct Original {TileKey key;bool existed;size_t value;};
                std::vector<Original> originals;
                explicit OccupancyUndo(std::map<TileKey,size_t>&map,std::map<ColumnKey,size_t>&columnMap):used(map),columns(columnMap){}
                size_t value(const TileKey&key)const{auto i=used.find(key);return i==used.end()?0:i->second;}
                void change(const TileKey&key,int delta){
                    auto old=std::find_if(originals.begin(),originals.end(),[&](const Original&v){return v.key==key;});
                    if(old==originals.end()){auto i=used.find(key);originals.push_back({key,i!=used.end(),i==used.end()?0:i->second});}
                    auto&v=used[key];auto&c=columns[physicalColumn(key)];
                    if(delta>0){++v;++c;}else{--v;--c;}
                }
                ~OccupancyUndo(){for(const auto&v:originals){
                    auto&c=columns[physicalColumn(v.key)];c-=used.at(v.key);c+=v.value;
                    if(v.existed)used.at(v.key)=v.value;else used.erase(v.key);
                }}
            } undo(occupied,occupiedColumns);
            auto&used=occupied;
            for(size_t g=0;g<partition.size();++g){
                if(partition[g].empty())continue;
                int dir=pin(n,partition[g][0]).slr>n.source.slr?1:-1,last=n.source.slr,parent=-1;
                for(int id:partition[g])last=dir>0?std::max(last,pin(n,id).slr):std::min(last,pin(n,id).slr);
                Point root=n.source.p;
                for(int from=n.source.slr;from!=last;from+=dir){
                    std::vector<int> downstream;for(int id:partition[g])
                        if(dir>0?pin(n,id).slr>=from+dir:pin(n,id).slr<=from+dir)downstream.push_back(id);
                    const std::string key=std::to_string(n.id)+":"+std::to_string(g)+":"+std::to_string(from)+":"+std::to_string(from+dir);
                    auto old=previous.find(key);int selected=-1;
                    if(old!=previous.end()){
                        const auto&t=tiles.at(old->second);
                        const auto ck=physicalColumn(t.key);
                        if(t.from==from&&t.to==from+dir&&(transientCapacity(from,from+dir)||undo.value(t.key)<tileCapacity.at(t.key))&&
                           columnPolicy.tier(occupiedColumns[ck]+1,columnCapacity.at(ck),transientCapacity(from,from+dir))!=ColumnPolicy::unavailable)selected=old->second;
                    }
                    if(selected<0){
                        // Cache both local endpoint boxes once, not once per
                        // candidate or receiver. No timing model is consulted.
                        TileBox input,output;input.add(root);
                        if(parent<0){for(const auto&s:n.sinks)if(s.slr==from)input.add(s.p);}
                        else {for(int id:partition[g])if(pin(n,id).slr==from)input.add(pin(n,id).p);}
                        for(int id:downstream)if(pin(n,id).slr==from+dir)output.add(pin(n,id).p);
                        selected=initialTile(from,from+dir,root,input,output,n.weight,used,-1,-1,&occupiedColumns);
                    }
                    if(selected<0)throw std::runtime_error("SLL initialization found no admissible column/tile: column utilization limit or physical tile capacity exhausted");
                    Crossing e;e.key=key;e.parent=parent;e.sinks=downstream;e.tile=selected;
                    int k=p.crossings.size();p.crossings.push_back(e);undo.change(tiles[selected].key,1);
                    for(int id:downstream)p.paths[id].push_back(k);
                    parent=k;root=tiles[selected].out;
                }
            }
            // With the complete topology available, score the two adjacent
            // local hyperedges. Other trees are constant for this tile move.
            // Refine newborn/reallocated crossings only. Retained legal tiles
            // are QP results and must not be reseeded on the next refresh.
            for(size_t k=0;k<p.crossings.size();++k){
                auto&e=p.crossings[k];
                const auto old=previous.find(e.key);
                if(old!=previous.end()&&old->second==e.tile)continue;
                const auto base=tiles[e.tile];undo.change(base.key,-1);
                const auto trees=localTrees(p);TileBox input,output;
                auto fill=[&](TileBox&box,const std::vector<Terminal>&tree){for(const auto&t:tree)
                    if(t.crossing!=int(k))box.add(t.isFixed?t.fixed:pin(n,t.pin).p);};
                fill(input,trees.at(e.parent+1));fill(output,trees.at(k+1));
                const int incumbent=old==previous.end()?-1:old->second;
                Point root=e.parent<0?n.source.p:tiles[p.crossings[e.parent].tile].out;
                int selected=initialTile(base.from,base.to,root,input,output,n.weight,used,incumbent,base.column,&occupiedColumns);
                if(selected<0)throw std::runtime_error("SLL initialization lost a feasible tile");
                e.tile=selected;undo.change(tiles[selected].key,1);
            }
        };
        constructTiles(p,groups);
        double retainedCost=0;bool hasRetainedCost=false;
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
                // Every proposal uses the same immutable net. Transfer its
                // ownership temporarily instead of copying all receiver data.
                if(!hasRetainedCost){retainedCost=cost(p);hasRetainedCost=true;}
                Plan candidate;candidate.net=std::move(p.net);
                try{
                    constructTiles(candidate,proposal);
                    const double candidateCost=cost(candidate);
                    if(candidateCost<retainedCost-1e-9){retainedCost=candidateCost;p=std::move(candidate);}
                    else p.net=std::move(candidate.net);
                } catch(const std::runtime_error&){p.net=std::move(candidate.net);/* Preserve the feasible shared tree. */}
            }
        }
        for(const auto&e:p.crossings){const auto&key=tiles[e.tile].key;++occupied[key];++occupiedColumns[physicalColumn(key)];nextIdentity[e.key]=e.tile;}
        next.push_back(std::move(p));
    }
    created=retained=deleted=0;
    for(const auto&v:nextIdentity){if(previous.count(v.first))++retained;else ++created;}
    for(const auto&v:previous)if(!nextIdentity.count(v.first))++deleted;
    plans=std::move(next);previous=std::move(nextIdentity);
}
inline void Model::legalizeChannels() {
    if(!jointPlacement)return;
    struct Item {size_t plan,crossing;std::vector<int> candidates;};
    std::map<TileKey,std::vector<Item>> grouped;
    std::map<std::string,TileKey> owner;
    for(size_t pi=0;pi<plans.size();++pi)for(size_t k=0;k<plans[pi].crossings.size();++k){
        const auto&t=tiles.at(plans[pi].crossings[k].tile);
        grouped[t.key].push_back({pi,k,t.members});
        for(int ci:t.members)for(const auto&r:footprint(channels[ci])){
            const auto added=owner.emplace(r,t.key);
            if(!added.second&&added.first->second!=t.key){legalizeChannelsSerial();return;}
        }
    }
    std::map<ColumnKey,size_t> columnLoads;
    for(const auto&group:grouped){
        if(group.second.size()>tileCapacity.at(group.first))
            throw std::runtime_error("Final SLL tile demand exceeds physical capacity");
        columnLoads[physicalColumn(group.first)]+=group.second.size();
    }
    for(const auto&entry:columnLoads)
        if(columnPolicy.tier(entry.second,columnCapacity.at(entry.first))==ColumnPolicy::unavailable)
            throw std::runtime_error("Final SLL column utilization ceiling violated");
    // Proven disjoint physical footprints allow independent tile searches.
    std::vector<std::vector<Item>> groups;
    for(auto&g:grouped){groups.push_back(std::move(g.second));auto&items=groups.back();
        std::stable_sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.candidates.size()<b.candidates.size();});}
    std::vector<std::vector<int>> choices(groups.size());
    std::vector<std::string> errors(groups.size());
    #pragma omp parallel for schedule(dynamic,1)
    for(size_t g=0;g<groups.size();++g)try{
        const auto&items=groups[g];auto&chosen=choices[g];chosen.assign(items.size(),-1);
        std::set<std::string> used=reserved;size_t visits=0;
        std::function<bool(size_t)> search=[&](size_t i){
            if(i==items.size())return true;
            if(++visits>100000)throw std::runtime_error("SLL parallel lane search limit");
            for(int ci:items[i].candidates)if(!conflicts(channels[ci],used)){
                occupy(channels[ci],used);chosen[i]=ci;if(search(i+1))return true;
                for(const auto&r:footprint(channels[ci]))used.erase(r);chosen[i]=-1;
            }return false;
        };
        if(!search(0))throw std::runtime_error("No compatible lane assignment");
    }catch(const std::exception&e){errors[g]=e.what();}
    for(const auto&e:errors)if(!e.empty())throw std::runtime_error(e);
    std::set<std::string> used=reserved;
    for(size_t g=0;g<groups.size();++g)for(int ci:choices[g]){
        if(conflicts(channels[ci],used))throw std::runtime_error("Parallel lane result resource collision");
        occupy(channels[ci],used);
    }
    for(size_t g=0;g<groups.size();++g)for(size_t i=0;i<groups[g].size();++i){
        const auto&item=groups[g][i];plans[item.plan].crossings[item.crossing].channel=choices[g][i];
    }
}

inline void Model::legalizeChannelsSerial() {
    if(!jointPlacement)return;
    // Exact channels are assigned only here, after tile planning. Group by the
    // physical pair so upward/downward crossings compete for the same lanes.
    struct Item {size_t plan,crossing;std::vector<int> candidates;};
    std::map<TileKey,std::vector<Item>> groups;
    for(size_t pi=0;pi<plans.size();++pi)for(size_t k=0;k<plans[pi].crossings.size();++k){
        const auto&t=tiles.at(plans[pi].crossings[k].tile);
        groups[t.key].push_back({pi,k,t.members});
    }
    std::map<ColumnKey,size_t> columnLoads;
    for(const auto&group:groups){
        if(group.second.size()>tileCapacity.at(group.first))
            throw std::runtime_error("Final SLL tile demand exceeds physical capacity");
        columnLoads[physicalColumn(group.first)]+=group.second.size();
    }
    for(const auto&entry:columnLoads)
        if(columnPolicy.tier(entry.second,columnCapacity.at(entry.first))==ColumnPolicy::unavailable)
            throw std::runtime_error("Final SLL column utilization ceiling violated");
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

