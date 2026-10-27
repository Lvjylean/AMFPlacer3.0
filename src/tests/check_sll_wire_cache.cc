#include "../lib/HiFPlacer/placement/placementInfo/SllWireCache.h"
#include <iostream>
#include <random>
#include <omp.h>
using namespace amf_sll_plan;
static double reference(const std::vector<WireSnapshotPoint>& pins, double ratio, int unit, Point trial) {
    if (pins.size() < 2) return 0;
    TileBox box;
    for (const auto& pin : pins) {
        Point p = pin.position;
        if (unit >= 0 && pin.unit == unit) { p.x += trial.x - pin.unitOrigin.x; p.y += trial.y - pin.unitOrigin.y; }
        box.add(p);
    }
    return box.x1-box.x0 + ratio*(box.y1-box.y0);
}
int main() {
    std::mt19937 random(20261028);
    std::uniform_real_distribution<double> coordinate(-1000, 1000);
    size_t checks = 0;
    for (int design = 0; design < 160; ++design) {
        std::vector<Point> origins;
        for (int i=0;i<40;++i) origins.push_back({coordinate(random),coordinate(random)});
        WireSnapshotNet cached;
        std::vector<std::vector<WireSnapshotPoint>> original;
        for (int tree=0;tree<4;++tree) {
            std::vector<WireSnapshotPoint> pins;
            const int size = design < 8 ? design : (design % 2 ? 4096 : 64);
            for (int i=0;i<size;++i) {
                const int unit=(i%7==0)?-1:int(random()%40);
                const Point origin=unit<0?Point{}:origins[unit];
                // Shared PU pins, tied extrema, negative positions and fixed endpoints.
                const Point offset=design%3?Point{double(int(random()%9)-4),double(int(random()%9)-4)}:Point{};
                pins.push_back({{origin.x+offset.x,origin.y+offset.y},origin,unit});
            }
            original.push_back(pins);cached.trees.emplace_back(pins);
        }
        int errors=0;
        #pragma omp parallel for reduction(+:errors)
        for (int query=0;query<500;++query) {
            const int unit=query%43-1;
            const Point trial{double(query%29)*77-700,double(query%31)*65-900};
            const double ratio=query%3==0?0:query%3==1?.4:2.1;
            double wanted=0;for(const auto& tree:original)wanted+=reference(tree,ratio,unit,trial);
            const double actual=cached.length(ratio,unit,trial);
            if (float(actual)!=float(wanted) || std::abs(actual-wanted)>1e-10*std::max(1.0,std::abs(wanted))) ++errors;
        }
        if(errors){std::cerr<<"Wire snapshot mismatch: "<<errors<<'\n';return 1;}
        checks+=500;
    }
    // A tree containing only the moving PU: exclusion must leave an empty box.
    std::vector<WireSnapshotPoint> only{{{10,20},{5,6},3},{{12,25},{5,6},3}};
    WireSnapshotTree tree(only);
    if(tree.length(.4,3,{100,200})!=reference(only,.4,3,{100,200}))return 2;
    std::cout<<"PASS "<<checks<<" parallel reference queries; shared PU pins, ties, fixed endpoints, empty trees and high fanout\n";
}
