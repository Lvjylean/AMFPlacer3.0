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

    bool saLegacyColumnPenalty() const
    {
        // The inherited grid-column-2 surcharge is a VU095 policy, not a
        // device-independent distance term. U250 uses the base SA distance.
        return kind == Kind::VU095;
    }

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
        case Kind::VHK158: return false; // resolve() refuses this pending profile.
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
        throw std::invalid_argument("device=VHK158 placement policy is reserved but not implemented");
    else
        throw std::invalid_argument("Unknown placement device '" + device +
                                    "'; expected VCU108 (VU095), U250 or reserved VHK158");
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
