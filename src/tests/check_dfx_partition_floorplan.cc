#include "SAPlacer.h"
#include "HardRegionConstraints.h"
#include "GraphPartitioner.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool b,const std::string &m){if(!b)throw std::runtime_error(m);}
void write(const fs::path &p,const std::string &s){std::ofstream f(p);f<<s;}
template<class F>void reject(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad,"expected rejection");}
int main(){try {
 auto dir=fs::temp_directory_path()/("dfx-floorplan-test-"+std::to_string(getpid()));fs::create_directory(dir);
 std::string inventory="region\tsite\ttype\tclock_region\tprohibit\n",mask="region\tsite\nstatic\tSLICE_X9Y9\n";
 for(const std::string r:{"a","b"})for(int x=0;x<2;++x)for(int y=0;y<5;++y) {
   auto name="SLICE_X"+std::to_string(x+(r=="b"?2:0))+"Y"+std::to_string(y);
   mask+=r+"\t"+name+"\n";
   inventory+=r+"\t"+name+"\tSLICEL\tX"+(r=="b"?"1":"0")+"Y0\t"+(x==0&&y==0?"1":"0")+"\n";
 }
 write(dir/"regions","region\tinstance\nstatic\t*\na\ttop/a\nb\ttop/b\n");
 write(dir/"sites",mask);write(dir/"inventory",inventory);
 write(dir/"geometry","region\tclock_regions\na\tCLOCKREGION_X0Y0\nb\tCLOCKREGION_X1Y0\n");
 std::map<std::string,std::string> cfg{
   {"IncremenralCompilation","true"},{"DFX placement mode","full"},
   {"DFX regions file",(dir/"regions").string()},{"DFX region sites file",(dir/"sites").string()}};
 HardRegionConstraints old(cfg);check(old.allows(1,"SLICE_X0Y0"),"default full mode unchanged");
 check(old.cellRegion("top/a/lut")==1&&old.cellRegion("top/aa/lut")==0&&old.cellRegion("top/b/carry")==2,"hierarchy-boundary ownership");
 auto on=cfg;on["DFX partition floorplan"]="true";
 reject([&]{HardRegionConstraints missing(on);});
 on["DFX resource inventory file"]=(dir/"inventory").string();on["DFX resource geometry file"]=(dir/"geometry").string();
 on["DFX boundary audit file"]=(dir/"audit").string();on["DFX effective sites file"]=(dir/"effective").string();
 HardRegionConstraints hard(on);
 check(!hard.allows(1,"SLICE_X0Y0")&&!hard.allows(2,"SLICE_X3Y4"),"first full blocks both outer rows");
 check(hard.allows(1,"SLICE_X0Y1")&&hard.allows(1,"SLICE_X0Y3"),"corner does not remove side column");
 check(hard.siteOwners.size()==old.siteOwners.size(),"raw ownership is not shrunk");
 auto wrong=on;wrong["DFX placement mode"]="incremental";reject([&]{HardRegionConstraints h(wrong);});
 using CU=PlacementInfo::ClusterUnit;using CN=PlacementInfo::ClusterNet;
 std::vector<CU*> nodes;std::vector<CN*> nets;
 for(int i=0;i<4;++i)nodes.push_back(new CU(i));
 CN net(0);for(auto n:nodes)net.addClusterUnit(n);nets.push_back(&net);
 GraphPartitioner<std::vector<CU*>,std::vector<CN*>> part(nodes,nets,100,1,false);
 part.solveSubset({2,3},96,24);check(part.getClustersPUIdSets()==std::vector<std::set<int>>{{2,3}},"protected node IDs excluded");
 part.solveSubset({},96,24);check(part.getClusters().empty(),"empty static subset");
 reject([&]{part.solveSubset({2,2},96,24);});reject([&]{part.solveSubset({4},96,24);});
 for(auto n:nodes)delete n;
 std::vector<std::vector<float>> edges{{0}},terminals{{2.f/5.f}};
 std::vector<std::vector<int>> enhanced;std::vector<float> weights{1},fx{11.25f},fy{2};
 SAPlacer sa("dfx-test",edges,enhanced,weights,terminals,fx,fy,1,3,4,30,5,1,200,1,2);
 sa.setPhysicalGrid({{1,2},{11,2},{21,2}},{true,false,true});
 std::vector<std::vector<std::vector<int>>> grid(1,std::vector<std::vector<int>>(3));grid[0][0]={0};
 check(std::fabs(sa.evaluateClusterPlacement(grid,{{0,0}})-20.5)<1e-5,"RP edge weight unchanged");
 fx[0]=20.75;
 sa.solve();check(sa.getCluster2XY()[0]==std::make_pair(2,0),"RP connectivity steers static cluster; excluded bin unused");
 SAPlacer single("single-bin",edges,enhanced,weights,terminals,fx,fy,1,1,4,30,5,1,20,1,1);
 single.setPhysicalGrid({{1,2}},{true});single.solve();
 check(single.getCluster2XY()[0]==std::make_pair(0,0),"single bin terminates");
 reject([&]{sa.setPhysicalGrid({{1,2},{11,2},{21,2}},{false,false,false});});
 std::vector<std::vector<float>> e{{0,1},{1,0}},noFixed(2);std::vector<float> w{1,1},empty;
 SAPlacer legacy("legacy",e,enhanced,w,noFixed,empty,empty,2,4,20,40,5,1);
 std::vector<std::vector<std::vector<int>>> g(2,std::vector<std::vector<int>>(4));g[0][0]={0};g[1][1]={1};
 check(std::fabs(legacy.evaluateClusterPlacement(g,{{0,0},{1,1}})-20)<1e-6,"legacy objective unchanged");
 std::cout<<"DFX partition/floorplan contract, subset, physical SA, boundary and legacy checks PASS\n";
 fs::remove_all(dir);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
