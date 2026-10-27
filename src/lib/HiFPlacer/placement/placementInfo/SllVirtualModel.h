#ifndef AMF_SLL_VIRTUAL_MODEL_H
#define AMF_SLL_VIRTUAL_MODEL_H
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// Physical optimization objects only: no DesignCell, register, or CLB area.
namespace amf_sll_plan {
struct Point { double x=0,y=0; };
struct Channel {
    std::string resource,segment;
    int from=-1,to=-1,column=-1;
    Point in,out;
    double slowMax=0,slowMin=0,fastMax=0,fastMin=0;
    std::vector<std::string> resources;
};
struct Pin {
    int id=-1,unit=-1,slr=-1;
    std::string name;
    Point p,offset;
    bool movable=false;
    double criticality=1;
};
struct Net { int id=-1;std::string name;Pin source;std::vector<Pin> sinks;double weight=1;std::map<int,size_t> sinkLookup; };
struct Crossing {
    std::string key;
    int channel=-1,parent=-1;
    std::vector<int> sinks; // Pin IDs downstream, not resource multiplicity.
};
struct Plan { Net net;std::vector<Crossing> crossings;std::map<int,std::vector<int>> paths; };
struct Terminal { int pin=-1;Point fixed;bool isFixed=true; };
using LocalDelay=std::function<double(Point,Point)>;
inline double length(Point a,Point b,double ratio) {return std::abs(a.x-b.x)+ratio*std::abs(a.y-b.y);}

class Model {
public:
    std::vector<Channel> channels;
    std::vector<Plan> plans;
    std::map<std::pair<int,int>,std::vector<int>> available;
    std::map<std::pair<int,int>,std::vector<std::vector<int>>> geometryBuckets;
    std::map<std::string,int> previous;
    std::set<std::string> reserved;
    double ratio=.4,timingWeight=1,extraCrossingCost=24,switchCost=.1;
    bool allowSplit=true;
    size_t created=0,retained=0,deleted=0;
    LocalDelay local;

    static std::vector<std::string> makeFootprint(const Channel&c) {
        std::vector<std::string> v{c.resource};size_t b=0;
        while(b<c.segment.size()){auto e=c.segment.find(',',b);v.push_back(c.segment.substr(b,e==std::string::npos?e:e-b));if(e==std::string::npos)break;b=e+1;}return v;
    }
    static const std::vector<std::string>&footprint(const Channel&c){return c.resources;}
    static bool conflicts(const Channel&c,const std::set<std::string>&used){for(const auto&v:footprint(c))if(used.count(v))return true;return false;}
    static void occupy(const Channel&c,std::set<std::string>&used){for(const auto&v:footprint(c))used.insert(v);}
    void index() {
        available.clear();geometryBuckets.clear();std::set<std::pair<std::string,int>> seen;
        for(size_t i=0;i<channels.size();++i) {
            auto&c=channels[i];c.resources=makeFootprint(c);
            if(c.from<0||std::abs(c.from-c.to)!=1||c.resource.empty()||c.segment.empty()||
               !std::isfinite(c.in.x)||!std::isfinite(c.in.y)||!std::isfinite(c.out.x)||!std::isfinite(c.out.y)||
               !(c.slowMax>0)||!(c.slowMin>0)||!(c.fastMax>0)||!(c.fastMin>0)||
               !std::isfinite(c.slowMax)||!std::isfinite(c.slowMin)||!std::isfinite(c.fastMax)||!std::isfinite(c.fastMin)||
               c.slowMax<c.slowMin||c.fastMax<c.fastMin||!seen.emplace(c.resource,c.from).second)
                throw std::invalid_argument("Invalid or duplicate directed SLL channel");
            available[{c.from,c.to}].push_back(int(i));
        }
        for(const auto&range:available) {
            std::map<std::tuple<double,double,double,double>,std::vector<int>> groups;
            for(int i:range.second){const auto&c=channels[i];groups[{c.in.x,c.in.y,c.out.x,c.out.y}].push_back(i);}
            for(auto&g:groups){auto&v=g.second;std::stable_sort(v.begin(),v.end(),[&](int a,int b){return channels[a].slowMax<channels[b].slowMax;});geometryBuckets[range.first].push_back(std::move(v));}
        }
        if(channels.empty())throw std::invalid_argument("Empty SLL channel inventory");
    }
    std::vector<int> candidates(int from,int to,const std::set<std::string>&used,const std::function<double(const Channel&)>&rank,int incumbent=-1)const {
        const auto&groups=geometryBuckets.at({from,to});std::vector<std::pair<double,size_t>> order;order.reserve(groups.size());
        for(size_t i=0;i<groups.size();++i)order.push_back({rank(channels[groups[i][0]]),i});
        std::sort(order.begin(),order.end());std::vector<int> out;
        if(incumbent>=0&&!conflicts(channels[incumbent],used))out.push_back(incumbent);
        for(const auto&g:order){for(int i:groups[g.second])if(!conflicts(channels[i],used)){if(i!=incumbent)out.push_back(i);break;}if(out.size()>=48)break;}
        return out;
    }
    double delay(Point a,Point b)const {return local?local(a,b):length(a,b,ratio)*.01;}
    const Pin& pin(const Net&n,int id)const {
        if(n.source.id==id)return n.source;
        auto it=n.sinkLookup.find(id);if(it!=n.sinkLookup.end())return n.sinks.at(it->second);
        for(const auto&p:n.sinks)if(p.id==id)return p;
        throw std::runtime_error("Unknown SLL tree pin");
    }
    std::vector<std::vector<Terminal>> localTrees(const Plan&p)const {
        std::vector<std::vector<Terminal>> trees(p.crossings.size()+1);
        trees[0].push_back({p.net.source.id,{},false});
        for(size_t k=0;k<p.crossings.size();++k) {
            const auto&e=p.crossings[k];const auto&c=channels.at(e.channel);
            trees.at(e.parent+1).push_back({-1,c.in,true});
            trees[k+1].push_back({-1,c.out,true});
        }
        for(const auto&s:p.net.sinks) {
            const auto&path=p.paths.at(s.id);
            trees.at(path.empty()?0:path.back()+1).push_back({s.id,{},false});
        }
        return trees;
    }
    double wireCost(const Plan&p)const {
        double value=0;
        for(const auto&tree:localTrees(p)) {
            if(tree.size()<2)continue;
            double x0=INFINITY,x1=-INFINITY,y0=INFINITY,y1=-INFINITY;
            for(const auto&t:tree){Point v=t.isFixed?t.fixed:pin(p.net,t.pin).p;x0=std::min(x0,v.x);x1=std::max(x1,v.x);y0=std::min(y0,v.y);y1=std::max(y1,v.y);}
            value+=x1-x0+ratio*(y1-y0);
        }
        return p.net.weight*value; // Shared local hyperedges counted once.
    }
    double pathDelay(const Plan&p,int sink,Point sourcePoint,Point sinkPoint)const {
        Point cur=sourcePoint;double d=0;
        for(int k:p.paths.at(sink)){const auto&c=channels.at(p.crossings.at(k).channel);d+=delay(cur,c.in)+c.slowMax;cur=c.out;}
        return d+delay(cur,sinkPoint);
    }
    double cost(const Plan&p)const {
        double worst=0,sum=0;
        for(const auto&s:p.net.sinks){double d=pathDelay(p,s.id,p.net.source.p,s.p);worst=std::max(worst,s.criticality*d);sum+=s.criticality*d;}
        int lo=p.net.source.slr,hi=lo;for(const auto&s:p.net.sinks){lo=std::min(lo,s.slr);hi=std::max(hi,s.slr);}
        return wireCost(p)+timingWeight*(worst+sum/std::max(size_t(1),p.net.sinks.size()))+
               extraCrossingCost*(p.crossings.size()-(hi-lo));
    }
    // Greedy capacity-aware tree construction; bounded coordinate improvement
    // follows it. Failure is reported as this allocator's failure, not proof of
    // device infeasibility. Candidate groups never send local sinks off die.
    Plan construct(const Net&n,const std::vector<std::vector<int>>&groups,const std::set<std::string>&occupied)const {
        Plan p;p.net=n;for(const auto&s:n.sinks)p.paths[s.id]={};
        std::set<std::string> used=occupied;
        for(size_t g=0;g<groups.size();++g) {
            if(groups[g].empty())continue;
            const int direction=pin(n,groups[g].front()).slr>n.source.slr?1:-1;
            int last=n.source.slr;
            for(int id:groups[g]){int s=pin(n,id).slr;last=direction>0?std::max(last,s):std::min(last,s);}
            int parent=-1;Point root=n.source.p;
            for(int from=n.source.slr;from!=last;from+=direction) {
                int to=from+direction;std::vector<int> downstream;
                for(int id:groups[g])if(direction>0?pin(n,id).slr>=to:pin(n,id).slr<=to)downstream.push_back(id);
                if(downstream.empty())continue;
                auto range=available.find({from,to});if(range==available.end())throw std::runtime_error("Missing SLL direction");
                const std::string key=std::to_string(n.id)+":"+std::to_string(g)+":"+std::to_string(from)+":"+std::to_string(to);
                double bx0=INFINITY,bx1=-INFINITY,by0=INFINITY,by1=-INFINITY;
                for(int id:downstream){const auto&s=pin(n,id);bx0=std::min(bx0,s.p.x);bx1=std::max(bx1,s.p.x);if(s.slr==to){by0=std::min(by0,s.p.y);by1=std::max(by1,s.p.y);}}
                auto rank=[&](const Channel&c){double v=length(root,c.in,ratio)+std::max(bx1,c.out.x)-std::min(bx0,c.out.x);if(std::isfinite(by0))v+=ratio*(std::max(by1,c.out.y)-std::min(by0,c.out.y));return v;};
                auto oldSlot=previous.find(key);
                auto eligible=candidates(from,to,used,rank,oldSlot==previous.end()?-1:oldSlot->second);
                double best=INFINITY;int selected=-1;
                for(int ci:eligible) {
                    const auto&c=channels[ci];if(conflicts(c,used))continue;
                    double x0=c.out.x,x1=c.out.x,y0=c.out.y,y1=c.out.y,td=0;
                    for(int id:downstream){const auto&s=pin(n,id);x0=std::min(x0,s.p.x);x1=std::max(x1,s.p.x);
                        // Future-die Y is not ordinary wire on this die.
                        if(s.slr==to){y0=std::min(y0,s.p.y);y1=std::max(y1,s.p.y);}
                        td=std::max(td,s.criticality*(delay(root,c.in)+c.slowMax+(s.slr==to?delay(c.out,s.p):std::abs(c.out.x-s.p.x)*.01)));}
                    double score=n.weight*(length(root,c.in,ratio)+x1-x0+ratio*(y1-y0))+timingWeight*td;
                    auto old=previous.find(key);if(old!=previous.end()&&old->second!=ci)score+=switchCost;
                    if(score<best){best=score;selected=ci;}
                }
                if(selected<0)throw std::runtime_error("SLL allocator exhausted compatible channels");
                int k=p.crossings.size();p.crossings.push_back({key,selected,parent,downstream});
                occupy(channels[selected],used);
                for(int id:downstream)p.paths[id].push_back(k);
                parent=k;root=channels[selected].out;
            }
        }
        return p;
    }
    void rebuild(std::vector<Net> nets) {
        for(auto&n:nets){n.sinkLookup.clear();for(size_t i=0;i<n.sinks.size();++i)n.sinkLookup[n.sinks[i].id]=i;}
        std::stable_sort(nets.begin(),nets.end(),[](const Net&a,const Net&b){double wa=1,wb=1;for(auto&s:a.sinks)wa=std::max(wa,s.criticality);for(auto&s:b.sinks)wb=std::max(wb,s.criticality);return wa==wb?a.id<b.id:wa>wb;});
        std::vector<Plan> next;std::set<std::string> occupied=reserved;
        std::map<std::string,int> nextIdentity;
        for(const auto&n:nets) {
            std::vector<std::vector<int>> groups(2);
            for(auto&s:n.sinks)if(s.slr!=n.source.slr)groups[s.slr>n.source.slr?1:0].push_back(s.id);
            if(groups[0].empty()&&groups[1].empty())continue;
            Plan p=construct(n,groups,occupied);
            if(allowSplit)for(int dir=0;dir<2;++dir)if(groups[dir].size()>1) {
                auto ids=groups[dir];std::stable_sort(ids.begin(),ids.end(),[&](int a,int b){return pin(n,a).p.x<pin(n,b).p.x;});
                std::vector<std::vector<int>> alternatives;
                // Spatial and criticality splits are evaluated, not prescribed.
                alternatives.push_back(std::vector<int>(ids.begin(),ids.begin()+ids.size()/2));
                double mx=1;for(int id:ids)mx=std::max(mx,pin(n,id).criticality);
                std::vector<int> critical;for(int id:ids)if(pin(n,id).criticality>=mx*.8)critical.push_back(id);
                if(!critical.empty()&&critical.size()<ids.size())alternatives.push_back(critical);
                for(const auto&first:alternatives) {
                    auto proposal=groups;proposal[dir]=first;std::vector<int> rest;
                    for(int id:ids)if(std::find(first.begin(),first.end(),id)==first.end())rest.push_back(id);
                    proposal.push_back(rest);
                    try {auto candidate=construct(n,proposal,occupied);if(cost(candidate)+switchCost<cost(p))p=std::move(candidate);}
                    catch(const std::runtime_error&){/* Optional split unavailable; shared tree remains valid. */}
                }
            }
            // Coordinate descent measures the complete same-die tree and every
            // sink path, including connections between consecutive crossings.
            for(int pass=0;pass<2;++pass)for(size_t k=0;k<p.crossings.size();++k) {
                auto&e=p.crossings[k];int old=e.channel;const auto base=channels[old];double best=cost(p);
                std::set<std::string> other=occupied;for(size_t j=0;j<p.crossings.size();++j)if(j!=k)occupy(channels[p.crossings[j].channel],other);
                // At most 48 inexpensive geometric candidates plus incumbent.
                double center=0;for(int id:e.sinks)center+=pin(n,id).p.x/e.sinks.size();
                auto near=candidates(base.from,base.to,other,[&](const Channel&c){return length(p.net.source.p,c.in,ratio)+std::abs(c.out.x-center);},old);
                int selected=old;
                for(int ci:near){e.channel=ci;double value=cost(p)+(e.channel==old?0:switchCost);if(value<best){best=value;selected=e.channel;}}
                e.channel=selected;
            }
            for(const auto&e:p.crossings){const auto&c=channels[e.channel];if(conflicts(c,occupied))throw std::runtime_error("SLL collision");occupy(c,occupied);nextIdentity[e.key]=e.channel;}
            next.push_back(std::move(p));
        }
        created=retained=deleted=0;
        for(auto&k:nextIdentity){if(previous.count(k.first))++retained;else ++created;}
        for(auto&k:previous)if(!nextIdentity.count(k.first))++deleted;
        plans=std::move(next);previous=std::move(nextIdentity);
    }
};
}
#endif
