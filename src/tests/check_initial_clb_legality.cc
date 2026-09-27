#include "InitialPacker.h"
#include "simpleJSON.h"
#include <fstream>
#include <stdexcept>
#include <omp.h>

int main(int argc,char **argv)
{
    try {
        if(argc!=3) throw std::runtime_error("Usage: checkInitialCLBLegality config.json ownership.tsv");
        auto cfg=parseJSONFile(argv[1]);omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);
        PlacementInfo placement(&design,&device,cfg);
        InitialPacker packer(&design,&device,&placement,cfg);packer.pack();
        std::ofstream out(argv[2]);
        out<<"cell\tmacro\tis_carry\tvirtual\tff\n";
        for(auto pu:placement.getPlacementUnits()) {
            if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)) {
                for(auto cell:macro->getCells())
                    out<<cell->getName()<<'\t'<<macro->getName()<<'\t'<<macro->checkHasCARRY()<<'\t'
                       <<cell->isVirtualCell()<<'\t'<<cell->isFF()<<'\n';
            } else if(auto single=dynamic_cast<PlacementInfo::PlacementUnpackedCell*>(pu)) {
                auto cell=single->getCell();
                out<<cell->getName()<<"\t\t0\t0\t"<<cell->isFF()<<'\n';
            }
        }
        if(!out)throw std::runtime_error("Cannot write ownership report");
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
