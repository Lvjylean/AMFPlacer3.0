#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

namespace residual_spread {
using Point = std::pair<int,int>;
constexpr int fallbackArea = 100;
struct Rect {
    int l,r,b,t;
    int width() const { return r-l+1; }
    int height() const { return t-b+1; }
    int area() const { return width()*height(); }
    Rect include(Point p) const {
        return {std::min(l,p.first),std::max(r,p.first),std::min(b,p.second),std::max(t,p.second)};
    }
    bool contains(Rect a) const { return l<=a.l && r>=a.r && b<=a.b && t>=a.t; }
    bool operator==(Rect a) const { return l==a.l && r==a.r && b==a.b && t==a.t; }
};
template<class F> void each(Rect a, F f) {
    for(int y=a.b;y<=a.t;++y) for(int x=a.l;x<=a.r;++x) f(x,y);
}
inline Rect pointBox(Point p) {return {p.first,p.first,p.second,p.second};}

struct Account {
    double room;         // Signed effective margin for receivers, actual nominal overflow for hot bins.
    bool potential;      // Conservative upper bound on receiver eligibility, including reserved FF demand.
    bool occupied;       // Another group owns this bin in this inner pass.
};
struct Plan {
    Rect box;
    double balance;
    bool receiver;
};
template<class Read> Plan account(Rect box,Read read) {
    Plan p{box,0,false};
    each(box,[&](int x,int y){auto b=read(x,y);p.balance+=b.room;p.receiver|=b.room>0;});
    return p;
}
inline Rect strip(Rect box,int dir) {
    if(dir==0) return {box.l-1,box.l-1,box.b,box.t};
    if(dir==1) return {box.r+1,box.r+1,box.b,box.t};
    if(dir==2) return {box.l,box.r,box.b-1,box.b-1};
    return {box.l,box.r,box.t+1,box.t+1};
}
template<class Read> bool available(Rect a,Read read) {
    bool free=true;
    each(a,[&](int x,int y){free &= !read(x,y).occupied;});
    return free;
}

// Follow only the four boundary strips. A neutral strip can bridge to a receiver.
// Each ray stops when the measured deficit has been covered or its stage budget ends.
template<class Read> Plan grow(Rect core,Rect domain,int budget, Read read,const Rect *avoid=nullptr) {
    Plan current=account(core,read);
    while(current.balance<0 || !current.receiver || (avoid && current.box==*avoid)) {
        Plan best=current;
        using Rank=std::tuple<double,int,int,int,int>;
        Rank bestRank{};
        bool found=false;
        for(int dir=0;dir<4;++dir) {
            Plan ray=current;
            for(;;) {
                auto edge=strip(ray.box,dir);
                if(!domain.contains(edge) || ray.box.area()+edge.area()>budget || !available(edge,read)) break;
                auto added=account(edge,read);
                ray.box=ray.box.include({edge.l,edge.b}).include({edge.r,edge.t});
                ray.balance+=added.balance;
                ray.receiver |= added.receiver;
                const double gain=std::min(ray.balance,0.)-std::min(current.balance,0.);
                const int cost=ray.box.area()-current.box.area();
                if((gain>0 || (avoid && current.box==*avoid && added.balance>0)) &&
                   !(avoid && ray.box==*avoid)) {
                    Rank rank{gain/cost,-std::max(ray.box.width(),ray.box.height()),-cost,-dir,0};
                    if(!found || rank>bestRank){best=ray;bestRank=rank;found=true;}
                }
                if(ray.balance>=0 && ray.receiver && !(avoid && ray.box==*avoid)) break;
            }
        }
        if(!found) break;
        current=best;
    }
    return current;
}

// A receiver and a source can share a <=100-bin rectangle iff their smallest
// bounding rectangle fits. Scan receiver bins, never movable objects or all rectangles.
// Claimed receivers still bar a cross-die fallback. Only the planning pass uses claims.
template<class Read> bool potentialNearby(Point p, Rect domain,Read read) {
    for(int y=std::max(domain.b,p.second-fallbackArea+1);y<=std::min(domain.t,p.second+fallbackArea-1);++y) {
        int reach=fallbackArea/(1+std::abs(y-p.second))-1;
        for(int x=std::max(domain.l,p.first-reach);x<=std::min(domain.r,p.first+reach);++x) {
            if(Point{x,y}!=p && read(x,y).potential) return true;
        }
    }
    return false;
}
}
