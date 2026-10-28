#include "RegionColumnDP.h"
#include <cassert>
#include <iostream>
#include <set>
#include <vector>
int main() {
    int n,m;std::cin>>n>>m;assert(n==71 && m==956);
    std::vector<int> ids(n),regions(n),height(n),ys(m),owners(m),bottoms;
    for(int i=0;i<n;++i)std::cin>>ids[i]>>regions[i]>>height[i];
    for(int j=0;j<m;++j)std::cin>>ys[j]>>owners[j];
    assert(std::cin.good());
    auto valid=[&](const std::vector<int>& indices,int region,int start,int h) {
        for(int k=0;k<h;++k) {
            int j=indices[start+k];
            if(owners[j]!=region || ys[j]!=ys[indices[start]]+k) return false;
        }
        return true;
    };
    std::vector<int> all(m);for(int j=0;j<m;++j)all[j]=j;
    bool mixed=AMFRegionColumnDP::solveOrdered(height,m,
        [&](int i,int s,int h){return valid(all,regions[i],s,h);},
        [](int,int){return 0.0;},bottoms);
    assert(!mixed);
    std::vector<int> placed(n,-1);
    for(const auto& e:AMFRegionColumnDP::partition(regions,owners)) {
        auto& g=e.second;std::vector<int> hs;
        for(int i:g.items)hs.push_back(height[i]);
        assert(AMFRegionColumnDP::solveOrdered(hs,g.sites.size(),
            [&](int,int s,int h){return valid(g.sites,e.first,s,h);},
            [](int,int){return 0.0;},bottoms));
        for(int i=0;i<(int)g.items.size();++i)placed[g.items[i]]=ys[g.sites[bottoms[i]]];
    }
    std::set<int> occupied;
    for(int i=0;i<n;++i) {
        assert(placed[i]>=0);
        for(int k=0;k<height[i];++k)assert(occupied.insert(placed[i]+k).second);
        // All produced ranges are within a single 60-row clock region, which
        // also proves they cannot cross an SLR boundary on this fixture.
        assert(placed[i]/60==(placed[i]+height[i]-1)/60);
        std::cout<<ids[i]<<"\t"<<regions[i]<<"\t"<<height[i]<<"\t"<<placed[i]<<"\n";
    }
    assert(occupied.size()==132);
    std::cerr<<"PASS complete captured column: 71 macros / 132 rows, mixed order infeasible, grouped order legal, no overlap\n";
}

