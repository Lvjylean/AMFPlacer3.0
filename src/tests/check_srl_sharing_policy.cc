#include "SRLSiteSharing.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace srl_site_sharing;
    Layout state,next;
    for(int i=0;i<4;++i) {
        Unit u;u.id=i;u.srlCount=1;u.controls="clk/ce/default";u.bels={"H6LUT"};
        assert(append(u,state,next));state=next;
    }
    assert(state.occupied.size()==4 && state.srlCount==4);
    Unit fifth;fifth.id=5;fifth.srlCount=1;fifth.controls="clk/ce/default";fifth.bels={"A6LUT"};
    assert(!append(fifth,state,next));
    Layout chainLayout;Unit chain;chain.id=10;chain.srlCount=3;chain.controls=fifth.controls;
    chain.bels={"C6LUT","B6LUT","A6LUT"};chain.externalQ31={2};
    assert(append(chain,{},chainLayout));
    assert(append(fifth,chainLayout,next));assert(next.srlCount==4);
    assert(next.assignments.at(10)==chain.bels);
    assert(next.assignments.at(5)[0]=="D6LUT");
    Unit incompatible=fifth;incompatible.controls="other_clk/ce/default";
    assert(!append(incompatible,chainLayout,next));
    incompatible.controls="clk/other_ce/default";assert(!append(incompatible,chainLayout,next));
    incompatible.controls="clk/ce/inverted";assert(!append(incompatible,chainLayout,next));
    fifth.externalQ31={0};assert(!append(fifth,chainLayout,next));
    Unit mux; mux.id=20;mux.srlCount=2;mux.controls=chain.controls;mux.bels={"A6LUT","B6LUT","F7MUX_AB"};
    Layout m;assert(append(mux,{},m));mux.id=21;assert(append(mux,m,next));
    assert(next.assignments.at(21)==std::vector<std::string>({"C6LUT","D6LUT","F7MUX_CD"}));
    assert(!append(mux,next,m));
    // Exhaustive pair shifts: legal layouts keep distinct BELs and never move
    // an externally used Q31 away from LUT A.
    unsigned checked=0;
    for(char a='A';a<='H';++a)for(char b='A';b<='H';++b)for(int ext=0;ext<2;++ext) {
        Unit u;u.id=1;u.srlCount=1;u.controls="same";u.bels={std::string(1,a)+"6LUT"};
        Unit v=u;v.id=2;v.bels={std::string(1,b)+"6LUT"};if(ext)v.externalQ31={0};
        Layout l,o;assert(append(u,{},l));
        bool ok=append(v,l,o);
        if(ok) {assert(o.occupied.size()==2);if(ext)assert(o.assignments.at(2)[0]=="A6LUT");}
        else assert(ext && a=='A');
        ++checked;
    }
    std::cout<<"PASS SRL sharing policy, exhaustive pairs="<<checked<<" and capacity/control/cascade/MUX checks\n";
}
