// Exercise production timing, both interfaces and STA against the frozen legacy model.
// Legacy method below is copied verbatim from the pre-change header (2026-09-29).
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <iomanip>
#include <stdexcept>

struct LegacyDelayReference
{
    DeviceInfo *deviceInfo;
    float slrBoundaryDelayNs;
    const float timingC0[10] = {95.05263521, -26.50563359, 77.42394117, 106.29195883, -14.975527};
    const float timingC1[10] = {123.05017047, -169.25614191, -117.28028144, 208.53573639, 174.2573465};
    const float timingC2[10] = {234.7694101, -433.99467294, -64.96319998, 373.78606257, 139.45226658};

    inline float getDelayByModel_conservative(float X1, float Y1, float X2, float Y2)
    {
        int clockRegionX0, clockRegionY0;
        deviceInfo->getClockRegionByLocation(X1, Y1, clockRegionX0, clockRegionY0);
        int clockRegionX1, clockRegionY1;
        deviceInfo->getClockRegionByLocation(X2, Y2, clockRegionX1, clockRegionY1);

        float X = std::fabs(X1 - X2);
        float Y = std::fabs(Y1 - Y2);
        auto physical = deviceInfo->isPhysicalBoundaryTimingEnabled() ? deviceInfo->getPhysicalBoundaryModel() : nullptr;
        const float slrDelay = physical ? physical->penalty(X1, Y1, X2, Y2, slrBoundaryDelayNs) :
            slrBoundaryDelayNs * deviceInfo->getSLRBoundaryCount(clockRegionY0, clockRegionY1);

        if (X * X + Y * Y < 9)
        {
            X *= 2;
            float delay = (timingC0[0] + std::pow(X, 0.3) * timingC0[1] + std::pow(Y, 0.3) * timingC0[2] +
                           std::pow(X, 0.5) * timingC0[3] + std::pow(Y, 0.5) * timingC0[4]) /
                          1000.0;
            if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
        else if (X * X + Y * Y < 36)
        {
            X *= 2;
            float delay = (timingC1[0] + std::pow(X, 0.3) * timingC1[1] + std::pow(Y, 0.3) * timingC1[2] +
                           std::pow(X, 0.5) * timingC1[3] + std::pow(Y, 0.5) * timingC1[4]) /
                          1000.0;
            if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
        else
        {
            X *= 2;
            float delay = (timingC2[0] + std::pow(X, 0.3) * timingC2[1] + std::pow(Y, 0.3) * timingC2[2] +
                           std::pow(X, 0.5) * timingC2[3] + std::pow(Y, 0.5) * timingC2[4]) /
                          1000.0;
            if (!physical && (std::abs(clockRegionX1 - clockRegionX0) > 1 || clockRegionX1 == 2 || clockRegionX0 == 2))
                delay += std::abs(clockRegionX1 - clockRegionX0) * 0.5;

            if (delay < 0.05)
                delay = 0.05;
            return delay + slrDelay;
        }
    }


};

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("Usage: checkDelayCoefficients config pairs result");
        auto cfg = parseJSONFile(argv[1]);
        DeviceInfo device(cfg, cfg["device"]);
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        PlacementTimingOptimizer timing(&placement, cfg);
        LegacyDelayReference legacy{&device, std::stof(cfg.at("SLRBoundaryDelayNs"))};
        placement.getTimingInfo()->buildSimpleTimingGraph();
        auto &locations = placement.getCellId2location();
        locations.resize(design.getCells().size());
        std::ifstream input(argv[2]);
        std::ofstream output(argv[3]);
        if (!input || !output) throw std::runtime_error("Cannot open input/output");
        output << std::setprecision(10)
               << "name\tlegacy_ns\tactual_ns\tnode_ns\treverse_ns\tsta_ns\tslack_ns\n";
        std::string name, sourceName = "source";
        float x0,y0,x1,y1;
        int count = 0;
        while (input >> name >> x0 >> y0 >> x1 >> y1)
        {
            float value = timing.getDelayByModel(x0,y0,x1,y1);
            float nodes = timing.getDelayByModel(nullptr,nullptr,x0,y0,x1,y1);
            float reverse = timing.getDelayByModel(x1,y1,x0,y0);
            if (value != nodes || value != reverse)
                throw std::runtime_error("Delay interfaces or endpoint symmetry disagree: "+name);
            float original = legacy.getDelayByModel_conservative(x0,y0,x1,y1);
            if (device.getDeviceName() != "U250" && value != original)
                throw std::runtime_error("Legacy device changed: "+name);
            float arrival = 0, slack = 0;
            if (name.rfind("sta-",0) == 0)
            {
                for (auto cell : design.getCells())
                    locations[cell->getCellId()] = cell->getName() == "mid"
                        ? PlacementInfo::Location{x1,y1} : PlacementInfo::Location{x0,y0};
                arrival = timing.conductStaticTimingAnalysis();
                slack = timing.getWorstSlackOfCell(design.getCell(sourceName));
            }
            output << name << '\t' << original << '\t' << value << '\t' << nodes << '\t'
                   << reverse << '\t' << arrival << '\t' << slack << '\n';
            ++count;
        }
        if (!input.eof() || !count) throw std::runtime_error("Empty or malformed probe input");
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 2; }
}
