#ifndef AMF_SA_CROSS_DIE_COMPENSATION_H
#define AMF_SA_CROSS_DIE_COMPENSATION_H

#include "DevicePlacementPolicy.h"
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

namespace amf_sa_cross_die
{
// GETRF/U250 routed low-distance sample: 130 branches, same CR column and
// adjacent CR rows across one seam. This is a full-connection reference,
// not 1.415 ns added on top of a complete ordinary connection.
constexpr double referenceDelayNs = 1.415;
// Inverse of the current ordinary U250 far-distance model at dx=0.
// Calibration provenance and coefficients are recorded in the research note.
constexpr double equivalentVerticalDistance = 182.69021393960458;

inline bool enabled(const std::map<std::string, std::string> &config, amf_device::Kind device)
{
    const auto entry = config.find("SACrossDieCompensationEnable");
    if (entry == config.end()) return device == amf_device::Kind::U250;
    if (entry->second != "true" && entry->second != "false")
        throw std::invalid_argument("SACrossDieCompensationEnable must be true or false");
    if (entry->second == "false") return false;
    if (device != amf_device::Kind::U250)
        throw std::invalid_argument("SA cross-die calibration is only available for U250");
    return true;
}

inline double extraVerticalDistance(double referenceRowPitch)
{
    if (!std::isfinite(referenceRowPitch) || referenceRowPitch <= 0 ||
        referenceRowPitch >= equivalentVerticalDistance)
        throw std::invalid_argument("Invalid reference CR row pitch for SA cross-die calibration");
    return equivalentVerticalDistance - referenceRowPitch;
}
} // namespace amf_sa_cross_die
#endif
