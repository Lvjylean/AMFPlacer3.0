#ifndef AMF_TIMING_WEIGHT_GUARD_H
#define AMF_TIMING_WEIGHT_GUARD_H
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace TimingWeightGuard {
// Zero preserves the original formula. A finite cap is an explicit experiment parameter.
inline double parseCap(const std::string &text) {
    size_t used = 0;
    double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value) ||
        (value != 0 && (value < 1 || value > 1000000)))
        throw std::invalid_argument("TimingMaxEnhancement must be 0 or finite in [1,1000000]");
    return value;
}
inline float enhancement(float slack, float clock, double power, float threshold, double cap,
                         bool nested = true) {
    if (cap == 0) {
        float value = std::pow(1 - slack / clock, power);
        if (nested && slack < threshold)
            value = std::pow(value, slack / threshold * 3);
        return value;
    }
    if (!std::isfinite(slack) || !std::isfinite(clock) || clock <= 0 ||
        !std::isfinite(power) || power < 0 || !std::isfinite(threshold) || slack > clock)
        return std::numeric_limits<float>::quiet_NaN();
    // Evaluate before exponentiation; clamping an already-overflowed float is too late.
    double logValue = power * std::log1p(-double(slack) / clock);
    if (nested && slack < threshold) {
        if (threshold >= 0) return float(cap); // zero-slack histogram bucket
        logValue *= double(slack) / threshold * 3;
    }
    return float(std::exp(std::min(logValue, std::log(cap))));
}
}
#endif
