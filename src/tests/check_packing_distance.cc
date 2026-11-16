#include "PlacementInfo.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
static void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
static bool found(float binDistance,float pointDistance){
 float lower=0;
 for(float upper=.5f;upper<=6;upper+=.25f){
  if(binDistance<=upper && pointDistance>lower && pointDistance<=upper)return true;
  lower=upper;
 }
 return false;
}
int main(int argc,char**argv){try{
 require(argc==2,"fixture path required");std::ifstream in(argv[1]);require(bool(in),"fixture missing");
 std::string line;int realCases=0;
 while(std::getline(in,line)){
  if(line.empty()||line[0]=='#')continue;
  std::istringstream s(line);float l,r,b,t,x,y,px,py;s>>l>>r>>b>>t>>x>>y>>px>>py;require(bool(s),"fixture row");
  PlacementInfo::PlacementBinInfo bin("test",l,r,b,t,0,0,nullptr);
  float point=std::fabs(px-x)+.4f*std::fabs(py-y);
  require(!found(bin.getManhattanDistanceTo(x,y),point),"baseline must reproduce missed annulus");
  require(found(bin.getManhattanDistanceTo(x,y,.4f),point),"weighted search must recover candidate");
  ++realCases;
 }
 require(realCases==12,"all recorded cases required");
 PlacementInfo::PlacementBinInfo bin("test",0,2,0,2,0,0,nullptr);
 const float queries[][4]={{1,1,0,0},{0,0,0,0},{2,2,0,0},{-1,1,1,0},{3,1,1,0},{1,-2,0,2},{1,4,0,2},{-1,4,1,2}};
 int boundaryCases=0;
 for(auto &q:queries){
  require(bin.getManhattanDistanceTo(q[0],q[1])==q[2]+q[3],"unweighted callers retain behavior");
  for(float ratio:{.4f,1.f,2.f}){
   float expected=q[2]+ratio*q[3];
   require(std::fabs(bin.getManhattanDistanceTo(q[0],q[1],ratio)-expected)<1e-6f,"inside edge axis corner weighted lower bound");
   ++boundaryCases;
  }
 }
 std::cout<<"{\"recorded_candidates_recovered\":"<<realCases<<",\"weighted_geometry_checks\":"<<boundaryCases<<",\"legacy_default_checks\":8,\"passed\":true}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
