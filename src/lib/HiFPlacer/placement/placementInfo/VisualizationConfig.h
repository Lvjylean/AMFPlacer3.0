#pragma once
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

namespace visualization_trace {
// Normalize once, before any component copies the placer configuration.
inline void configure(std::map<std::string, std::string>& cfg,
                      const std::string& configFile) {
    auto option = cfg.find("visualization");
    if (option == cfg.end()) return; // Preserve legacy configurations.
    if (option->second != "true" && option->second != "false")
        throw std::invalid_argument("visualization must be true or false");
    const bool active = option->second == "true";
    cfg["VisualizationTraceFine"] = active ? "true" : "false";
    for (const char* key : {"DumpAllCoordTrace", "DumpLUTCoordTrace",
             "DumpDSPCoordTrace", "DumpCARRYCoordTrace", "DumpFFCoordTrace",
             "DumpLUTFFCoordTrace", "DumpBRAMCoordTrace"}) cfg.erase(key);
    if (!active) return;
    namespace fs = std::filesystem;
    auto dump = cfg.find("dumpDirectory");
    const fs::path base = dump != cfg.end() && !dump->second.empty()
        ? fs::absolute(dump->second) : fs::absolute(configFile).parent_path();
    const fs::path directory = base / "visualization";
    if (fs::exists(directory) && !fs::is_empty(directory))
        throw std::runtime_error("visualization output directory is not empty: " + directory.string());
    fs::create_directories(directory);
    cfg["DumpAllCoordTrace"] = (directory / "DumpAllCoordTrace").string();
}
}
