#include "../lib/HiFPlacer/placement/placementInfo/HardRegionConstraints.h"
#include <filesystem>
#include <cassert>
#include <iostream>

int main(int argc,char **argv) {
    assert(argc==2);
    std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    auto defs=out/"regions.tsv",sites=out/"sites.tsv";
    std::ofstream(defs)<<"region\tinstance\nstatic\t*\na\ttop/rp_a\nb\ttop/rp_b\n";
    std::ofstream(sites)<<"region\tsite\nstatic\tS0\nstatic\tS1\na\tA0\na\tA1\nb\tB0\nb\tB1\n";
    std::map<std::string,std::string> cfg{{"IncremenralCompilation","true"},{"DFX placement mode","full"},
        {"DFX regions file",defs.string()},{"DFX region sites file",sites.string()}};
    HardRegionConstraints h(cfg);
    assert(h.enabled && !IncrementalContext(cfg).enabled);
    assert(h.cellRegion("top/rp_a/ff[0]")==1);
    assert(h.cellRegion("top/rp_a_extra/ff")==0);
    assert(h.cellRegion("top/rp_b/ff")==2);
    assert(h.allows(0,"S0") && !h.allows(0,"A0") && !h.allows(1,"B0") && !h.allows(0,"unknown"));
    h.addModeledSite("S0",0,0);h.addModeledSite("S1",100,100);
    h.addModeledSite("A0",10,10);h.addModeledSite("A1",20,20);
    h.addModeledSite("B0",10,50);h.addModeledSite("B1",20,60);h.finishModel();
    float x=90,y=90;h.project(1,0,0,0,0,x,y);assert(x<20.01 && y<20.01);
    x=15;y=19;h.project(1,0,0,0,4,x,y);assert(y<16.01 && y+4<20.01);
    x=15;y=15;h.project(0,0,0,0,0,x,y);assert(x<10 || x>20 || y<10 || y>20);
    x=15;y=18;h.project(0,0,0,-4,4,x,y);assert(x<10 || x>20 || y-4>20 || y+4<10);
    bool failed=false;try{h.project(1,0,0,0,30,x,y);}catch(const std::runtime_error&){failed=true;}assert(failed);
    std::ofstream(sites,std::ios::app)<<"b\tA0\n";
    failed=false;try{HardRegionConstraints bad(cfg);}catch(const std::runtime_error&){failed=true;}assert(failed);
    HardRegionConstraints disabled({});x=400;y=-9;disabled.project(0,0,0,0,0,x,y);assert(x==400 && y==-9);
    std::cout<<"hard-region ownership, projection, macro envelope, prefix and overlap guards passed\n";
}
