#include "../lib/HiFPlacer/designInfo/native/FfPinAccessRepair.h"
#include <iostream>
using R=amf_native::FfPinAccessRepair;
int main(){try {
 int n=0;auto check=[&](bool v){++n;if(!v)throw std::runtime_error("assertion failed");};
 auto rejects=[&](auto f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);};
 R::Table base{{"rp/a","SLICE_X3Y4/CFF2"},{"rp/b","SLICE_X3Y4/AFF2"},{"rp/c","SLICE_X3Y4/BFF2"},{"rp/d","SLICE_X3Y4/DFF2"},{"rp/lut","SLICE_X3Y4/A6LUT"}};
 R::Table types{{"rp/a","FDRE"},{"rp/b","FDRE"},{"rp/c","FDRE"},{"rp/d","FDRE"},{"rp/lut","LUT6"}},access;
 for(char c='A';c<='H';++c)for(auto suffix:{std::string("FF"),std::string("FF2")})access["SLICE_X3Y4/"+std::string(1,c)+suffix]="NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP";
 access["SLICE_X3Y4/CFF2"]="ALL_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA";
 std::set<std::string> allowed{"SLICE_X3Y4"},blocked;
 auto run=[&]{return R::plan(base,types,allowed,blocked,access);};
 auto result=run();check(result.from.size()==1);check(result.to.at("rp/a")=="SLICE_X3Y4/CFF");check(result.placement.at("rp/lut")==base.at("rp/lut"));
 access["SLICE_X3Y4/CFF"]="PARTIAL_STATIC_FIRST_HOP_BLOCKAGE";result=run();check(result.to.at("rp/a")=="SLICE_X3Y4/BFF");
 auto old=base;base["rp/new"]="SLICE_X3Y4/AFF";types["rp/new"]="FDRE";rejects(run);base=old;types.erase("rp/new");
 blocked.insert("SLICE_X3Y4");rejects(run);blocked.clear();
 auto saved=access;access.erase("SLICE_X3Y4/HFF2");rejects(run);access=saved;
 types["rp/a"]="FDSE";rejects(run);types["rp/a"]="FDRE";
 for(char c='A';c<='D';++c)access["SLICE_X3Y4/"+std::string(1,c)+"FF"]="BOUNDARY_NET_DEPENDENT";rejects(run);access=saved;
 access["SLICE_X3Y4/CFF2"]="NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP";result=run();check(result.placement==base&&result.from.empty());
 std::cout<<"Native FF pin repair: "<<n<<" checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
