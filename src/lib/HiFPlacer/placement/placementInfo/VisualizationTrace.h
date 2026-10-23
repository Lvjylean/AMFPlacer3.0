#pragma once
#include "PlacementInfo.h"
#include <zlib.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <stdexcept>
namespace visualization_trace {
inline bool enabled(const std::map<std::string,std::string>& c) { auto i=c.find("VisualizationTraceFine");return i!=c.end() && i->second=="true"; }
inline void snapshot(PlacementInfo* info, const std::map<std::string,std::string>& cfg,
 const char* phase, int round, int iteration, const char* step,
 const std::map<int,std::pair<float,float>>& anchors={}) {
 if(!enabled(cfg)) return;
 static unsigned long seq=0;
 static const auto start=std::chrono::steady_clock::now();
 auto prefix=cfg.at("DumpAllCoordTrace");
 std::string file=prefix+"-"+std::to_string(seq)+".gz", tmp=file+".tmp";
 gzFile out=gzopen(tmp.c_str(),"wb3");if(!out)throw std::runtime_error("Cannot create visualization trace");
 size_t count=0;
 auto emit=[&](float x,float y,const std::string& name){
  if(gzprintf(out,"%.9g %.9g %s\n",double(x),double(y),name.c_str())<=0)throw std::runtime_error("Visualization write failed");++count;
 };
 for(auto pu:info->getPlacementUnits()) {
  float x=pu->X(),y=pu->Y();auto a=anchors.find(pu->getId());if(a!=anchors.end()){x=a->second.first;y=a->second.second;}
  if(auto single=dynamic_cast<PlacementInfo::PlacementUnpackedCell*>(pu)) emit(x,y,single->getCell()->getName());
  else if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu))
   for(int i=0;i<macro->getNumOfCells();++i){float dx,dy;DesignInfo::DesignCellType t;macro->getVirtualCellInfo(i,dx,dy,t);auto cell=macro->getCell(i);emit(x+dx,y+dy,cell?cell->getName():"");}
 }
 if(gzclose(out)!=Z_OK || std::rename(tmp.c_str(),file.c_str()))throw std::runtime_error("Visualization finalize failed");
 std::ofstream events(prefix+"-events.tsv",std::ios::app);
 events<<seq<<'\t'<<phase<<'\t'<<round<<'\t'<<iteration<<'\t'<<step<<'\t'<<std::setprecision(12)
 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\t'<<count<<'\n';
 if(!events)throw std::runtime_error("Visualization event write failed");++seq;
}
}
