#include "../lib/HiFPlacer/designInfo/native/BoundarySitePolicy.h"
#include "../lib/HiFPlacer/designInfo/native/StaticDesignCache.h"
#include "../lib/HiFPlacer/placement/placementInfo/IncrementalContext.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace amf_native;
namespace fs=std::filesystem;
int checks=0;
void ck(bool b,const std::string&s){++checks;if(!b)throw std::runtime_error(s);}
void text(const fs::path&p,const std::string&s){std::ofstream f(p);f<<s;}
std::string read(const fs::path&p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void reject(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}ck(bad,"expected rejection");}
int main(){try {
 auto p=fs::temp_directory_path()/("amf-edge-test-"+std::to_string(getpid()));fs::create_directory(p);
 const std::string header="region\tsite\ttype\tclock_region\tprohibit\n";
 std::string rows=header,rs="region\tsite\n";std::set<std::string> sites;
 for(int y=0;y<6;++y)for(int x=0;x<3;++x){auto n="SLICE_X"+std::to_string(x)+"Y"+std::to_string(y);sites.insert(n);
  rows+="a\t"+n+"\t"+(x==1?"SLICEM":"SLICEL")+"\tX0Y"+std::to_string(y/3)+"\t"+(x==0&&y==0?"1":"0")+"\n";}
 for(int y=0;y<4;++y){auto n="DSP48E2_X0Y"+std::to_string(y);sites.insert(n);rows+="a\t"+n+"\tDSP48E2\tX0Y"+std::to_string(y/2)+"\t0\n";}
 for(int y=0;y<2;++y){auto n="RAMB36_X0Y"+std::to_string(y);sites.insert(n);rows+="a\t"+n+"\tRAMBFIFO36\tX0Y"+std::to_string(y)+"\t"+(y==0?"1":"0")+"\n";}
 for(auto&s:sites)rs+="a\t"+s+"\n";
 text(p/"inventory",rows);text(p/"geometry","region\tclock_regions\na\tCLOCKREGION_X0Y0:CLOCKREGION_X0Y1\n");
 std::map<std::string,std::set<std::string>> allowed{{"a",sites}};
 auto derive=[&](const std::string&side="interior-prohibit"){return BoundarySitePolicy::derive((p/"inventory").string(),(p/"geometry").string(),allowed,side);};
 auto r=derive();ck(r.margin["a"].size()==8,"six outer SLICEs plus two outer DSPs");
 ck(!r.margin["a"].count("SLICE_X0Y2")&&!r.margin["a"].count("SLICE_X0Y3"),"internal clock-region seam retained");
 ck(!r.margin["a"].count("SLICE_X0Y1"),"corner prohibition does not expand side column");
 ck(r.prohibited["a"].count("RAMB36_X0Y0")&&!r.margin["a"].count("RAMB36_X0Y1"),"real BRAM prohibition honored; no BRAM margin");
 ck(derive("any-prohibit").margin["a"].count("SLICE_X0Y1"),"explicit corner-inclusive alternative");
 auto more=rows;auto q=more.find("a\tSLICE_X0Y1\tSLICEL\tX0Y0\t0");ck(q!=std::string::npos,"fixture");more.replace(q, std::string("a\tSLICE_X0Y1\tSLICEL\tX0Y0\t0").size(),"a\tSLICE_X0Y1\tSLICEL\tX0Y0\t1");text(p/"inventory",more);
 ck(derive().margin["a"].count("SLICE_X0Y2"),"actual side interior PROHIBIT activates column");text(p/"inventory",rows);
 text(p/"geometry","region\tclock_regions\na\tSLICE_X0Y0:SLICE_X2Y5\n");reject([&]{derive();});
 text(p/"geometry","region\tclock_regions\na\tCLOCKREGION_X0Y0 CLOCKREGION_X1Y1\n");reject([&]{derive();});
 text(p/"geometry","region\tclock_regions\na\tCLOCKREGION_X0Y0:CLOCKREGION_X0Y1\n");
 text(p/"inventory",rows+"a\tSLICE_X0Y0\tSLICEL\tX0Y0\t1\n");reject([&]{derive();});
 text(p/"inventory",header);reject([&]{derive();});text(p/"inventory",rows);
 reject([&]{derive("guess");});
 // Integration: actual cache roundtrip and production IncrementalContext.
 text(p/"regions","region\tinstance\nstatic\t*\na\ttop/a\n");text(p/"sites",rs);
 text(p/"ports","module\tport\tdirection\tdriver\tclock\na\tx\tIN\ts/O\t0\n");
 Config c{{"Native static cache write",(p/"draft").string()},{"Native static identity","fixture"},{"device","U250"},{"ClockPeriod","20"},
 {"Native full input sha256",std::string(64,'1')},{"Native source sha256",std::string(64,'2')},{"DFX regions file",(p/"regions").string()},
 {"DFX region sites file",(p/"sites").string()},{"Native full ports file",(p/"ports").string()}};
 StaticDesignCache cache(c);cache.observeCell("s","LUT1");cache.observePin({"s/O","O","OUT","n","s/O"});cache.finishCapture();
 text(p/"place","[catch {place_cell {s SLICE_X2Y0/A6LUT}}]\n");cache.savePlacement((p/"place").string(),c);
 text(p/"positions","cell\tsite\tbel\ns\tSLICE_X2Y0\tA6LUT\n");
 Config contract{{"identity","fixture"},{"device","U250"},{"clock_period","20"},{"static_shell_sha256",std::string(64,'3')},
 {"static_connectivity_verified","1"},{"interfaces_verified","1"}};
 cache.seal((p/"positions").string(),contract,true);cache.write((p/"old.bin").string());auto old=read(p/"old.bin");
 std::vector<Cell> dynamic{{"d","LUT1","","",{{"d/I0","I0","IN","x","@PORT/x"}},"",false}};
 std::vector<Port> ports{{"a","x","IN","@PORT/x",false}};
 Config inc{{"Native static identity","fixture"},{"Native static shell sha256",std::string(64,'3')},{"device","U250"},{"ClockPeriod","20"},
 {"Native workspace",(p/"default").string()}};
 auto cfg=inc;cache.compose("a",dynamic,ports,cfg);auto nc=cfg;nc["IncremenralCompilation"]="false";
 IncrementalContext normal(nc);ck(!normal.enabled,"ordinary placement unaffected");
 cfg=inc;cfg["Native DFX edge policy"]=BoundarySitePolicy::version;cfg["Native workspace"]=(p/"missing").string();reject([&]{cache.compose("a",dynamic,ports,cfg);});
 contract["resource_inventory_file"]=(p/"inventory").string();contract["resource_geometry_file"]=(p/"geometry").string();
 contract["resource_inventory_sha256"]=std::string(64,'4');contract["resource_geometry_sha256"]=std::string(64,'5');contract["resource_side_policy"]="interior-prohibit";
 auto wrong=contract;wrong["static_shell_sha256"]=std::string(64,'9');reject([&]{auto x=cache;x.certifyResources(wrong);});
 cache.certifyResources(contract);cache.write((p/"child.bin").string());cache=StaticDesignCache::load((p/"child.bin").string());
 ck(read(p/"old.bin")==old,"parent cache immutable");
 ck(old.substr(0,11)=="AMFSTATIC2\n"&&read(p/"child.bin").substr(0,11)=="AMFSTATIC3\n","new resource cache cannot be consumed by unaware old binaries");reject([&]{cache.certifyResources(contract);});
 cfg=inc;cfg["Native DFX edge policy"]=BoundarySitePolicy::version;cfg["Native workspace"]=(p/"on").string();auto prepared=cache.compose("a",dynamic,ports,cfg);
 IncrementalContext ctx(cfg);ck(!ctx.siteAllowed("SLICE_X1Y0")&&!ctx.siteAllowed("DSP48E2_X0Y3"),"production mask excludes both families");
 ck(ctx.siteAllowed("SLICE_X0Y2")&&ctx.siteAllowed("DSP48E2_X0Y1")&&ctx.siteAllowed("RAMB36_X0Y1"),"interior and BRAM retained");
 ck(!ctx.siteAllowed("RAMB36_X0Y0"),"actual prohibition retained independent of margin");
 ck(ctx.fixed.at("s").site=="SLICE_X2Y0","fixed static cell at margin never moved");
 ck(prepared.fixedCells==1&&prepared.dynamicCells==1,"graph and fixed context retained");
 cfg=inc;cfg["Native workspace"]=(p/"off").string();cache.compose("a",dynamic,ports,cfg);IncrementalContext off(cfg);
 ck(off.siteAllowed("SLICE_X1Y0")&&off.siteAllowed("DSP48E2_X0Y3"),"certified cache margin defaults off");
 ck(!off.siteAllowed("SLICE_X0Y0"),"actual prohibition remains a hard legality constraint");
 std::cout<<"boundary site policy: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
