#include "ParallelCLBPacker.h"
#include "simpleJSON.h"
#include <algorithm>
#include <stdexcept>
#include <omp.h>
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv){
 try{
  require(argc==2,"Usage: checkBoundaryPacking toy-config.json");
  auto cfg=parseJSONFile(argv[1]);omp_set_num_threads(1);
  DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
  std::vector<DeviceInfo::DeviceSite*> row;
  for(std::string type : {"SLICEL","SLICEM"})for(auto site:device.getSitesInType(type))if(site->Y()==100)row.push_back(site);
  std::sort(row.begin(),row.end(),[](auto a,auto b){return a->X()<b->X();});
  DeviceInfo::DeviceSite *left=nullptr,*right=nullptr;
  for(size_t i=1;i<row.size();++i)if(row[i-1]->getClockRegionX()!=row[i]->getClockRegionX()){
   left=row[i-1];right=row[i];break;
  }
  require(left&&right&&right->X()-left->X()<10,"missing real adjacent clock-region sites");
  // The target's clock-region column has no available SLICE; a nearby column does.
  for(std::string type : {"SLICEL","SLICEM"})for(auto candidate:device.getSitesInType(type))
   if(candidate!=right)candidate->setOccupied();
  for(auto cell:design.getCells()){
   auto pu=new PlacementInfo::PlacementUnpackedCell(cell->getName(),p.getPlacementUnits().size(),cell);
   pu->setWeight(1);pu->setAnchorLocationAndForgetTheOriginalOne(left->X(),left->Y());
   if(cell->getName()!="mid")pu->setFixed();
   p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
   p.getCellId2PlacementUnit()[cell->getCellId()]=pu;
  }
  p.updateCells2PlacementUnits();p.reloadNets();p.getCompatiblePlacementTable()->setBELTypeForCells(&design);
  design.updateFFControlSets();p.calculateNetNumDistributionOfPUs();p.createGridBins(5,5);p.updateElementBinGrid();p.buildSimpleTimingGraph();
  std::string middleName="mid";
  auto mid=p.getPlacementUnitByCellId(design.getCell(middleName)->getCellId());
  PlacementTimingOptimizer timing(&p,cfg);
  ParallelCLBPacker packer(&design,&device,&p,cfg,3,10,.25,.5,6,10,.02,"boundary-probe",&timing,nullptr);
  std::vector<ParallelCLBPacker::PackingCLBSite*> mapping(p.getPlacementUnits().size(),nullptr);
  ParallelCLBPacker::PackingCLBSite site(&p,right,3,10,.25,.5,6,10,.4,.02,mapping);
  for(bool enabled:{false,true}){
   cfg["BoundaryAwareClustering"]=enabled?"true":"false";
   require(p.boundaryClusteringEnabled()==enabled,"config mode fixture failed");
   auto choices=packer.findNeiborSitesFromBinGrid(DesignInfo::CellType_LUT4,left->X(),left->Y(),0,12,.4,true);
   bool found=std::find(choices->begin(),choices->end(),right)!=choices->end();delete choices;
   require(found==enabled,"ordinary packing-site query lost soft-region fallback or changed legacy behavior");
   choices=packer.findNeiborSitesFromBinGrid(DesignInfo::CellType_LUT4,left->X(),left->Y(),0,12,.4,true,1,0,1,0,10000);
   found=std::find(choices->begin(),choices->end(),right)!=choices->end();delete choices;
   require(found==enabled,"cone packing-site query lost soft-region fallback or changed legacy behavior");
   std::set<PlacementInfo::PlacementUnit*,Packing_PUcompare> nearby;
   site.findNeiborPUsFromBinGrid(DesignInfo::CellType_LUT4,right->X(),right->Y(),0,12,10,mapping,.4,&nearby,true);
   require(bool(nearby.count(mid))==enabled,"packing PU query retained a hard clock-column fence");
   mapping[mid->getId()]=&site;nearby.clear();
   site.findNeiborPUsFromBinGrid(DesignInfo::CellType_LUT4,right->X(),right->Y(),0,12,10,mapping,.4,&nearby,true);
   require(bool(nearby.count(mid))==enabled,"mapped PU query retained a hard clock-column fence");
   mapping[mid->getId()]=nullptr;
  }
  cfg["BoundaryAwareClustering"]="true";
  require(packer.exceptionPULegalize(mid,12,false),"physical-mode PU could not use the legal neighboring column");
  std::cout<<"PASS ordinary/cone site search and unmapped/mapped PU search across real CR columns; legacy filters preserved\n";
  return 0;
 }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
