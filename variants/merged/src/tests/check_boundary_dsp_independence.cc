// Verify production DSP node classification and delay for both independent switches.
#include "PlacementTimingInfo.h"
#include "simpleJSON.h"
#include <fstream>
#include <cmath>
#include <stdexcept>
int main(int argc, char **argv) {
 try {
  if (argc != 3) throw std::runtime_error("Usage: checkBoundaryDSPIndependence config result");
  auto cfg=parseJSONFile(argv[1]);
  DeviceInfo device(cfg,cfg.at("device")); DesignInfo design(cfg,&device);
  int checks=0,dspCount=0;
  for(auto cell:design.getCells()) dspCount+=cell->isDSP();
  if(!dspCount)throw std::runtime_error("Fixture must contain DSPs");
  for(bool critical:{false,true}) for(bool weak:{false,true}) for(bool registered:{false,true}) {
   auto options=cfg;
   options["DSPCritical"]=critical?"true":"false";
   options["BoundaryWeakAttraction"]=weak?"true":"false";
   for(auto cell:design.getCells())if(cell->isDSP())cell->setHasDSPReg(registered);
   PlacementTimingInfo timing(&design,&device,options);timing.buildSimpleTimingGraph();
   auto &nodes=timing.getSimplePlacementTimingInfo();
   for(auto cell:design.getCells())if(cell->isDSP()) {
    if(nodes.at(cell->getCellId())->checkIsRegister()!=(!critical||registered))
     throw std::runtime_error("DSP register classification changed");
    ++checks;
   }
   timing.setDSPInnerDelay();
   for(auto cell:design.getCells())if(cell->isDSP()) {
    if(std::fabs(nodes.at(cell->getCellId())->getInnerDelay()-(critical?1.3f:.1f))>1e-6)
     throw std::runtime_error("DSP internal delay changed");
    ++checks;
   }
  }
  std::ofstream out(argv[2]);out<<"{\"checks\":"<<checks<<",\"dsp_count\":"<<dspCount<<",\"passed\":true}\n";
  return 0;
 } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}
