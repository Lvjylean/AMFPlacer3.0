#include "BoundaryAwareClusterer.h"
#include "RegionCapacityTracker.h"
#include "simpleJSON.h"
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <omp.h>
int main(int argc,char **argv)
{
    try {
        if(argc!=4)throw std::runtime_error("Usage: checkBoundaryClustering config.json scenario result.tsv");
        auto cfg=parseJSONFile(argv[1]);std::string scenario=argv[2];omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
        for(auto cell:design.getCells())
        {
            auto pu=new PlacementInfo::PlacementUnpackedCell(cell->getName(),p.getPlacementUnits().size(),cell);
            pu->setWeight(1);
            if(cell->isDSP())cell->setHasDSPReg(true);
            bool middle=cell->getName()=="mid";
            float x=100,y=237;
            if(scenario=="io"){x=middle?165:150;y=100;}
            else if(scenario=="xy"){x=middle?165:150;y=middle?243:237;}
            else if(scenario=="inside"){x=middle?101:100;y=100;}
            else if(middle)y=243;
            pu->setAnchorLocationAndForgetTheOriginalOne(x,y);
            if(!middle)pu->setFixed();
            p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
            p.getCellId2PlacementUnit()[cell->getCellId()]=pu;
        }
        p.updateCells2PlacementUnits();p.reloadNets();
        p.getCompatiblePlacementTable()->setBELTypeForCells(&design);
        design.updateFFControlSets();p.calculateNetNumDistributionOfPUs();
        p.createGridBins(5,5);p.updateElementBinGrid();p.buildSimpleTimingGraph();
        // Match the actual InitialPacker representation, not just scalar capacities.
        RegionCapacityTracker budget(&p);
        DesignInfo::DesignCell real36(false,"ram36-probe",DesignInfo::CellType_RAMB36E2,999997);
        DesignInfo::DesignCell upper18(true,"ram36-upper",DesignInfo::CellType_RAMB18E2,999998);
        DesignInfo::DesignCell real18(false,"ram18-probe",DesignInfo::CellType_RAMB18E2,999999);
        auto a=budget.cellDemand(&real36),b=budget.cellDemand(&upper18),c=budget.cellDemand(&real18);
        if(a[PhysicalBoundaryModel::BRAM36]!=1 || b[PhysicalBoundaryModel::BRAM18]!=0 || c[PhysicalBoundaryModel::BRAM18]!=1)
            throw std::runtime_error("RAMB36 virtual upper-half double counted");
        PlacementTimingOptimizer timing(&p,cfg);
        timing.clusterCriticalPathsByPhysicalRegion();
        std::string middleName="mid";
        auto pu=p.getPlacementUnitByCellId(design.getCell(middleName)->getCellId());
        auto &prefs=p.getRegionPreferences();
        if(scenario=="inside")
        {
            if(!prefs.empty())throw std::runtime_error("An in-region path was moved without gain");
        }
        else
        {
            if(!prefs.count(pu))throw std::runtime_error("Critical CLB next to fixed/DSP/URAM endpoints not clustered");
            float x,y;
            if(!p.regionTarget(pu,prefs.at(pu).region,x,y))throw std::runtime_error("Invalid region target");
            if((scenario=="io"||scenario=="xy") && !(x<158))throw std::runtime_error("Wrong X attraction");
            if((scenario=="slr"||scenario=="xy"||scenario=="dsp"||scenario=="uram") && !(y<239.5))
                throw std::runtime_error("Wrong Y attraction");
            pu->setAnchorLocationAndForgetTheOriginalOne(x,y);
            float x2,y2;p.regionTarget(pu,prefs.at(pu).region,x2,y2);
            if(x!=x2||y!=y2)throw std::runtime_error("In-region target causes center collapse");
        }
        // Retired objects must be removed before refresh dereferences a preference.
        auto retired=new PlacementInfo::PlacementUnpackedCell("retired",999999,design.getCell(middleName));
        prefs[retired]={0,999,1};delete retired;p.refreshRegionPreferences();
        if(prefs.count(retired))throw std::runtime_error("Stale PU preference survived");
        std::ofstream out(argv[3]);out<<"scenario\tpreferences\tpassed\n"<<scenario<<'\t'<<prefs.size()<<"\t1\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}

