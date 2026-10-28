#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <iomanip>
#include <stdexcept>
#include <omp.h>
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv)
{
    try {
        check(argc==4,"Usage: checkDevicePolicyStretch config expected_movement output");
        auto cfg=parseJSONFile(argv[1]);omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg.at("device"));DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
        for(auto cell:design.getCells()) {
            auto pu=new PlacementInfo::PlacementUnpackedCell(cell->getName(),p.getPlacementUnits().size(),cell);
            pu->setWeight(1);
            // >16 cells in each occupied bin at the bottom of one CR column:
            // the original density-triggered stretching must move this fixture.
            pu->setAnchorLocationAndForgetTheOriginalOne(20,10+cell->getCellId()%3);
            if(cell->getName()=="source")pu->setFixed();
            p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
            p.getCellId2PlacementUnit()[cell->getCellId()]=pu;
        }
        p.updateCells2PlacementUnits();p.reloadNets();
        p.getCompatiblePlacementTable()->setBELTypeForCells(&design);design.updateFFControlSets();
        p.calculateNetNumDistributionOfPUs();p.createGridBins(5,5);p.updateElementBinGrid();p.buildSimpleTimingGraph();
        PlacementTimingOptimizer timing(&p,cfg);
        std::vector<std::pair<float,float>> before;
        for(auto pu:p.getPlacementUnits())before.push_back({pu->X(),pu->Y()});
        timing.stretchClockRegionColumns();
        std::ofstream out(argv[3]);check(bool(out),"Cannot open output");out<<std::setprecision(10);
        int moved=0;
        for(auto pu:p.getPlacementUnits()){
            auto old=before.at(pu->getId());const bool changed=old!=std::make_pair(pu->X(),pu->Y());
            if(changed)++moved;
            if(pu->isFixed())check(!changed,"Stretch moved a fixed PU");
            out<<pu->getName()<<'\t'<<pu->X()<<'\t'<<pu->Y()<<'\n';
        }
        check((moved>0)==(std::string(argv[2])=="true"),"Unexpected production stretching behavior");
        std::cout<<"Moved PUs: "<<moved<<'\n';return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
