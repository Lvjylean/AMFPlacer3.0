#include "ExternalFloorplan.h"
#include "ClusterPlacer.h"
#include "simpleJSON.h"
#include <fstream>
#include <omp.h>
#include <set>
#include <stdexcept>

static void require(bool value, const char *message)
{ if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("Usage: checkExternalFloorplan config scenario result");
        omp_set_num_threads(1);
        auto cfg = parseJSONFile(argv[1]);
        std::string scenario = argv[2];
        DeviceInfo device(cfg, cfg["device"]);
        DesignInfo design(cfg, &device);
        PlacementInfo p(&design, &device, cfg);
        std::string aName = "pair_a", bName = "pair_b";
        auto a = design.getCell(aName), b = design.getCell(bName);
        auto macro = new PlacementInfo::PlacementMacro("mixed_macro", 0, PlacementInfo::PlacementMacro::PlacementMacroType_FFFFPair);
        macro->addCell(a, a->getCellType(), 0, 0);
        macro->addCell(b, b->getCellType(), 0, 1);
        macro->addVirtualCell(&design, DesignInfo::CellType_FDRE, 0, 2);
        macro->setWeight(3); macro->setAnchorLocationAndForgetTheOriginalOne(1, 10);
        p.getPlacementUnits().push_back(macro); p.getPlacementMacros().push_back(macro);
        for (auto cell : macro->getCells()) p.getCellId2PlacementUnit()[cell->getCellId()] = macro;
        for (auto cell : design.getCells())
        {
            if (cell == a || cell == b || cell->isVirtualCell()) continue;
            auto pu = new PlacementInfo::PlacementUnpackedCell(cell->getName(), p.getPlacementUnits().size(), cell);
            pu->setWeight(1); pu->setAnchorLocationAndForgetTheOriginalOne(1, 10);
            if (cell->getName() == "fixed") { pu->setFixed(); pu->setLocked(); }
            p.getPlacementUnits().push_back(pu); p.getPlacementUnpackedCells().push_back(pu);
            p.getCellId2PlacementUnit()[cell->getCellId()] = pu;
        }
        p.updateCells2PlacementUnits(); p.reloadNets();
        p.getCompatiblePlacementTable()->setBELTypeForCells(&design);
        design.updateFFControlSets(); p.calculateNetNumDistributionOfPUs();
        p.createGridBins(5, 5); p.updateElementBinGrid();
        std::vector<std::tuple<float, float, bool, bool>> before;
        for (auto pu : p.getPlacementUnits()) before.emplace_back(pu->X(), pu->Y(), pu->isFixed(), pu->isLocked());
        size_t mapped = 0;
        for (auto site : device.getSites()) mapped += site->isMapped();
        if (scenario == "legacy")
        {
            p.buildSimpleTimingGraph();
            ClusterPlacer placer(&p, cfg);
            placer.ClusterPlacement();
            require(p.getClusterNum() > 0, "Legacy clustering did not run");
        }
        else
        {
            bool rejected = false;
            try
            {
                ClusterPlacer placer(&p, cfg);
                placer.ClusterPlacement();
            }
            catch (const std::exception &e)
            {
                if (scenario != "reject") throw;
                rejected = true;
                std::cout << "EXPECTED_REJECTION: " << e.what() << '\n';
            }
            if (scenario == "reject")
            {
                require(rejected, "Malformed input was accepted");
                for (size_t i = 0; i < before.size(); ++i)
                {
                    auto pu = p.getPlacementUnits()[i];
                    require(pu->X() == std::get<0>(before[i]) && pu->Y() == std::get<1>(before[i]), "Rejected input partly moved PUs");
                }
            }
            else
            {
                require(p.getRegionPreferences().empty() && p.getPU2ClockRegionCenters().empty(),
                        "External initialization installed persistent attraction");
                require(macro->getCellOffsetYInMacro(b) == 1 && macro->getCells().size() == 3, "Macro changed");
                std::set<std::pair<float, float>> coords;
                for (size_t i = 0; i < before.size(); ++i)
                {
                    auto pu = p.getPlacementUnits()[i];
                    require(pu->isFixed() == std::get<2>(before[i]) && pu->isLocked() == std::get<3>(before[i]), "PU flags changed");
                    if (pu->isFixed() || pu->isLocked())
                        require(pu->X() == std::get<0>(before[i]) && pu->Y() == std::get<1>(before[i]), "Fixed PU moved");
                    else coords.insert({pu->X(), pu->Y()});
                }
                require(coords.size() > 4, "Initialization collapsed to a few centers");
                size_t mappedAfter = 0;
                for (auto site : device.getSites()) mappedAfter += site->isMapped();
                require(mapped == mappedAfter, "Initializer reserved sites");
                for (int i = 0; i < 12; ++i)
                {
                    std::string name = "free" + std::to_string(i);
                    auto pu = p.getPlacementUnitByCell(design.getCell(name));
                    int cx, cy; device.getClockRegionByLocation(pu->X(), pu->Y(), cx, cy);
                    require(cy == 0 && (cx == 0 || cx == 2), "Nonrectangular CR union was filled or mis-mapped");
                }
                // Move an ordinary PU into a CR/SLR outside its input region. Existing
                // legality helpers must not clamp it back to the external preference.
                std::string name = "free0"; auto pu = p.getPlacementUnitByCell(design.getCell(name));
                float x = 2, y = 800;
                p.legalizeXYInArea(pu, x, y); pu->setAnchorLocationAndForgetTheOriginalOne(x, y);
                require(pu->Y() > 719.5, "Soft initialization prevented later cross-SLR movement");
            }
        }
        std::ofstream out(argv[3]); out << "{\"passed\":true,\"scenario\":\"" << scenario << "\"}\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 2; }
}

