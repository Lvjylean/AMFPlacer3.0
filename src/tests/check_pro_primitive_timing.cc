// Verify opt-in primitive corrections through production timing classes.
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <fstream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: checkProPrimitiveTiming config result");
        auto cfg = parseJSONFile(argv[1]);
        DeviceInfo device(cfg, cfg.at("device"));
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        placement.getTimingInfo()->buildSimpleTimingGraph();
        placement.getCellId2location().resize(design.getCells().size(), {20, 20});
        using Node = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>::TimingNode;
        DesignInfo::DesignCell carryA("carryA", DesignInfo::CellType_CARRY8, 0);
        DesignInfo::DesignCell carryB("carryB", DesignInfo::CellType_CARRY8, 1);
        DesignInfo::DesignCell dsp("dsp", DesignInfo::CellType_DSP48E2, 2);
        DesignInfo::DesignCell ff("ff", DesignInfo::CellType_FDRE, 3);
        Node a(&carryA, 0), b(&carryB, 1), d(&dsp, 2), f(&ff, 3);
        d.setForwardLevel(0);
        int checks = 0;
        auto equal = [&](float actual, float expected) {
            if (!std::isfinite(actual) || std::fabs(actual - expected) > 1e-6f)
                throw std::runtime_error("Primitive correction mismatch");
            ++checks;
        };
        const std::array<std::array<float, 4>, 5> pairs = {{{20,20,20,20},
            {20,20,20,24}, {20,20,30,40}, {20,239,20,240}, {74,239,75,240}}};
        for (bool enabled : {false, true})
        {
            for (bool critical : {false, true})
            {
                auto options = cfg;
                options["ProPrimitiveTiming"] = enabled ? "true" : "false";
                options["ProLegacyIOBoundary"] = "false";
                options["DSPCritical"] = critical ? "true" : "false";
                PlacementTimingOptimizer timing(&placement, options);
                for (auto xy : pairs)
                {
                    auto delay = [&](Node *n, Node *m) {
                        return timing.getDelayByModel(n,m,xy[0],xy[1],xy[2],xy[3]);
                    };
                    float base = timing.getDelayByModel(xy[0],xy[1],xy[2],xy[3]);
                    equal(delay(&a,&b), base); // CARRY disabled until a later STA phase.
                    equal(delay(&d,&f), base); // DSP disabled before packing timing.
                }
                timing.enablePackingTiming();
                for (int staCall = 1; staCall <= 31; ++staCall)
                {
                    timing.conductStaticTimingAnalysis();
                    // Full STA computes effectFactor from its own invocation counter.
                    // Exercise the actual inactive, exact-threshold and active phases.
                    if (staCall != 1 && staCall != 30 && staCall != 31) continue;
                    float factor = timing.getEffectFactor();
                    if ((factor > 1) != (staCall > 30))
                        throw std::runtime_error("Unexpected production STA phase gate");
                    for (auto xy : pairs)
                    {
                        auto delay = [&](Node *n, Node *m) {
                            return timing.getDelayByModel(n,m,xy[0],xy[1],xy[2],xy[3]);
                        };
                        float base = timing.getDelayByModel(xy[0],xy[1],xy[2],xy[3]);
                        equal(delay(&a,&b), enabled && factor > 1 ? std::max(0.027f,base-0.15f) : base);
                        equal(delay(&b,&a), delay(&a,&b));
                        equal(delay(&a,&f), base);
                        equal(delay(&f,&f), base);
                        float dspExpected = base + (enabled && !critical ? 0.5f : 0.0f);
                        equal(delay(&d,&f), dspExpected);
                        equal(delay(&f,&d), dspExpected);
                        equal(delay(&d,&d), dspExpected); // Add only once for two DSP endpoints.
                        d.setForwardLevel(1);
                        equal(delay(&d,&f), base);
                        d.setForwardLevel(0);
                    }
                }
            }
        }
        auto legacy = cfg;
        legacy["ProLegacyIOBoundary"] = "true";
        bool rejected = false;
        try { PlacementTimingOptimizer incompatible(&placement, legacy); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Legacy boundary model accepted U250");
        std::ofstream output(argv[2]);
        output << "{\"checks\":" << checks << ",\"legacy_u250_rejected\":true,\"passed\":true}\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 2; }
}
