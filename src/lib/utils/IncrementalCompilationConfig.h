#ifndef AMF_INCREMENTAL_COMPILATION_CONFIG_H
#define AMF_INCREMENTAL_COMPILATION_CONFIG_H
#include <map>
#include <stdexcept>
#include <string>

namespace amf_config {
inline const char *incrementalKey() { return "IncrementalCompilation"; }
inline const char *legacyIncrementalKey() { return "IncremenralCompilation"; }

inline bool incrementalEnabled(const std::map<std::string, std::string> &cfg)
{
    auto current = cfg.find(incrementalKey()), legacy = cfg.find(legacyIncrementalKey());
    for (auto it : {current, legacy})
        if (it != cfg.end() && it->second != "true" && it->second != "false")
            throw std::runtime_error(it->first + " must be true or false");
    if (current != cfg.end() && legacy != cfg.end() && current->second != legacy->second)
        throw std::runtime_error("Conflicting IncrementalCompilation and legacy IncremenralCompilation");
    auto selected = current != cfg.end() ? current : legacy;
    return selected != cfg.end() && selected->second == "true";
}

// Normalize before logging/forwarding; never emit the legacy spelling.
inline void normalizeIncrementalCompilation(std::map<std::string, std::string> &cfg)
{
    const bool enabled = incrementalEnabled(cfg);
    if (cfg.count(incrementalKey()) || cfg.count(legacyIncrementalKey()))
        cfg[incrementalKey()] = enabled ? "true" : "false";
    cfg.erase(legacyIncrementalKey());
}
}
#endif
