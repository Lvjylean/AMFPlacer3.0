#include "../lib/HiFPlacer/placement/placementInfo/GridUpdatePolicy.h"
#include <cassert>
#include <iostream>
#include <random>
#include <set>
#include <omp.h>

struct Record { size_t bin; int id; float occupation; std::vector<int> types; };
int main()
{
    std::mt19937 rng(912731);
    size_t checked = 0;
    for (int trial = 0; trial < 200; ++trial) {
        const size_t bins = 1 + rng()%113, n = rng()%3000;
        std::vector<Record> records;
        std::vector<int> capacity(bins*3);
        std::vector<float> shrink(bins*3);
        for (size_t b=0;b<bins*3;++b) { capacity[b]=rng()%21; shrink[b]=float(1+rng()%10)/10; }
        for (size_t i=0;i<n;++i) {
            int type=rng()%3;
            records.push_back({rng()%bins,int(i),float(rng()%17)/4,{type}});
            if (rng()%2) records.back().types.push_back((type+1)%3);
        }
        std::vector<int> expected(n), used(bins*3,0);
        // Independent transcription of the legacy global visitation loop.
        for (const auto &r : records) {
            int chosen = r.types.front();
            for (int type : r.types) {
                const size_t k=r.bin*3+type;
                if (capacity[k] && float(used[k]+int(r.occupation))/(shrink[k]*capacity[k]) <= 1.00+1e-5) {
                    chosen=type; break;
                }
            }
            expected[r.id]=chosen;
            used[r.bin*3+chosen]+=int(r.occupation);
        }
        std::vector<size_t> offsets, order;
        grid_update::stableBuckets(records,bins,offsets,order);
        for (int threads : {1,2,4,8}) {
            std::vector<int> got(n,-1), actual(bins*3,0);
#pragma omp parallel for num_threads(threads) schedule(static)
            for (size_t b=0;b<bins;++b) {
                int previous=-1;
                for (size_t j=offsets[b];j<offsets[b+1];++j) {
                    const auto &r=records[order[j]];
                    assert(r.id>previous); previous=r.id;
                    int type=grid_update::chooseType(r.types,[&](int t) {
                        const size_t k=b*3+t;
                        return capacity[k] && float(actual[k]+int(r.occupation))/(shrink[k]*capacity[k]) <= 1.00+1e-5;
                    });
                    got[r.id]=type; actual[b*3+type]+=int(r.occupation);
                }
            }
            assert(got==expected); assert(actual==used); ++checked;
        }
    }
    assert(grid_update::sameMembers(std::set<int>{1,3},std::vector<int>{3,1}));
    assert(!grid_update::sameMembers(std::set<int>{1,3},std::vector<int>{1,2}));
    assert(!grid_update::sameMembers(std::set<int>{1,3},std::vector<int>{1}));
    // Existing int-Y transfer can disagree with the floating full rebuild.
    const float startY=-0.5f, height=2.0f, y=1.75f;
    const int full=int((double(y)-startY)/height);
    const int transfer=int((double(int(y))-startY)/height);
    assert(full!=transfer);
    std::cout << "PASS " << checked << " deterministic bucket/assignment cases at 1/2/4/8 threads; member changes; Y truncation counterexample (not repaired)\n";
}
