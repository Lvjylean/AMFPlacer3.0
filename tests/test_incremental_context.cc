#include "../src/lib/HiFPlacer/placement/placementInfo/IncrementalContext.h"
#include <cassert>
#include <filesystem>
#include <functional>

int main(int argc, char **argv)
{
    assert(argc == 2);
    std::filesystem::path root(argv[1]);
    auto write = [&](const char *name, const char *text) {std::ofstream(root/name) << text;};
    auto rejects = [](std::function<void()> fn) {
        bool threw=false; try {fn();} catch (const std::runtime_error &) {threw=true;} assert(threw);
    };
    IncrementalContext off({}); assert(!off.enabled);
    IncrementalContext disabled({{"IncremenralCompilation","false"},{"DFX active partition","bad/"}});
    assert(!disabled.enabled); // disabled mode does not read DFX files
    rejects([] {IncrementalContext c(std::map<std::string,std::string>{{"IncremenralCompilation","TRUE"}});});
    rejects([] {IncrementalContext c(std::map<std::string,std::string>{{"IncremenralCompilation","true"}});});
    write("allow", "SLICE_X1Y1\nSLICE_X1Y2\n");
    write("block", "SLICE_X1Y2\n");
    write("fixed", "cell\tsite\tbel\nstatic/a\tSLICE_X2Y2\tAFF\n");
    std::map<std::string,std::string> cfg{{"IncremenralCompilation","true"},
        {"DFX active partition","rp"},{"DFX allowed sites file",(root/"allow").string()},
        {"DFX blocked sites file",(root/"block").string()},
        {"DFX static placement file",(root/"fixed").string()}};
    IncrementalContext c(cfg);
    assert(c.enabled && c.isActive("rp/a") && !c.isActive("rp2/a"));
    assert(c.siteAllowed("SLICE_X1Y1") && !c.siteAllowed("SLICE_X1Y2") && !c.siteAllowed("SLICE_X2Y2"));
    write("fixed", "cell\tsite\tbel\nrp/a\tSLICE_X1Y1\tAFF\n");
    rejects([&] {IncrementalContext invalid(cfg);});
    write("fixed", "cell\tsite\tbel\nstatic/a\tSLICE_X2Y2\tAFF\nstatic/a\tSLICE_X2Y2\tBFF\n");
    rejects([&] {IncrementalContext invalid(cfg);});
    write("fixed", "cell\tsite\tbel\n");
    rejects([&] {IncrementalContext invalid(cfg);});
    cfg["DFX input scope"] = "rm-boundary-cone-v1";
    IncrementalContext isolated(cfg); assert(isolated.fixed.empty() && isolated.enabled);
    cfg["DFX input scope"] = "typo";
    rejects([&] {IncrementalContext invalid(cfg);});
    return 0;
}
