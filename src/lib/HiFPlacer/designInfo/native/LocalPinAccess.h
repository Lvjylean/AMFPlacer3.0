#ifndef AMF_LOCAL_PIN_ACCESS_H
#define AMF_LOCAL_PIN_ACCESS_H

#include <algorithm>
#include <functional>
#include <iostream>
#include <numeric>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace amf_native {
// An entrance is a certified BEL/site-pin -> external node -> one-hop path.
// A successful assignment only proves local resource compatibility, never
// global routability. Ordinary runs do not instantiate this policy.
class LocalPinAccess {
public:
    struct Resource {
        std::string name;
        bool occupied=false, bad=false;
        // -1: ordinary; 0/1: dedicated constant source (not a signal LUT).
        int tie=-1;
        // Logical constant of an existing physical owner, independently of tie.
        int ownerConstant=-1;
        std::set<std::string> ownerPorts;
    };
    struct Demand {
        std::string pin, net;
        int constant=-1;
        std::set<std::string> ports;
        std::vector<std::vector<unsigned>> entrances;
    };
    struct Solution {
        bool feasible=false;
        std::vector<std::vector<unsigned>> entrances;
        std::size_t explored=0;
    };
    std::vector<Resource> resources;

    bool available(unsigned id,const Demand &d) const {
        const auto &r=resources.at(id);
        if(r.bad || (r.tie>=0 && r.tie!=d.constant))return false;
        if(!r.occupied)return true;
        // Equal logical values alone do not identify a routed signal net.
        if(d.constant>=0 && r.ownerConstant==d.constant)return true;
        for(const auto &p:d.ports)if(r.ownerPorts.count(p))return true;
        return false;
    }
    using ContextIndex=std::map<unsigned,std::vector<unsigned>>;
    ContextIndex indexContext(const std::vector<Demand>&context) const {
        ContextIndex index;
        for(unsigned i=0;i<context.size();++i){const auto&d=context[i];std::set<unsigned> nodes;
            for(const auto&path:d.entrances) {
                if(path.empty())throw std::runtime_error("Empty context entrance path");
                if(std::all_of(path.begin(),path.end(),[&](unsigned r){return available(r,d);}))
                    nodes.insert(path.begin(),path.end());
            }
            for(auto r:nodes)index[r].push_back(i);
        }
        return index;
    }
    // Context must already be jointly feasible. Components linked only by
    // same-net resources are independent: sharing them is explicitly legal.
    std::vector<unsigned> conflictingContext(const std::vector<Demand>&seeds,
        const std::vector<Demand>&context,const ContextIndex&index) const {
        std::map<unsigned,std::set<std::string>> visited;
        std::vector<std::pair<unsigned,std::string>> queue;
        std::set<unsigned> included;
        auto include=[&](const Demand&d){
            for(const auto&path:d.entrances) {
                if(path.empty())throw std::runtime_error("Empty seed entrance path");
                if(!std::all_of(path.begin(),path.end(),[&](unsigned r){return available(r,d);}))continue;
                for(auto r:path)if(visited[r].insert(d.net).second)queue.emplace_back(r,d.net);
            }
        };
        for(const auto&d:seeds)include(d);
        for(std::size_t pos=0;pos<queue.size();++pos){auto entry=queue[pos];auto found=index.find(entry.first);
            if(found==index.end())continue;
            for(auto i:found->second)if(context[i].net!=entry.second&&included.insert(i).second)include(context[i]);
        }
        return std::vector<unsigned>(included.begin(),included.end());
    }
    Solution solve(const std::vector<Demand> &demands,std::size_t searchLimit=100000) const {
        Solution result;result.entrances.resize(demands.size());
        std::vector<std::vector<std::vector<unsigned>>> choices(demands.size());
        std::vector<unsigned> order;
        for(unsigned i=0;i<demands.size();++i) {
            if(demands[i].net.empty())throw std::runtime_error("Local access demand has no net identity");
            for(auto path:demands[i].entrances) {
                if(path.empty())throw std::runtime_error("Empty entrance is not a reachability proof");
                std::sort(path.begin(),path.end());path.erase(std::unique(path.begin(),path.end()),path.end());
                if(std::all_of(path.begin(),path.end(),[&](unsigned r){return available(r,demands[i]);}))
                    choices[i].push_back(std::move(path));
            }
            std::sort(choices[i].begin(),choices[i].end());
            choices[i].erase(std::unique(choices[i].begin(),choices[i].end()),choices[i].end());
            if(choices[i].empty())return result;
            order.push_back(i);
        }
        if(!demands.empty()&&!searchLimit)
            throw std::runtime_error("Local entrance assignment search budget exhausted; feasibility unresolved");
        // Linear first-fit is only a successful-witness shortcut. A failed
        // attempt falls through to the exact solver; it never rejects a site.
        // This avoids rebuilding/searching conflict graphs for easy candidates.
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return choices[a].size()<choices[b].size();});
        std::map<unsigned,std::string> greedyUsed;
        bool greedy=true;
        for(auto i:order) {
            bool found=false;
            for(const auto &path:choices[i]) {
                bool ok=true;
                for(auto r:path){auto it=greedyUsed.find(r);if(it!=greedyUsed.end()&&it->second!=demands[i].net){ok=false;break;}}
                if(!ok)continue;
                for(auto r:path)greedyUsed[r]=demands[i].net;
                result.entrances[i]=path;found=true;break;
            }
            if(!found){greedy=false;break;}
        }
        if(greedy){result.feasible=true;return result;}
        result.entrances.assign(demands.size(),{});
        // Paths differing only in resources that no other net can use are
        // equivalent for this local CSP. Keep a real full-path witness, but
        // search only nondominated inter-net conflict footprints.
        std::map<unsigned,std::string> firstNet;std::set<unsigned> contested;
        for(unsigned i=0;i<demands.size();++i)for(const auto &path:choices[i])for(auto r:path) {
            auto found=firstNet.emplace(r,demands[i].net);
            if(!found.second&&found.first->second!=demands[i].net)contested.insert(r);
        }
        for(auto &domain:choices) {
            std::map<std::vector<unsigned>,std::vector<unsigned>> representatives;
            for(const auto &path:domain) {
                std::vector<unsigned> key;
                for(auto r:path)if(contested.count(r))key.push_back(r);
                representatives.emplace(std::move(key),path);
            }
            std::vector<std::vector<unsigned>> minimal;
            for(const auto &candidate:representatives) {
                bool dominated=false;
                for(const auto &alternative:representatives) {
                    if(alternative.first.size()>=candidate.first.size())continue;
                    if(std::includes(candidate.first.begin(),candidate.first.end(),alternative.first.begin(),alternative.first.end())){
                        dominated=true;break;
                    }
                }
                if(!dominated)minimal.push_back(candidate.second);
            }
            domain=std::move(minimal);
        }
        // Split by possible conflicts, not by same-net sharing. Independent
        // components cannot consume each other's resources for different nets.
        std::vector<unsigned> parent(demands.size());std::iota(parent.begin(),parent.end(),0);
        std::function<unsigned(unsigned)> root=[&](unsigned x){return parent[x]==x?x:parent[x]=root(parent[x]);};
        auto join=[&](unsigned a,unsigned b){a=root(a);b=root(b);if(a!=b)parent[b]=a;};
        std::map<unsigned,std::vector<unsigned>> incidence;
        for(unsigned i=0;i<demands.size();++i){std::set<unsigned> unique;
            for(const auto &path:choices[i])unique.insert(path.begin(),path.end());
            for(auto r:unique)incidence[r].push_back(i);}
        for(const auto &entry:incidence){const auto &ids=entry.second;if(ids.empty())continue;
            bool conflict=false;for(auto i:ids)if(demands[i].net!=demands[ids[0]].net){conflict=true;break;}
            if(conflict)for(auto i:ids)join(ids[0],i);}
        std::map<unsigned,std::vector<unsigned>> components;
        for(unsigned i=0;i<demands.size();++i)components[root(i)].push_back(i);
        std::map<unsigned,std::pair<std::string,unsigned>> used;
        std::vector<bool> assigned(demands.size(),false);
        auto compatible=[&](unsigned i,const std::vector<unsigned> &path){
            for(auto r:path){auto it=used.find(r);if(it!=used.end()&&it->second.first!=demands[i].net)return false;}
            return true;
        };
        for(const auto &component:components){const auto &ids=component.second;used.clear();
            std::function<bool(unsigned)> visit=[&](unsigned remaining){
                if(!remaining)return true;
                if(++result.explored>searchLimit){
                    std::cerr<<"AMF_LOCAL_ACCESS_UNRESOLVED demands="<<demands.size()<<" component="<<ids.size()<<" remaining="<<remaining<<"\n";
                    for(auto i:ids){std::cerr<<"AMF_LOCAL_ACCESS_REPLAY\t"<<i<<'\t'<<demands[i].net<<'\t';
                        bool first=true;for(const auto &path:choices[i]){if(!first)std::cerr<<';';first=false;
                            for(unsigned k=0;k<path.size();++k)std::cerr<<(k?",":"")<<path[k];}std::cerr<<"\n";}
                    throw std::runtime_error("Local entrance assignment search budget exhausted; feasibility unresolved");
                }
                // Recompute compatible domains: reject a newly blocked demand
                // immediately, before exploring unrelated pin combinations.
                unsigned selected=demands.size();std::vector<unsigned> options;
                for(auto i:ids)if(!assigned[i]){std::vector<unsigned> valid;
                    for(unsigned j=0;j<choices[i].size();++j)if(compatible(i,choices[i][j]))valid.push_back(j);
                    if(valid.empty())return false;
                    if(selected==demands.size()||valid.size()<options.size()){selected=i;options=std::move(valid);}
                }
                assigned[selected]=true;const auto &d=demands[selected];
                for(auto j:options){const auto &path=choices[selected][j];
                    for(auto r:path){auto &u=used[r];u.first=d.net;++u.second;}
                    result.entrances[selected]=path;
                    if(visit(remaining-1))return true;
                    for(auto r:path)if(--used.at(r).second==0)used.erase(r);
                    result.entrances[selected].clear();
                }
                assigned[selected]=false;return false;
            };
            if(!visit(ids.size())){result.entrances.assign(demands.size(),{});return result;}
        }
        result.feasible=true;return result;
    }
};
}
#endif
