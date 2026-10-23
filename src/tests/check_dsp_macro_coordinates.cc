#include "ParallelCLBPacker.h"
#include "HardMacroAnchor.h"
#include "simpleJSON.h"
#include <omp.h>
#include <stdexcept>
static void need(bool ok,const std::string &why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char **argv) {
 try {
  need(argc==3,"usage: checkDSPMacroCoordinates config replay.tsv");omp_set_num_threads(1);
  auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg.at("device"));DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
  std::ifstream in(argv[2]);std::string line,group,name,siteName;float dx,dy;
  std::map<std::string,PlacementInfo::PlacementMacro*> macros;
  std::map<DesignInfo::DesignCell*,DeviceInfo::DeviceSite*> expected;
  while(std::getline(in,line)) {
   std::istringstream row(line);need(bool(row>>group>>name>>dx>>dy>>siteName),"invalid replay row");
   auto cell=design.getCell(name);auto site=device.getSite(siteName);need(cell&&site,"missing replay member/site");
   auto &macro=macros[group];
   if(!macro) {macro=new PlacementInfo::PlacementMacro(group,p.getPlacementUnits().size(),PlacementInfo::PlacementMacro::PlacementMacroType_DSP);p.getPlacementUnits().push_back(macro);p.getPlacementMacros().push_back(macro);}
   macro->addCell(cell,cell->getCellType(),dx,dy);
   macro->setWeight(macro->getNumOfCells());
   macro->setAnchorLocationAndForgetTheOriginalOne(site->X(),site->Y()); // Reproduce old last-member anchor.
   p.getCellId2PlacementUnit()[cell->getCellId()]=macro;
   p.getPULegalSite()[macro].push_back(site);expected[cell]=site;
  }
  need(expected.size()==design.getCells().size(),"incomplete coordinate replay");
  p.updateCells2PlacementUnits();p.reloadNets();p.getCompatiblePlacementTable()->setBELTypeForCells(&design);
  p.calculateNetNumDistributionOfPUs();p.createGridBins(5,5);p.updateElementBinGrid();
  ParallelCLBPacker packer(&design,&device,&p,cfg,3,10,.25,.5,6,10,.02,"coordinate-replay",nullptr,nullptr);
  packer.addNonCLBPackingSites();
  const auto anchors=packer.getNonCLBPackedAnchors();
  // Visualize BEFORE committing anchors: the override must fix the stale state too.
  packer.dumpVisualizationSnapshot("coordinate-test",0,"before_writeback");
  for(int repeat=0;repeat<2;++repeat) {
   packer.setPULocationToPackedSite();
   for(auto &entry:expected) {
    auto cell=entry.first;auto site=entry.second;auto pu=p.getPlacementUnitByCellId(cell->getCellId());
    hard_macro_anchor::verify(pu,cell,site->X(),site->Y());
    auto xy=p.getCellId2location().at(cell->getCellId());
    need(std::fabs(xy.X-site->X())<1e-4f && std::fabs(xy.Y-site->Y())<1e-4f,"bin-grid cell coordinate disagrees with site");
    need(hard_macro_anchor::equal(anchors.at(pu->getId()),{pu->X(),pu->Y()}),"visualization and writeback disagree");
   }
  }
  packer.dumpVisualizationSnapshot("coordinate-test",1,"after_writeback");
  std::ofstream tcl(cfg.at("dumpDirectory")+"/replayed-dsp.tcl");packer.dumpDSPBRAMPlacementTcl(tcl);tcl.close();
  // Guard and locked-member regressions use a real multi-DSP macro.
  auto macro=macros.begin()->second;auto cell=macro->getCell(0);auto site=expected.at(cell);
  hard_macro_anchor::Anchors guard;hard_macro_anchor::accumulate(guard,macro,cell,site->X(),site->Y());
  bool rejected=false;try{hard_macro_anchor::accumulate(guard,macro,cell,site->X(),site->Y()+2.5f);}catch(const std::runtime_error&){rejected=true;}
  need(rejected,"inconsistent member sites accepted");macro->setLocked();
  hard_macro_anchor::Anchors locked;hard_macro_anchor::accumulate(locked,macro,cell,site->X(),site->Y());
  rejected=false;try{hard_macro_anchor::accumulate(locked,macro,cell,site->X(),site->Y()+2.5f);}catch(const std::runtime_error&){rejected=true;}
  need(rejected,"locked macro moved");
  DesignInfo::DesignCell singleCell("single",DesignInfo::CellType_DSP48E2,0);
  PlacementInfo::PlacementUnpackedCell single("single",999999,&singleCell);
  need(hard_macro_anchor::equal(hard_macro_anchor::fromMember(&single,&singleCell,13,27),{13,27}),"unpacked DSP offset changed");
  rejected=false;try{hard_macro_anchor::fromMember(&single,&singleCell,0,std::numeric_limits<float>::infinity());}catch(const std::runtime_error&){rejected=true;}
  need(rejected,"nonfinite site accepted");
  std::cout<<"PASS production hard-site writeback, repeated writeback, visualization override, bin-grid and Tcl; cells="<<expected.size()<<" macros="<<macros.size()<<"; inconsistent/locked guards passed; no placement\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
