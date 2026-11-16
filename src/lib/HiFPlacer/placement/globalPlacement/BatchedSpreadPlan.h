#pragma once
#include "ResidualSpreadSearch.h"
#include <queue>
#include <cstdint>
#include <set>

namespace batched_spread {
using residual_spread::Point;
using residual_spread::Rect;
using residual_spread::each;
using residual_spread::pointBox;
constexpr double eps = 1e-6;

inline Rect unite(Rect a, Rect b) { return a.include({b.l,b.b}).include({b.r,b.t}); }
inline int overlap(Rect a, Rect b) {
    return std::max(0, std::min(a.r,b.r)-std::max(a.l,b.l)+1) *
           std::max(0, std::min(a.t,b.t)-std::max(a.b,b.b)+1);
}
struct Bin {
    double excess=0, room=0, movable=0, capacity=0;
    double donor() const { return std::min(excess,movable); }
    double urgency() const { return capacity>0 ? excess/capacity : std::numeric_limits<double>::infinity(); }
};
struct Window {
    Rect box;
    int origin;
    int target=-1;
};
struct Grid {
    int width;
    std::vector<Bin> bins;
    int index(int x,int y) const { return y*width+x; }
    Point point(int id) const { return {id%width,id/width}; }
    const Bin &at(int x,int y) const { return bins[index(x,y)]; }
};

// Direction comparisons use the signed balance of the whole new strip. A
// second strip is examined only when the adjacent strip cannot reduce debt.
template<class Cold> Rect grow(const Grid &g, Rect box, Rect domain, int limit, Point seed, Cold cold) {
    double debt=0;
    each(box,[&](int x,int y){auto &b=g.at(x,y); debt+=b.excess-b.room;});
    while(debt>eps) {
        bool found=false;
        Rect best=box;
        double bestNet=0, bestGain=0;
        int bestDistance=0, bestCollateral=-1;
        for(int direction=0;direction<4;++direction) {
            Rect candidate=box;
            double net=0;
            int distance=std::numeric_limits<int>::max();
            for(int depth=0;depth<2;++depth) {
                Rect edge=residual_spread::strip(candidate,direction);
                candidate=unite(candidate,edge);
                if(!domain.contains(candidate) || candidate.area()>limit) break;
                each(edge,[&](int x,int y){
                    auto &b=g.at(x,y);
                    net+=b.room-b.excess;
                    if(b.room>eps) distance=std::min(distance,std::abs(x-seed.first)+std::abs(y-seed.second));
                });
                if(net<=eps) continue;
                double gain=std::min(debt,net);
                bool better=!found || gain>bestGain || (gain==bestGain && distance<bestDistance);
                int collateral=-1;
                if(found && gain==bestGain && distance==bestDistance) {
                    collateral=cold(candidate);
                    if(bestCollateral<0) bestCollateral=cold(best);
                    better=std::make_tuple(collateral,candidate.area(),std::max(candidate.width(),candidate.height())) <
                           std::make_tuple(bestCollateral,best.area(),std::max(best.width(),best.height()));
                }
                if(better) {
                    found=true; best=candidate; bestNet=net; bestGain=gain;
                    bestDistance=distance; bestCollateral=collateral;
                }
                break;
            }
        }
        if(!found) break;
        debt-=bestNet; box=best;
    }
    return box;
}

// Enumerate only spatial neighbors. Stale edges in the priority queue are
// invalidated by retiring their endpoints after each merge.
template<class Regrow> std::vector<Window> merge(std::vector<Window> windows,int width,int height,
                                                 int limit,Regrow regrow) {
    struct Edge {
        int a,b,intersection,area,first,second;
        bool operator<(const Edge &o) const {
            int64_t lhs=int64_t(intersection)*o.area,rhs=int64_t(o.intersection)*area;
            return lhs!=rhs ? lhs<rhs : std::tie(first,second)>std::tie(o.first,o.second);
        }
    };
    std::priority_queue<Edge> queue;
    std::vector<std::vector<int>> owners(width*height);
    std::vector<char> live(windows.size(),true);
    std::vector<int> seen(windows.size(),-1);
    auto insert=[&](int i) {
        auto &cur=windows[i];
        each(cur.box,[&](int x,int y){
            auto &ids=owners[y*width+x];
            for(int j:ids) {
                if(!live[j] || seen[j]==i) continue;
                seen[j]=i;
                auto &other=windows[j];
                int intersection=overlap(cur.box,other.box), area=unite(cur.box,other.box).area();
                if(2*intersection>=std::min(cur.box.area(),other.box.area()) && area<=limit)
                    queue.push({j,i,intersection,area,std::min(cur.origin,other.origin),std::max(cur.origin,other.origin)});
            }
            ids.push_back(i);
        });
    };
    for(int i=0;i<int(windows.size());++i) insert(i);
    while(!queue.empty()) {
        auto e=queue.top();queue.pop();
        if(!live[e.a] || !live[e.b]) continue;
        live[e.a]=live[e.b]=false;
        const int origin=e.first;
        Rect box=regrow(unite(windows[e.a].box,windows[e.b].box),origin);
        windows.push_back({box,origin}); live.push_back(true);seen.push_back(-1);
        insert(int(windows.size()-1));
    }
    std::vector<Window> result;
    for(size_t i=0;i<windows.size();++i) if(live[i]) result.push_back(windows[i]);
    return result;
}

// Every rectangle containing seed and any receiver contains their minimum
// enclosing rectangle. Used for the exhaustive local-room check before escape.
template<class F> void nearby(Point seed,Rect domain,int limit,F visit) {
    for(int y=std::max(domain.b,seed.second-limit+1);y<=std::min(domain.t,seed.second+limit-1);++y) {
        int reach=limit/(1+std::abs(y-seed.second))-1;
        for(int x=std::max(domain.l,seed.first-reach);x<=std::min(domain.r,seed.first+reach);++x)
            visit(x,y);
    }
}

struct Ranked {
    Window window;
    int peak, age, collateral;
    double urgency, zeroDebt, relief;
};
// Order peaks first, then compare candidates for the same peak. A two-pass
// ordering keeps the comparator transitive even when different peaks tie.
inline bool earlier(const Ranked &a,const Ranked &b) {
    if(a.urgency!=b.urgency) return a.urgency>b.urgency;
    if(a.zeroDebt!=b.zeroDebt) return a.zeroDebt>b.zeroDebt;
    if(a.peak!=b.peak) return a.peak<b.peak;
    if(a.relief!=b.relief) return a.relief>b.relief;
    if(a.collateral!=b.collateral) return a.collateral<b.collateral;
    if(a.age!=b.age) return a.age>b.age;
    return a.window.origin<b.window.origin;
}
}
