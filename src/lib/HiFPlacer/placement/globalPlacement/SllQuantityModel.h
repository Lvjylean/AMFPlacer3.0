#ifndef AMF_SLL_QUANTITY_MODEL_H
#define AMF_SLL_QUANTITY_MODEL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

// A routing-independent, vertical-SLR demand model. Integer demand is NOT
// routed SLL usage. Pin offsets include both macro-cell and cell-pin offsets.
namespace amf_sll {
struct Pin { unsigned unit; double offsetY; };
struct Net {
    std::size_t begin, end;
    unsigned id;
    bool optimize = true;
    double weight = 1.0;
};
struct Result {
    std::int64_t discrete = 0, discreteOptimized = 0;
    std::size_t invalidNets = 0, validNets = 0;
    double smooth = 0;
    std::vector<std::int64_t> seams;
    std::vector<double> gradient;
};

inline double sigmoid(double v) {
    if (v >= 0) return 1.0 / (1.0 + std::exp(-v));
    const double e = std::exp(v);
    return e / (1.0 + e);
}

struct Model {
    std::vector<double> boundaries, tau;
    double gamma = 0.25;
    double minY = 0, maxY = 1;
    std::size_t units = 0;
    std::vector<Pin> pins;
    std::vector<Net> nets;

    void validate() const {
        if (!units || boundaries.empty() || boundaries.size() != tau.size() ||
            !std::isfinite(gamma) || gamma <= 0 || !std::isfinite(minY) ||
            !std::isfinite(maxY) || minY >= maxY)
            throw std::invalid_argument("Invalid SLL model dimensions/temperatures/domain");
        double previous = minY;
        for (std::size_t b = 0; b < boundaries.size(); ++b) {
            if (!(boundaries[b] > previous && boundaries[b] < maxY) ||
                !std::isfinite(tau[b]) || tau[b] <= 0)
                throw std::invalid_argument("Invalid SLL physical boundary or soft width");
            previous = boundaries[b];
        }
        for (const auto &pin : pins)
            if (pin.unit >= units || !std::isfinite(pin.offsetY))
                throw std::invalid_argument("Invalid SLL pin/PU mapping");
        std::size_t previousEnd = 0;
        for (const auto &net : nets) {
            if (net.begin != previousEnd || net.end < net.begin || net.end > pins.size() ||
                !std::isfinite(net.weight) || net.weight < 0)
                throw std::invalid_argument("Invalid SLL net mapping/weight");
            previousEnd = net.end;
        }
        if (previousEnd != pins.size()) throw std::invalid_argument("Unowned SLL pins");
    }

    // Physical regions use y0 < y <= y1: an exact seam belongs to its lower SLR.
    int slr(double y) const {
        return int(std::lower_bound(boundaries.begin(), boundaries.end(), y) - boundaries.begin());
    }

    Result evaluate(const std::vector<double> &y, const std::vector<unsigned char> &movable,
                    int threads, bool needGradient) const {
        if (y.size() != units || movable.size() != units || threads < 1)
            throw std::invalid_argument("Invalid SLL evaluation dimensions/threads");
        for (double v : y)
            if (!std::isfinite(v)) throw std::invalid_argument("Non-finite SLL coordinate");
#ifndef _OPENMP
        threads = 1;
#endif
        threads = std::min(threads, int(std::max(std::size_t(1), nets.size())));
        std::vector<Result> partial(threads);
        for (auto &r : partial) {
            r.seams.assign(boundaries.size(), 0);
            if (needGradient) r.gradient.assign(units, 0);
        }
#ifdef _OPENMP
#pragma omp parallel num_threads(threads)
#endif
        {
            int tid = 0;
#ifdef _OPENMP
            tid = omp_get_thread_num();
#endif
            auto &out = partial[tid];
            std::vector<double> z, dz, plus, minus;
#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
            for (std::int64_t ni = 0; ni < std::int64_t(nets.size()); ++ni) {
                const auto &net = nets[ni];
                const auto count = net.end - net.begin;
                if (!count) continue;
                int lo = int(boundaries.size()), hi = 0;
                bool valid = true;
                for (std::size_t j = net.begin; j < net.end; ++j) {
                    const auto &pin = pins[j];
                    const double py = y[pin.unit] + pin.offsetY;
                    if (!std::isfinite(py) || py < minY || py > maxY) { valid = false; break; }
                    const int s = slr(py);
                    lo = std::min(lo, s); hi = std::max(hi, s);
                }
                if (!valid) ++out.invalidNets;
                else {
                    ++out.validNets;
                    out.discrete += hi - lo;
                    for (int b = lo; b < hi; ++b) ++out.seams[b];
                }
                if (!net.optimize) continue;
                if (valid) out.discreteOptimized += hi - lo;
                // Keep the smooth cost even outside the known physical domain;
                // moving a pin out of bounds must never erase an objective term.
                if (count < 2 || net.weight == 0) continue;
                z.assign(count, 0); dz.assign(count, 0);
                plus.resize(count); minus.resize(count);
                double zlo = std::numeric_limits<double>::infinity(), zhi = -zlo;
                for (std::size_t j = 0; j < count; ++j) {
                    const auto &pin = pins[net.begin + j];
                    const double py = y[pin.unit] + pin.offsetY;
                    for (std::size_t b = 0; b < boundaries.size(); ++b) {
                        const double s = sigmoid((py - boundaries[b]) / tau[b]);
                        z[j] += s;
                        if (needGradient) dz[j] += s * (1 - s) / tau[b];
                    }
                    zlo = std::min(zlo, z[j]); zhi = std::max(zhi, z[j]);
                }
                double sp = 0, sm = 0, zp = 0, zm = 0;
                for (std::size_t j = 0; j < count; ++j) {
                    // Shift BEFORE exponentiation, including at small gamma.
                    plus[j] = std::exp((z[j] - zhi) / gamma);
                    minus[j] = std::exp((zlo - z[j]) / gamma);
                    sp += plus[j]; sm += minus[j];
                    zp += z[j] * plus[j]; zm += z[j] * minus[j];
                }
                const double mup = zp / sp, mum = zm / sm;
                out.smooth += net.weight * (mup - mum);
                if (needGradient)
                    for (std::size_t j = 0; j < count; ++j) {
                        const auto &pin = pins[net.begin + j];
                        if (!movable[pin.unit]) continue;
                        const double gradZ = plus[j] / sp * (1 + (z[j] - mup) / gamma)
                                           - minus[j] / sm * (1 - (z[j] - mum) / gamma);
                        out.gradient[pin.unit] += net.weight * gradZ * dz[j];
                    }
            }
        }
        Result result;
        result.seams.assign(boundaries.size(), 0);
        if (needGradient) result.gradient.assign(units, 0);
        // Fixed reduction order for a given thread count. No floating atomics.
        for (const auto &r : partial) {
            result.discrete += r.discrete; result.discreteOptimized += r.discreteOptimized;
            result.validNets += r.validNets; result.invalidNets += r.invalidNets;
            result.smooth += r.smooth;
            for (std::size_t b = 0; b < boundaries.size(); ++b) result.seams[b] += r.seams[b];
            if (needGradient)
                for (std::size_t i = 0; i < units; ++i) result.gradient[i] += r.gradient[i];
        }
        if (!std::isfinite(result.smooth)) throw std::runtime_error("Non-finite SLL cost");
        for (double g : result.gradient)
            if (!std::isfinite(g)) throw std::runtime_error("Non-finite SLL gradient");
        return result;
    }
};

// For 0.5*y'Ay+b'y, add lambda * [g'delta + 0.5*delta'R*delta].
inline void addLocalModel(const std::vector<double> &y, const std::vector<double> &g,
                          const std::vector<unsigned char> &movable,
                          double lambda, double curvature,
                          std::vector<double> &diagonal, std::vector<double> &linear) {
    if (y.size() != g.size() || y.size() != movable.size() || y.size() != diagonal.size() ||
        y.size() != linear.size() || !std::isfinite(lambda) || lambda < 0 ||
        !std::isfinite(curvature) || curvature <= 0)
        throw std::invalid_argument("Invalid SLL QP model");
    for (std::size_t i = 0; i < y.size(); ++i) {
        if (!movable[i]) continue;
        diagonal[i] += lambda * curvature;
        linear[i] += lambda * (g[i] - curvature * y[i]);
    }
}
} // namespace amf_sll
#endif
