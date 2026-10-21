#ifndef AMF_DEVICE_PLACEMENT_POLICY_H
#define AMF_DEVICE_PLACEMENT_POLICY_H

#include <map>
#include <stdexcept>
#include <string>

namespace amf_device
{
enum class Kind { VU095, U250, VHK158 };

// Device policy is orthogonal to clustering strategy, phase scheduling and DFX.
// U250 deliberately retains the validated 78 behavior; this is not a new
// U250 floorplanning/attraction algorithm.
struct PlacementPolicy
{
    Kind kind;
    std::string name;
    bool clockRegionColumnStretch;

    bool shouldStretch(bool boundaryAware, bool noBlockAware) const
    {
        return clockRegionColumnStretch && !boundaryAware && !noBlockAware;
    }

    bool selectedColumnAttraction(bool multiSlrPlacement) const
    {
        // Preserve the existing U250 experimental-multi-SLR gate as well.
        // VU095 uses the original column-2/left-column-1/right-column-3 rule.
        return kind == Kind::U250 && multiSlrPlacement;
    }

    bool legacySideConstraint(bool phaseEnabled, bool boundaryAware) const
    {
        // The inherited side constraint is intentionally unchanged on U250
        // in this framework-only revision. Future rules get their own case.
        switch (kind)
        {
        case Kind::VU095:
        case Kind::U250: return phaseEnabled && !boundaryAware;
        case Kind::VHK158: return false; // sparse single-clock LUT/FF pilot only.
        }
        return false;
    }
};

inline std::string canonicalDeviceName(const std::string &device)
{
    return device == "VU095" ? "VCU108" : device;
}

inline PlacementPolicy resolve(const std::map<std::string, std::string> &config,
                               const std::string &device)
{
    PlacementPolicy policy{Kind::VU095, "vu095-amf2pro", true};
    const auto canonical = canonicalDeviceName(device);
    if (canonical == "VCU108")
        policy = {Kind::VU095, "vu095-amf2pro", true};
    else if (canonical == "U250")
        policy = {Kind::U250, "u250-current", false};
    else if (canonical == "VHK158")
    {
        auto is = [&](const char *key, const char *value) {
            auto it = config.find(key); return it != config.end() && it->second == value;
        };
        if (!is("VHK158LutFfPilot", "true") || !is("noBlockAware", "true") ||
            !is("BoundaryAwareClustering", "false") || !is("PhysicalBoundaryMode", "false") ||
            !config.count("clock region geometry file") || config.at("clock region geometry file").empty() ||
            is("ClockRegionColumnStretch", "true") || is("IncrementalCompilation", "true") ||
            is("IncremenralCompilation", "true") || is("ProPrimitiveTiming", "true") ||
            is("ProLegacyIOBoundary", "true"))
            throw std::invalid_argument("VHK158 requires the guarded sparse CR single-clock LUT/FF pilot; advanced boundary/DFX policies are unsupported");
        policy = {Kind::VHK158, "vhk158-sparse-lutff-pilot", false};
    }
    else
        throw std::invalid_argument("Unknown placement device '" + device +
                                    "'; expected VCU108 (VU095), U250 or guarded VHK158");
    const auto stretch = config.find("ClockRegionColumnStretch");
    if (stretch != config.end())
    {
        if (stretch->second != "true" && stretch->second != "false")
            throw std::invalid_argument("ClockRegionColumnStretch must be true or false");
        policy.clockRegionColumnStretch = stretch->second == "true";
    }
    return policy;
}
} // namespace amf_device
#endif
