#include "../lib/HiFPlacer/placement/globalPlacement/CrossDieSpreadLimit.h"
#include "../lib/utils/PlacementFeatureConfig.h"
#include <iostream>

static void check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
struct PU {
    unsigned id; float x, y;
    unsigned getId() const { return id; }
    float X() const { return x; }
    float Y() const { return y; }
};
int main() { try {
    PU pu{2, 30, 200}; std::vector<PU*> units{&pu};
    CrossDieSpreadLimit limit;
    for (double bin : {5., 2.5, 2.}) {
        limit.reset(20, bin, bin, units); limit.activate(2);
        auto p = limit.project(2, {330, 600});
        check(std::abs(p.x - (30 + 12 * bin)) < 1e-4, "diagonal X / stage radius");
        check(std::abs(p.y - (200 + 16 * bin)) < 1e-4, "diagonal Y / stage radius");
        check(limit.contains(2, p), "projected point outside disk");
        auto inside = limit.project(2, {33, 204});
        check(inside.x == 33 && inside.y == 204, "unlimited short move changed");
        for (int i=0; i<50; ++i) {
            // Successive writes/pass-local history cannot refresh the origin.
            pu.x=p.x; pu.y=p.y; limit.activate(2);
            p=limit.project(2, {pu.x+300, pu.y+400});
            check(limit.contains(2,p), "repeated pass escaped round budget");
            check(limit.origin(2).x==30 && limit.origin(2).y==200, "origin refreshed");
        }
        pu.x=30;pu.y=200;
    }
    limit.reset(20, 5, 2, units); limit.activate(2);
    auto anisotropic=limit.project(2,{330,360});
    check(std::abs(anisotropic.x-90)<1e-4 && std::abs(anisotropic.y-232)<1e-4,
          "non-square bins or ray direction");
    auto negative=limit.project(2,{-270,40});
    check(std::abs(negative.x+30)<1e-4 && std::abs(negative.y-168)<1e-4,"negative ray");
    pu.x=90;pu.y=232;limit.reset(20,5,2,units);
    check(limit.origin(2).x==90 && !limit.active(2),"new round did not reset");
    limit.reset(0,5,5,units); check(!limit.enabled() && !limit.active(2),"disabled budget");
    check(amf_config::crossDieSpreadMaxBins({})==20,"default radius");
    check(amf_config::crossDieSpreadMaxBins({{"CrossDieSpreadMaxBins","0"}})==0,"off switch");
    for (auto bad : {"-1", "nan", "inf", "20bin", ""}) {
        bool failed=false;
        try { amf_config::crossDieSpreadMaxBins({{"CrossDieSpreadMaxBins",bad}}); }
        catch (const std::exception&) { failed=true; }
        check(failed,"invalid configuration accepted");
    }
    std::cout<<"PASS stage radii 100/50/40, ray direction, non-square bins, cumulative budget, round reset, config validation\n";
} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
