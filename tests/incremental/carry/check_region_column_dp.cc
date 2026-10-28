#include "RegionColumnDP.h"
#include "ColumnOverflow.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <random>

using namespace AMFRegionColumnDP;

int main()
{
    // The real first failure: B(3), static(2), B(2). Static sites are outside
    // B's Y301..358 interval. Fixed mixed order is impossible, while each
    // region's internally ordered list has ample legal contiguous space.
    std::vector<int> owners(960, 0), physicalY(960);
    for (int j = 0; j < 960; ++j)
    {
        physicalY[j] = j;
        if (j >= 301 && j <= 358) owners[j] = 2;
        if (j == 300 || j == 359) owners[j] = -1;
    }
    const std::vector<int> regions{2, 0, 2}, heights{3, 2, 2};
    auto legalBlock = [&](int region, const std::vector<int> &ys, int start, int h) {
        for (int k = 0; k < h; ++k)
            if (owners[ys[start+k]] != region || ys[start+k] != ys[start]+k) return false;
        return true;
    };
    std::vector<int> bottoms;
    assert(!solveOrdered(heights, 960,
        [&](int i,int s,int h){return legalBlock(regions[i],physicalY,s,h);},
        [](int,int){return 0.0;},bottoms));
    std::vector<int> placed(3, -1);
    for (const auto &entry : partition(regions,owners))
    {
        const auto &g = entry.second;
        std::vector<int> hs;
        for (int i : g.items) hs.push_back(heights[i]);
        assert(solveOrdered(hs,g.sites.size(),
            [&](int,int s,int h){return legalBlock(entry.first,g.sites,s,h);},
            [](int,int){return 0.0;},bottoms));
        for (int k=0;k<static_cast<int>(g.items.size());++k) placed[g.items[k]]=g.sites[bottoms[k]];
    }
    assert(placed[0] >= 301 && placed[2] > placed[0] && placed[2]+1 <= 358);
    assert(placed[1]+1 < 301 || placed[1] > 358);

    // Two free rows on opposite sides of a gap are not a two-row macro site.
    std::vector<int> gap{299,360};
    assert(!solveOrdered({2},2,[&](int,int s,int h){return legalBlock(0,gap,s,h);},
                         [](int,int){return 0.0;},bottoms));
    // Capacity belongs to each region, not to their sum.
    assert(!solveOrdered({3,2},4,[](int,int,int){return true;},
                         [](int,int){return 0.0;},bottoms));
    assert(solveOrdered({},0,[](int,int,int){return true;},
                        [](int,int){return 0.0;},bottoms));

    // Independent exhaustive oracle: compare optimum and feasibility on small
    // columns with holes, macro heights, fixed-position restrictions and signed
    // costs. This checks traceback, not just the DP's own recurrence.
    std::mt19937 random(20261016);
    for (int trial=0;trial<500;++trial)
    {
        int n=1+random()%4,m=1+random()%9;
        std::vector<int> hs(n),allowed(n*m),cost(n*m);
        for(int &h:hs) h=1+random()%3;
        for(int &a:allowed) a=random()%4!=0;
        for(int &c:cost) c=static_cast<int>(random()%19)-9;
        double best=std::numeric_limits<double>::infinity();
        std::function<void(int,int,double)> enumerate=[&](int i,int first,double sum) {
            if(i==n){best=std::min(best,sum);return;}
            for(int s=first;s+hs[i]<=m;++s)
                if(allowed[i*m+s]) enumerate(i+1,s+hs[i],sum+cost[i*m+s]);
        };
        enumerate(0,0,0);
        bool ok=solveOrdered(hs,m,[&](int i,int s,int){return allowed[i*m+s]!=0;},
                            [&](int i,int s){return cost[i*m+s];},bottoms);
        assert(ok==std::isfinite(best));
        if(ok)
        {
            double sum=0;int end=0;
            for(int i=0;i<n;++i)
            {
                int s=bottoms[i];assert(s>=end && s+hs[i]<=m && allowed[i*m+s]);
                sum+=cost[i*m+s];end=s+hs[i];
            }
            assert(sum==best);
        }
    }
    using namespace AMFColumnOverflow;
    assert(directionalReliefCeil(1,.476)==1);
    assert(directionalReliefCeil(2,.476)==1);
    assert(directionalReliefCeil(1,0)==0);
    assert(!wholeMacroFitsBudget(47,2,956,.05f));
    assert(wholeMacroFitsBudget(45,2,956,.05f));
    assert(!wholeMacroFitsBudget(0,2,1,1));
    std::cout<<"PASS: region-order conflict, gaps, regional shortage, empty input, 500 exhaustive cases, whole-macro ceiling/budget checks\n";
}
