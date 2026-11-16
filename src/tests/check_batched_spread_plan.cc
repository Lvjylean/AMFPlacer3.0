#include "BatchedSpreadPlan.h"
#include <cassert>
#include <iostream>
using namespace batched_spread;
int main() {
    auto cold=[](Rect){return 0;};
    Grid g{8,std::vector<Bin>(8)};
    g.bins[3]={4,0,4,2};g.bins[2].excess=1;g.bins[1].room=5;
    Rect domain{0,7,0,0}, seed{3,3,0,0}, expected{1,3,0,0};
    assert(grow(g,seed,domain,3,{3,0},cold)==expected);
    assert(grow(g,seed,domain,2,{3,0},cold)==seed);
    g.bins[1].room=0;g.bins[0].room=9;
    assert(grow(g,seed,domain,8,{3,0},cold)==seed);
    bool seen=false;nearby({3,0},domain,4,[&](int x,int){seen|=x==0;});assert(seen);
    seen=false;nearby({3,0},domain,3,[&](int x,int){seen|=x==0;});assert(!seen);
    std::vector<Window> chain{{{0,2,0,0},0},{{1,3,0,0},1},{{2,4,0,0},2}};
    std::vector<int> order;
    auto record=[&](Rect r,int s){order.push_back(s);return r;};
    auto merged=merge(chain,8,1,5,record);
    assert(merged.size()==1 && merged.front().box.area()==5 && order.front()==0);
    assert(merge(chain,8,1,4,record).size()==2);
    assert(merge({{{0,2,0,0},0},{{2,4,0,0},2}},8,1,8,record).size()==2);
    // Re-account corners of an L-shaped overlap, then regrow the union.
    Grid grid{4,std::vector<Bin>(12)};grid.bins[0].excess=5;grid.bins[0].movable=5;
    grid.bins[3].room=12;grid.bins[10].excess=5;
    auto m=merge({{{0,2,0,1},0},{{1,2,0,2},1}},4,3,12,[&](Rect r,int i){
        return grow(grid,r,{0,3,0,2},12,grid.point(i),cold);
    });
    assert(m.size()==1 && m.front().box.area()==12);
    // Cover every admissible receiver, including a diagonal and the stage cutoff.
    for(int limit:{25,100}) {
        std::set<Point> hit;
        nearby({7,6},{0,20,0,20},limit,[&](int x,int y){hit.insert({x,y});});
        for(int y=0;y<=20;++y) for(int x=0;x<=20;++x)
            assert(bool(hit.count({x,y}))==((1+std::abs(x-7))*(1+std::abs(y-6))<=limit));
    }
    std::cout<<"batch geometry, competition, stage budget and local receiver checks passed\n";
}
