#include "HierarchicalBoundaryPolicy.h"
#include <iostream>
#include <stdexcept>
#include <vector>
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    try {
        // Geometric order and SLR IDs deliberately differ.
        struct Region { int slr; };
        std::vector<Region> regions{{7}, {7}, {3}, {3}, {9}, {9}};
        auto a = hierarchical_boundary::select(regions, {{0,40},{1,20},{2,0},{3,40}});
        require(a.slr == 7 && a.region == 0 && a.slrVotes == 60 && a.sideVotes == 40,
                "Conditional side majority was incorrectly required to exceed half the whole cluster");
        a = hierarchical_boundary::select(regions, {{0,30},{1,30},{3,40}});
        require(a.slr == 7 && a.region == -1, "A side tie must keep the SLR decision");
        a = hierarchical_boundary::select(regions, {{0,50},{2,50}});
        require(a.slr == -1, "A 50/50 SLR split is not a strict majority");
        a = hierarchical_boundary::select(regions, {{0,40},{2,35},{4,25}});
        require(a.slr == -1, "A plurality is not a majority");
        a = hierarchical_boundary::select(regions, {{0,20},{1,20},{2,40},{3,20}});
        require(a.slr == 3 && a.region == 2, "SLR selection depends on numeric ID order");
        auto range = hierarchical_boundary::expandedRange(20,40,0,100,50,100);
        require(range.first == 15 && range.second == 45, "Paper stretch equation changed");
        range = hierarchical_boundary::expandedRange(1,41,0,60,100,100);
        require(range.first == 0 && range.second == 60, "Stretch escaped its physical region");
        range = hierarchical_boundary::expandedRange(20,40,0,100,100,0);
        require(range.first == 20 && range.second == 40, "Empty-region denominator is unguarded");
        // Independent transcription of the original one-sided branch, with
        // exact threshold checks and both directions after crossing the center.
        for (bool dsp : {false,true}) for (size_t nets : {0,1,2,10,100})
            for (float d : {-10.f,-.01f,0.f,.01f,2.99f,3.f,3.01f,6.f,6.01f,1000.f})
            {
                float expected=0;
                if(d>6)expected=.01f*std::pow(nets,1.1);
                else if(d>3 && !dsp)expected=.01f*nets;
                else if(d>0 && !dsp)expected=(d/3)*.01f*nets;
                require(std::fabs(hierarchical_boundary::anchorWeight(.01f,d,nets,dsp)-expected)<1e-5,
                        "One-sided weight differs from original piecewise formula");
                for(int direction : {-1,1})
                    require(hierarchical_boundary::anchorWeight(.01f,direction*(50-(50+direction*2)),nets,dsp)==0,
                            "Crossing the center reversed the attraction");
            }
        std::cout << "PASS majority, stretch, 100 original-formula cases and center-crossing checks\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
