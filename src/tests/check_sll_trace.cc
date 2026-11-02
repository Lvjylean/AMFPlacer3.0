#include "../lib/HiFPlacer/placement/placementInfo/SllVisualizationTrace.h"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace amf_sll_plan;
std::string read(const std::string& path) {
    auto f=gzopen(path.c_str(),"rb");assert(f);char data[1024];std::string s;int n;
    while((n=gzread(f,data,sizeof(data)))>0)s.append(data,n);
    assert(n==0);assert(gzclose(f)==Z_OK);return s;
}
int main(int argc,char**argv) {
    assert(argc==2);std::filesystem::create_directories(argv[1]);
    std::string prefix=std::string(argv[1])+"/DumpSllTrace-";
    assert(amf_sll_trace::writeSnapshot(nullptr,prefix+"0.tsv.gz")==0);
    assert(read(prefix+"0.tsv.gz").find("#state\tuninitialized")!=std::string::npos);
    Model model;Tile tile;tile.from=0;tile.to=1;tile.column=71;
    tile.in={10,180};tile.out={11,240};tile.key={0,71,180};
    model.tiles.push_back(tile);model.tileCapacity[tile.key]=2;
    Plan plan;plan.net.id=7;plan.net.name="top/n[1]";plan.net.source.name="driver/O";
    Crossing crossing;crossing.key="7:0:0:1";crossing.tile=0;plan.crossings.push_back(crossing);
    model.plans.push_back(plan);model.previous[crossing.key]=0;
    assert(amf_sll_trace::writeSnapshot(&model,prefix+"1.tsv.gz")==1);
    assert(model.plans[0].crossings[0].channel==-1 && model.previous.at(crossing.key)==0);
    assert(read(prefix+"1.tsv.gz").find("\t10\t180\t11\t240\t2\t\n")!=std::string::npos);
    model.tiles[0].column=110;model.tiles[0].in.x=20;model.tiles[0].out.x=21;
    model.tileCapacity[{0,110,180}]=2;
    amf_sll_trace::writeSnapshot(&model,prefix+"2.tsv.gz");
    assert(read(prefix+"2.tsv.gz").find("\t110\t20\t180\t21\t240\t2\t\n")!=std::string::npos);
    model.plans.clear();amf_sll_trace::writeSnapshot(&model,prefix+"3.tsv.gz");
    assert(read(prefix+"3.tsv.gz").find("#state\trecorded")!=std::string::npos);
    assert(read(prefix+"3.tsv.gz").find("#count\t0")!=std::string::npos);
    assert(amf_sll_trace::escape("a\tb\\c\nd")=="a\\tb\\\\c\\nd");
    std::cout<<"SLL trace read-only birth/move/disappearance, paired coordinates and no channel legalization passed\n";
}
