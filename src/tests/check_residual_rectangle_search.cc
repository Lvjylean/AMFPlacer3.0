#include "../lib/HiFPlacer/placement/globalPlacement/ResidualSpreadSearch.h"
#include <iostream>
#include <stdexcept>
using namespace residual_spread;
void check(bool ok) { if(!ok) throw std::runtime_error("bin planner mismatch"); }

int main() {
    // Same physical grid, three different residuals. A zero-capacity gap can be bridged.
    Rect line{0,5,0,0};
    for(auto spec:{std::make_pair(45.,2),std::make_pair(90.,3),std::make_pair(150.,4)}) {
        const double supply[]={-spec.first,0,55,50,50,0};
        auto read=[&](int x,int){return Account{supply[x],supply[x]>0,false};};
        auto selected=grow(pointBox({0,0}),line,6,read);
        check(selected.box.r==spec.second && selected.balance>=0);
    }
    // A normal stage budget can exceed 100. Only the fallback eligibility test uses 100.
    Rect longRow{0,120,0,0};
    auto far=[](int x,int){return Account{x==0?-12.:x==110?15.:0.,x==110,false};};
    check(grow(pointBox({0,0}),longRow,120,far).box.r==110);
    check(!potentialNearby({0,0},longRow,far));
    // Inclusive area and a diagonal pocket, already claimed by another region.
    for(Point p: {Point{9,9},Point{10,9},Point{99,0},Point{100,0}}) {
        auto occupied=[&](int x,int y){bool match=Point{x,y}==p;return Account{match?2.:0.,match,match};};
        check(potentialNearby({0,0},{0,120,0,15},occupied)==((p.first+1)*(p.second+1)<=100));
    }
    std::cout<<"PASS residual sizing, bridging, independent stage budget and local fallback guard\n";
}
