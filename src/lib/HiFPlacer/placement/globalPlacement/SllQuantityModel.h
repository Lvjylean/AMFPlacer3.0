#ifndef AMF_SLL_QUANTITY_MODEL_H
#define AMF_SLL_QUANTITY_MODEL_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
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
    bool gradientActive = true; // Frozen per QP; never removes full S/F statistics.
};
struct NetGeometry {double span=0;int crossings=-1;};
struct DiscreteResult {
    std::int64_t discrete=0,discreteOptimized=0;
    std::size_t invalidNets=0,validNets=0;
    std::vector<std::int64_t> seams;
    double seconds=0;
};
struct Result {
    std::int64_t discrete = 0, discreteOptimized = 0;
    std::size_t invalidNets = 0, validNets = 0;
    double smooth = 0, activeSmooth = 0;
    double discreteSeconds=0, smoothGradientSeconds=0;
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

    // One pin scan supplies full integer demand and optional exclusion geometry.
    // This replaces the old discrete scan inside evaluate; it is not an extra pin scan.
    DiscreteResult countDiscrete(const std::vector<double>&y,int threads,
                                 std::vector<NetGeometry>*geometry=nullptr) const {
        const auto started=std::chrono::steady_clock::now();
        if(y.size()!=units || threads<1)throw std::invalid_argument("Invalid SLL counting dimensions");
        for(double v:y)if(!std::isfinite(v))throw std::invalid_argument("Non-finite SLL coordinate");
        const int chunks=int(std::min(size_t(16),std::max(size_t(1),nets.size())));
        threads=std::min(threads,chunks);
        std::vector<DiscreteResult> partial(chunks);
        for(auto&r:partial)r.seams.assign(boundaries.size(),0);
        if(geometry)geometry->assign(nets.size(),{});
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(threads)
#endif
        for(int chunk=0;chunk<chunks;++chunk){auto&out=partial[chunk];
            for(size_t ni=nets.size()*chunk/chunks;ni<nets.size()*(chunk+1)/chunks;++ni){
                const auto&net=nets[ni];if(net.begin==net.end)continue;
                int lo=int(boundaries.size()),hi=0;bool valid=true;
                double ymin=INFINITY,ymax=-INFINITY;
                for(size_t j=net.begin;j<net.end;++j){const auto&pin=pins[j];double py=y[pin.unit]+pin.offsetY;
                    if(!std::isfinite(py)||py<minY||py>maxY){valid=false;break;}
                    const int s=slr(py);lo=std::min(lo,s);hi=std::max(hi,s);
                    if(geometry){ymin=std::min(ymin,py);ymax=std::max(ymax,py);}
                }
                if(!valid){++out.invalidNets;continue;}
                ++out.validNets;out.discrete+=hi-lo;if(net.optimize)out.discreteOptimized+=hi-lo;
                for(int b=lo;b<hi;++b)++out.seams[b];
                if(geometry)(*geometry)[ni]={ymax-ymin,hi-lo};
            }
        }
        DiscreteResult result;result.seams.assign(boundaries.size(),0);
        for(const auto&r:partial){result.discrete+=r.discrete;result.discreteOptimized+=r.discreteOptimized;
            result.validNets+=r.validNets;result.invalidNets+=r.invalidNets;
            for(size_t b=0;b<boundaries.size();++b)result.seams[b]+=r.seams[b];}
        result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return result;
    }

    Result evaluate(const std::vector<double> &y, const std::vector<unsigned char> &movable,
                    int threads, bool needGradient, std::vector<double> *netStrength = nullptr,
                    const DiscreteResult *samePositionCount = nullptr) const {
        if (y.size() != units || movable.size() != units || threads < 1)
            throw std::invalid_argument("Invalid SLL evaluation dimensions/threads");
        for (double v : y)
            if (!std::isfinite(v)) throw std::invalid_argument("Non-finite SLL coordinate");
#ifndef _OPENMP
        threads = 1;
#endif
        // A supplied count must describe exactly these positions. Only the QP
        // preparation path reuses its immediately preceding geometry/count pass.
        const auto counted=samePositionCount?*samePositionCount:countDiscrete(y,threads);
        const auto smoothStarted=std::chrono::steady_clock::now();
        // Fixed logical chunks keep both summation order and gradient storage
        // independent of the number of OpenMP workers (including serial runs).
        const int chunks = int(std::min(std::size_t(16), std::max(std::size_t(1), nets.size())));
        threads = std::min(threads, chunks);
        std::vector<Result> partial(chunks);
        if(netStrength) netStrength->assign(nets.size(), 0);
        for (auto &r : partial) {
            r.seams.assign(boundaries.size(), 0);
            if (needGradient) r.gradient.assign(units, 0);
        }
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(threads)
#endif
        for (int chunk = 0; chunk < chunks; ++chunk) {
            auto &out = partial[chunk];
            std::vector<double> z, dz, plus, minus;
            const std::size_t begin = nets.size() * chunk / chunks;
            const std::size_t end = nets.size() * (chunk + 1) / chunks;
            for (std::size_t ni = begin; ni < end; ++ni) {
                const auto &net = nets[ni];
                const auto count = net.end - net.begin;
                if (!count) continue;
                if (!net.optimize) continue;
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
                        if (needGradient && net.gradientActive) dz[j] += s * (1 - s) / tau[b];
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
                if(net.gradientActive)out.activeSmooth += net.weight * (mup - mum);
                if (needGradient && net.gradientActive)
                    for (std::size_t j = 0; j < count; ++j) {
                        const auto &pin = pins[net.begin + j];
                        if (!movable[pin.unit]) continue;
                        const double gradZ = plus[j] / sp * (1 + (z[j] - mup) / gamma)
                                           - minus[j] / sm * (1 - (z[j] - mum) / gamma);
                        const double contribution = net.weight * gradZ * dz[j];
                        out.gradient[pin.unit] += contribution;
                        if(netStrength) (*netStrength)[ni] = std::max((*netStrength)[ni], std::abs(contribution));
                    }
            }
        }
        Result result;
        result.seams.assign(boundaries.size(), 0);
        if (needGradient) result.gradient.assign(units, 0);
        // Fixed chunk order for every worker count. No floating atomics.
        for (const auto &r : partial) {
            result.discrete += r.discrete; result.discreteOptimized += r.discreteOptimized;
            result.validNets += r.validNets; result.invalidNets += r.invalidNets;
            result.smooth += r.smooth;result.activeSmooth += r.activeSmooth;
            for (std::size_t b = 0; b < boundaries.size(); ++b) result.seams[b] += r.seams[b];
            if (needGradient)
                for (std::size_t i = 0; i < units; ++i) result.gradient[i] += r.gradient[i];
        }
        result.discrete=counted.discrete;result.discreteOptimized=counted.discreteOptimized;
        result.validNets=counted.validNets;result.invalidNets=counted.invalidNets;result.seams=counted.seams;
        result.discreteSeconds=counted.seconds;
        result.smoothGradientSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-smoothStarted).count();
        if (!std::isfinite(result.smooth)) throw std::runtime_error("Non-finite SLL cost");
        for (double g : result.gradient)
            if (!std::isfinite(g)) throw std::runtime_error("Non-finite SLL gradient");
        return result;
    }
};

// Validated U250 tile-Y pitch is one AMF Y unit. This is a resource
// equivalent span, not a measurement of interposer metal length.
constexpr double sllLengthTiles = 60.0;
inline double firstOrderCoefficient(double actualY2XRatio) {
    if (!std::isfinite(actualY2XRatio) || actualY2XRatio <= 0)
        throw std::invalid_argument("SLL requires a finite positive QP y2xRatio");
    const double coefficient = sllLengthTiles * actualY2XRatio;
    if (!std::isfinite(coefficient)) throw std::invalid_argument("SLL length coefficient overflow");
    return coefficient;
}
inline void rejectDeprecatedParameters(const std::map<std::string,std::string>& cfg) {
    for (const auto &key : {"SllQuantityWeight", "SllQuantityCurvature"})
        if (cfg.count(key)) throw std::invalid_argument(std::string(key) +
            " is retired; remove it. SLL uses 60 * actual QP y2xRatio, first order only.");
}
// For 0.5*y'Ay+b'y, first order changes ONLY b, independent of y0.
inline void addFirstOrderModel(const std::vector<double> &g,
                              const std::vector<unsigned char> &movable,
                              double coefficient, std::vector<double> &linear) {
    if (g.size() != movable.size() || g.size() != linear.size() ||
        !std::isfinite(coefficient) || coefficient < 0)
        throw std::invalid_argument("Invalid first-order SLL dimensions/coefficient");
    for (size_t i=0;i<g.size();++i)
        if (movable[i] && (!std::isfinite(g[i]) || !std::isfinite(linear[i]) ||
            !std::isfinite(linear[i]+coefficient*g[i])))
            throw std::invalid_argument("Non-finite first-order SLL linear term");
    if (coefficient == 0) return;
    for (size_t i=0;i<g.size();++i)
        if (movable[i]) linear[i] += coefficient*g[i];
}
// Preserve the previous actual-objective acceptance tolerance. No curvature,
// jitter, damping or new regularization is introduced.
inline bool acceptFirstOrderCandidate(double baseDelta, double smoothDelta,
                                     double coefficient, size_t invalidBefore,
                                     size_t invalidAfter, double maxMove, double maxStep) {
    const double combined=baseDelta+coefficient*smoothDelta;
    return std::isfinite(baseDelta) && std::isfinite(smoothDelta) &&
           std::isfinite(coefficient) && coefficient>=0 &&
           std::isfinite(maxMove) && std::isfinite(maxStep) && maxStep>0 &&
           maxMove>=0 && maxMove<=maxStep && invalidAfter<=invalidBefore &&
           std::isfinite(combined) && combined<=1e-8;
}
} // namespace amf_sll
#endif
