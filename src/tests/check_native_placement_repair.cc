#include "../lib/HiFPlacer/designInfo/native/PlacementRepair.h"
#include <iostream>
using R=amf_native::PlacementRepair;
int main(){try{
    int checks=0;
    auto check=[&](bool x){++checks;if(!x)throw std::runtime_error("test assertion failed");};
    auto reject=[&](auto f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);};
    R::Table base{{"a/dsp","DSP48E2_X1Y47/DSP_ALU"},{"a/neighbor","DSP48E2_X1Y46/DSP_ALU"},{"a/lut","SLICE_X1Y2/A6LUT"}};
    R::Table types{{"a/dsp","DSP48E2"},{"a/neighbor","DSP48E2"},{"a/lut","LUT1"}};
    std::set<std::string> allowed{"DSP48E2_X1Y43"},blocked,cascade;
    R::AccessMap old,newer;
    for(int i=0;i<4;++i){std::string cell=i==0?"a/neighbor":"a/dsp",pin="P"+std::to_string(i),net="n"+std::to_string(i);
        old[cell+"/"+pin]={cell,i==0?"DSP48E2_X1Y46":"DSP48E2_X1Y47",pin,net,i==3?"GROUND":"SIGNAL",{"m1","m2","m3"}};
        newer[cell+"/"+pin]=old.at(cell+"/"+pin);
        if(i){newer.at(cell+"/"+pin).site="DSP48E2_X1Y43";newer.at(cell+"/"+pin).resources={"r1","r2","r3","r4"};}}
    auto call=[&]{return R::plan(base,types,allowed,blocked,cascade,"a/dsp","DSP48E2_X1Y47/DSP_ALU","DSP48E2_X1Y43/DSP_ALU",old,newer);};
    auto result=call();check(result.originalMatched==3&&result.repairedMatched==4);
    check(result.placement.at("a/dsp")=="DSP48E2_X1Y43/DSP_ALU");
    check(result.placement.at("a/lut")==base.at("a/lut")&&result.placement.at("a/neighbor")==base.at("a/neighbor"));
    blocked.insert("DSP48E2_X1Y43");reject(call);blocked.clear();
    cascade.insert("a/dsp");reject(call);cascade.clear();
    allowed.clear();reject(call);allowed.insert("DSP48E2_X1Y43");
    base.at("a/neighbor")="DSP48E2_X1Y43/DSP_ALU";reject(call);base.at("a/neighbor")="DSP48E2_X1Y46/DSP_ALU";
    auto saved=newer;newer.erase(newer.begin());reject(call);newer=saved;
    newer.begin()->second.net="changed";reject(call);newer=saved;
    for(auto &p:newer)p.second.resources={"m1","m2","m3"};reject(call);newer=saved;
    newer.begin()->second.resources.insert("INT_X0Y0/VCC_WIRE");reject(call);newer=saved;
    types.at("a/dsp")="LUT1";reject(call);types.at("a/dsp")="DSP48E2";
    base.at("a/dsp")="DSP48E2_X1Y45/DSP_ALU";reject(call);base.at("a/dsp")="DSP48E2_X1Y47/DSP_ALU";
    auto shared=old;for(auto &p:shared)p.second.net="one_ground";check(R::match(shared)==1);
    std::cout<<"native placement repair: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
