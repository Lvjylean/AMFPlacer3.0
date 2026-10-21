#ifndef AMF_BOUNDARY_CLUSTERING_STRATEGY_H
#define AMF_BOUNDARY_CLUSTERING_STRATEGY_H

#include <map>
#include <stdexcept>
#include <string>

namespace boundary_strategy {
// Canonical user-facing name. Keep old inputs usable without making the
// legacy region-gain path an implicit fallback.
inline std::string resolve(const std::map<std::string, std::string> &config)
{
    const auto it = config.find("BoundaryClusteringStrategy");
    if (it == config.end() || it->second == "slr-hierarchical" || it->second == "paper-hierarchical")
        return "slr-hierarchical";
    if (it->second == "cr-slr-hierarchical") return "cr-slr-hierarchical";
    if (it->second == "region-gain") return "region-gain";
    throw std::runtime_error("Unknown BoundaryClusteringStrategy: " + it->second);
}
}
#endif

