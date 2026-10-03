#include "InitialPacker.h"
#include "MacroLegalizer.h"
#include "CLBLegalizer.h"
#include "simpleJSON.h"
#include <stdexcept>
#include <iostream>
#include <omp.h>

int main(int argc, char **argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected a design without Carry/LUTRAM/SRL");
        auto cfg = parseJSONFile(argv[1]);
        omp_set_num_threads(1);
        DeviceInfo device(cfg, cfg["device"]);
        DesignInfo design(cfg, &device);
        for (auto cell : design.getCells())
            if (cell->isCarry() || cell->isLUTRAM() || cell->isShifter())
                throw std::runtime_error("Test input contains a target resource");
        PlacementInfo placement(&design, &device, cfg);
        InitialPacker packer(&design, &device, &placement, cfg);
        packer.pack();
        std::vector<DesignInfo::DesignCellType> types{DesignInfo::CellType_CARRY8};
        std::vector<std::string> sites{"SLICEM"};
        MacroLegalizer carry("empty-carry-test", &placement, &device, types, cfg);
        CLBLegalizer mclb("empty-mclb-test", &placement, &device, sites, cfg);
        if (carry.getAverageDisplacementOfExactLegalization() < 1000 ||
            mclb.getAverageDisplacementOfExactLegalization() < 1000)
            throw std::runtime_error("Uninspected state was mistaken for empty success");
        for (bool exact : {false, true, true}) {
            carry.legalize(exact);
            mclb.legalize(exact);
            if (carry.getAverageDisplacementOfExactLegalization() != 0 ||
                carry.getAverageDisplacementOfRoughLegalization() != 0 ||
                mclb.getAverageDisplacementOfExactLegalization() != 0 ||
                mclb.getAverageDisplacementOfRoughLegalization() != 0)
                throw std::runtime_error("Empty target prevents convergence or produces NaN");
            carry.dumpMatching(true, true);
            mclb.dumpMatching(true, true);
        }
        std::cout << "Empty Carry/MCLB rough, exact, repeated legalization and forced diagnostic export passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
