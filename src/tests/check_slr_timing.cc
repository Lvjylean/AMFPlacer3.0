// Native integration probe: use the production device reader, delay model and STA.
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <iomanip>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("Usage: checkSLRTiming config.json pairs.tsv result.tsv");
        auto cfg = parseJSONFile(argv[1]);
        DeviceInfo device(cfg, cfg["device"]);
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        auto offCfg = cfg;
        offCfg["SLRBoundaryDelayNs"] = "0";
        PlacementTimingOptimizer off(&placement, offCfg), on(&placement, cfg);
        std::ifstream pairs(argv[2]);
        std::ofstream out(argv[3]);
        if (!pairs || !out) throw std::runtime_error("Cannot open probe input/output");
        out << std::setprecision(9) << "name\tboundaries\told_ns\tnew_ns\treverse_ns\n";
        std::string name;
        float x0, y0, x1, y1;
        int expected;
        bool foundPair = false;
        while (pairs >> name >> x0 >> y0 >> x1 >> y1 >> expected)
        {
            int cx0, cy0, cx1, cy1;
            device.getClockRegionByLocation(x0, y0, cx0, cy0);
            device.getClockRegionByLocation(x1, y1, cx1, cy1);
            const int boundaries = device.getSLRBoundaryCount(cy0, cy1);
            if (boundaries != expected) throw std::runtime_error("Wrong SLR seam count: " + name);
            out << name << '\t' << boundaries << '\t' << off.getDelayByModel(x0, y0, x1, y1) << '\t'
                << on.getDelayByModel(x0, y0, x1, y1) << '\t' << on.getDelayByModel(x1, y1, x0, y0) << '\n';
            foundPair = true;
        }
        if (!pairs.eof() || !foundPair) throw std::runtime_error("Malformed or empty pair input");
        // The final pair drives a register -> LUT -> register path which goes
        // across the same seam(s) and back. Verify actual STA/slack propagation.
        placement.getTimingInfo()->buildSimpleTimingGraph();
        auto &locations = placement.getCellId2location();
        locations.resize(design.getCells().size());
        for (auto cell : design.getCells())
            locations[cell->getCellId()] = cell->getName() == "mid" ? PlacementInfo::Location{x1, y1}
                                                                    : PlacementInfo::Location{x0, y0};
        std::string sourceName = "source";
        const float offArrival = off.conductStaticTimingAnalysis();
        const float offSlack = off.getWorstSlackOfCell(design.getCell(sourceName));
        const float onArrival = on.conductStaticTimingAnalysis();
        const float onSlack = on.getWorstSlackOfCell(design.getCell(sourceName));
        out << "sta_roundtrip\t" << 2 * expected << '\t' << offArrival << '\t' << onArrival << "\t0\n";
        out << "slack_roundtrip\t" << 2 * expected << '\t' << offSlack << '\t' << onSlack << "\t0\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
