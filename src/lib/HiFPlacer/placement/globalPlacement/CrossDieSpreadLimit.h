#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// One budget per GP spreading round, shared by all resource passes and inner
// iterations. Distances use the original (not seam-fragment) bin dimensions.
class CrossDieSpreadLimit
{
  public:
    struct Point { float x = 0, y = 0; };

    template<class Units>
    void reset(double bins, double width, double height, const Units &units)
    {
        if (!std::isfinite(bins) || bins < 0 || !std::isfinite(width) || width <= 0 ||
            !std::isfinite(height) || height <= 0)
            throw std::invalid_argument("Invalid cross-die spreading radius or bin dimensions");
        radius = bins; binWidth = width; binHeight = height;
        origins.clear(); activated.clear(); limitedWrites = rejectedWrites = activeUnits = 0;
        if (!enabled()) return;
        int maxId = -1;
        for (auto pu : units) maxId = std::max(maxId, int(pu->getId()));
        origins.resize(maxId + 1, {std::numeric_limits<float>::quiet_NaN(), 0});
        activated.resize(maxId + 1, false);
        for (auto pu : units) {
            if (size_t(pu->getId()) >= origins.size() || !std::isfinite(pu->X()) || !std::isfinite(pu->Y()))
                throw std::invalid_argument("Invalid PU in spreading round snapshot");
            origins.at(pu->getId()) = {pu->X(), pu->Y()};
        }
    }

    bool enabled() const { return radius > 0; }
    double maxBins() const { return radius; }
    double width() const { return binWidth; }
    double height() const { return binHeight; }
    bool active(int id) const { return enabled() && activated.at(id); }
    void activate(int id) {
        origin(id); // Validate snapshot identity before workers are started.
        if (!activated.at(id)) { activated[id] = true; ++activeUnits; }
    }
    Point origin(int id) const {
        auto p = origins.at(id);
        if (!std::isfinite(p.x)) throw std::logic_error("PU missing from spreading round snapshot");
        return p;
    }
    double distanceSquared(int id, Point p) const {
        const auto a = origin(id);
        const double dx = (double(p.x) - a.x) / binWidth;
        const double dy = (double(p.y) - a.y) / binHeight;
        return dx * dx + dy * dy;
    }
    bool contains(int id, Point p) const {
        // Float anchor writeback can round outwards by a few ulps.
        const double tolerance = 1e-4;
        return std::isfinite(p.x) && std::isfinite(p.y) &&
               distanceSquared(id, p) <= (radius + tolerance) * (radius + tolerance);
    }
    Point project(int id, Point p) const {
        const double d2 = distanceSquared(id, p);
        if (!std::isfinite(d2)) throw std::invalid_argument("Non-finite spreading target");
        if (d2 <= radius * radius) return p;
        const auto a = origin(id);
        const double scale = radius / std::sqrt(d2);
        return {float(a.x + (double(p.x) - a.x) * scale),
                float(a.y + (double(p.y) - a.y) * scale)};
    }

    // Updated only on the controlling thread, never by placement workers.
    size_t limitedWrites = 0, rejectedWrites = 0, activeUnits = 0;

  private:
    double radius = 0, binWidth = 1, binHeight = 1;
    std::vector<Point> origins;
    std::vector<unsigned char> activated;
};
