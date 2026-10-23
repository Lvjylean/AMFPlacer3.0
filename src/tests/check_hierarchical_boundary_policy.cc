#include "HierarchicalBoundaryPolicy.h"
#include "ClockRegionAnchorPolicy.h"
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
                require(std::fabs(hierarchical_boundary::anchorWeight(.01f,d,nets,!dsp)-expected)<1e-5,
                        "One-sided weight differs from original piecewise formula");
                for(int direction : {-1,1})
                    require(hierarchical_boundary::anchorWeight(.01f,direction*(50-(50+direction*2)),nets,!dsp)==0,
                            "Crossing the center reversed the attraction");
            }
        struct DieRegion { int slr; float y0,y1; };
        std::vector<DieRegion> dies{{3,240,480},{9,720,960},{7,0,240},
                                    {5,480,720},{7,0,240},{3,240,480}};
        for (int i=0;i<int(dies.size());++i)
            require(hierarchical_boundary::slrPullDirection(dies,i)==
                    (dies[i].slr==7?-1:(dies[i].slr==9?1:0)),
                    "SLR outer/middle rule depends on ID, order, or HPIO side");
        // Compare all three rotated cases against the original X branches.
        // Interior regions may reverse after crossing center; outer ones may not.
        for(bool dsp : {false,true}) for(size_t nets : {0,1,2,10,100})
            for(float offset : {-100.f,-6.01f,-6.f,-3.f,-.01f,0.f,.01f,3.f,6.f,6.01f,100.f})
                for(int direction : {-1,0,1}) {
                    const float y=360+offset;
                    const float d=direction<0?y-360:(direction>0?360-y:std::fabs(y-360));
                    float expected=0;
                    if(d>6)expected=.01f*std::pow(nets,1.1);
                    else if(d>3 && !dsp)expected=.01f*nets;
                    else if(d>0 && !dsp)expected=d/3*.01f*nets;
                    const float actual=hierarchical_boundary::anchorWeight(.01f,
                        hierarchical_boundary::anchorDistance(y,360,direction),nets,!dsp);
                    require(std::fabs(actual-expected)<1e-5,"Rotated SLR weight differs from upstream branch");
                }
        // Unequal CR spacing and shuffled row/column IDs: geometry, not an
        // assumed U250 grid size or numbering, determines the edge centers.
        struct Box { int slr; float x0,x1,y0,y1; };
        std::vector<Box> boxes{{4,0,45,100,260},{4,55,100,100,260},
                               {9,0,100,0,100},{2,0,100,260,400}};
        std::vector<hierarchical_boundary::ClockRegionCenter> cr{
            {8,99,10,120},{2,99,35,120},{1,99,70,120},{7,99,95,120},
            {8,3,10,225},{2,3,35,225},{1,3,70,225},{7,3,95,225},
            {8,7,10,20},{8,8,10,75},{8,4,10,290},{8,0,10,370}};
        hierarchical_boundary::ClockRegionAnchor anchor;
        auto selectCR=[&](int region,bool gx,bool gy,int dx,double mean) {
            return hierarchical_boundary::selectClockRegionAnchor(boxes,region,gx,gy,dx,mean,cr,anchor);
        };
        require(selectCR(0,true,true,-1,150) && anchor.x==35 && anchor.y==120 &&
                anchor.pullY==1 && anchor.column==2 && anchor.row==99,"Lower CR edge selection failed");
        require(selectCR(1,true,true,1,210) && anchor.x==70 && anchor.y==225 &&
                anchor.pullY==-1 && anchor.row==3,"Upper/right CR edge selection failed");
        require(selectCR(0,false,true,0,180) && anchor.y==120 && anchor.pullY==1,
                "Exact mean-Y tie is not deterministic");
        require(selectCR(2,false,true,0,10) && anchor.y==75 && anchor.pullY==-1,
                "Bottom die selected its exterior edge");
        require(selectCR(3,false,true,0,390) && anchor.y==290 && anchor.pullY==1,
                "Top die selected its exterior edge");
        require(!selectCR(-1,true,true,1,150) && !selectCR(0,true,true,0,150),"Invalid CR request accepted");
        require(!selectCR(0,true,true,-1,std::numeric_limits<double>::quiet_NaN()),"Nonfinite mean accepted");
        cr.clear();require(!selectCR(0,true,true,-1,150),"Missing CR geometry fell back to region center");
        std::cout << "PASS majority, stretch, original/rotated weights and CR edge geometry\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
