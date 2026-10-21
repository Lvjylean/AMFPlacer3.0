#include "../lib/HiFPlacer/placement/placementInfo/HardRegionConstraints.h"
#include "../lib/utils/simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
using Config = std::map<std::string, std::string>;
int checks = 0;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("check " + std::to_string(checks)); }
template<class F> void reject(F f) {
    bool failed = false;
    try { f(); } catch (const std::runtime_error &) { failed = true; }
    check(failed);
}
int main() { try {
    auto dir = std::filesystem::temp_directory_path() / ("amf-config-key-" + std::to_string(getpid()));
    std::filesystem::create_directory(dir);
    auto file = [&](const std::string &name, const std::string &data) {
        auto p = dir / name; std::ofstream(p) << data; return p.string();
    };
    check(!IncrementalContext(Config{}).enabled && !HardRegionConstraints(Config{}).enabled);
    Config rm{{"DFX placement mode", "incremental"}, {"DFX active partition", "top/a"},
              {"DFX allowed sites file", file("allowed", "S1\nS2\n")},
              {"DFX blocked sites file", file("blocked", "S2\n")},
              {"DFX static placement file", file("static", "cell\tsite\tbel\ns/ff\tS0\tAFF\n")}};
    Config full{{"DFX placement mode", "full"},
                {"DFX regions file", file("regions", "region\tinstance\nstatic\t*\na\ttop/a\n")},
                {"DFX region sites file", file("sites", "region\tsite\nstatic\tS0\na\tS1\n")}};
    for (const std::string key : {"IncrementalCompilation", "IncremenralCompilation"}) {
        auto a = rm; a[key] = "true"; IncrementalContext inc(a);
        check(inc.enabled && inc.fixed.at("s/ff").site == "S0" && inc.siteAllowed("S1") && !inc.siteAllowed("S2"));
        auto b = full; b[key] = "true"; HardRegionConstraints hard(b);
        check(hard.enabled && hard.cellRegion("top/a/ff") == 1 && !hard.allows(1, "S0"));
        check(!IncrementalContext(b).enabled);
        check(!IncrementalContext(Config{{key, "false"}}).enabled);
        const auto path = file(key + ".json", "{\"" + key + "\":\"false\"}");
        auto parsed = parseJSONFile(path);
        check(parsed.at("IncrementalCompilation") == "false" && !parsed.count("IncremenralCompilation"));
    }
    auto same = rm; same["IncrementalCompilation"] = same["IncremenralCompilation"] = "true";
    check(IncrementalContext(same).enabled);
    amf_config::normalizeIncrementalCompilation(same);
    check(same.at("IncrementalCompilation") == "true" && !same.count("IncremenralCompilation"));
    for (const std::string value : {"true", "false"}) {
        Config conflict{{"IncrementalCompilation", value}, {"IncremenralCompilation", value == "true" ? "false" : "true"}};
        reject([&] { IncrementalContext ignored(conflict); });
        auto cf = full; cf.insert(conflict.begin(), conflict.end());
        reject([&] { HardRegionConstraints ignored(cf); });
    }
    for (const std::string key : {"IncrementalCompilation", "IncremenralCompilation"})
        reject([&] { IncrementalContext ignored(Config{{key, "yes"}}); });
    reject([&] { HardRegionConstraints ignored(full); });
    auto conflictFile = file("conflict.json", "{\"IncrementalCompilation\":\"false\",\"IncremenralCompilation\":\"true\"}");
    reject([&] { parseJSONFile(conflictFile); });
    std::cout << "incremental configuration: " << checks << " checks passed\n";
    std::filesystem::remove_all(dir);
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
