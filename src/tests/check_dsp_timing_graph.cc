#include "PlacementTimingInfo.h"
#include "simpleJSON.h"
#include <omp.h>
#include <cmath>
#include <stdexcept>
static void need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char **argv) {
 try {
  need(argc==2,"usage: checkDSPTimingGraph config");omp_set_num_threads(1);
  auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg.at("device"));DesignInfo design(cfg,&device);
  unsigned reg=0,comb=0;
  for(auto c:design.getCells())if(c->isDSP()) {
   need(c->getDSPPREG()>=0,"uncertified DSP");
   c->setHasDSPReg(c->getDSPPREG()==0); // The previous heuristic must not override either value.
   need(c->checkHasDSPReg()==(c->getDSPPREG()==1),"heuristic overrode metadata");
   (c->getDSPPREG()==1?reg:comb)++;
  }
  for(auto mode:{"true","false"}) {
   cfg["DSPCritical"]=mode;PlacementTimingInfo timing(&design,&device,cfg);timing.buildSimpleTimingGraph();
   auto graph=timing.getSimplePlacementTimingGraph();
   for(auto node:graph->getNodes()) {
    node->setInnerDelay(0);
    if(node->getDesignNode()->isDSP())need(node->checkIsRegister()==(node->getDesignNode()->getDSPPREG()==1),"graph PREG classification mismatch");
   }
   auto clustering=timing.getClusteringGraph();
   need(clustering!=graph,"graph views alias");
   for(auto n:graph->getNodes()) {
    auto c=n->getDesignNode();
    need(c->getTimingForwardLevel()==n->getForwardLevel(),"legacy graph corrupted timing fields");
    if(c->isDSP())need(clustering->getNodes().at(n->getId())->checkIsRegister()==
       (std::string(mode)=="false" || c->getDSPPREG()==0),"legacy classification lost");
   }
   graph->propogateArrivalTime();
   // Fixture chain per DSP: FF -> LUT -> DSP -> LUT -> FF. All net delays 1 ns.
   for(auto cell:design.getCells())if(cell->isDSP()) {
    auto name=cell->getName()+"_sink";auto sink=design.getCell(name);
    auto node=graph->getNodes().at(sink->getCellId());
    need(std::fabs(node->getLatestInputArrival()-(cell->getDSPPREG()==1?2.f:4.f))<1e-5f,"arrival did not restart at PREG (or was cut at PREG=0)");
    auto dsp=graph->getNodes().at(cell->getCellId());
    need((dsp->getForwardLevel()==0)==(cell->getDSPPREG()==1),"forward path did not reset at PREG");
   }
  }
  std::cout<<"PASS production DesignInfo + graph + levelization + arrival; PREG1="<<reg<<" PREG0="<<comb<<" DSPCritical=true/false; no placement\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
