#ifndef AMF_GRID_UPDATE_POLICY_H
#define AMF_GRID_UPDATE_POLICY_H

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

// Experimental bookkeeping backends. None changes placement scheduling.
namespace grid_update {
enum class Mode { legacy, batched, parallel, incremental };
inline Mode mode()
{
    static const Mode value = [] {
        const char *p = std::getenv("AMF_GRID_UPDATE_MODE");
        const std::string s = p ? p : "legacy";
        if (s == "legacy") return Mode::legacy;
        if (s == "batched") return Mode::batched;
        if (s == "parallel") return Mode::parallel;
        if (s == "incremental") return Mode::incremental;
        throw std::invalid_argument("Unknown AMF_GRID_UPDATE_MODE: " + s);
    }();
    return value;
}
inline bool verify()
{
    static const bool v = [] { const char *p = std::getenv("AMF_GRID_UPDATE_VERIFY");
        return p && std::string(p) == "1"; }();
    return v;
}
inline bool parallel() { return mode() == Mode::parallel || mode() == Mode::incremental; }

// Counting partition: preserve the ORIGINAL PU/virtual-cell visitation order
// inside each spatial bin. Different BEL types in a bin must stay together.
template<class Records>
void stableBuckets(const Records &records, size_t nBins,
                   std::vector<size_t> &offsets, std::vector<size_t> &order)
{
    offsets.assign(nBins + 1, 0);
    for (const auto &r : records) {
        if (r.bin >= nBins) throw std::out_of_range("grid record bin");
        ++offsets[r.bin + 1];
    }
    for (size_t b = 1; b <= nBins; ++b) offsets[b] += offsets[b - 1];
    auto next = offsets;
    order.resize(records.size());
    for (size_t i = 0; i < records.size(); ++i) order[next[records[i].bin]++] = i;
}

// Same first-fitting type and fallback as the legacy updater. Int conversion
// of occupation belongs to the existing bin API and is deliberately retained.
template<class Types, class Fits>
int chooseType(const Types &types, Fits fits)
{
    if (types.empty()) throw std::invalid_argument("cell without BEL types");
    for (int t : types) if (fits(t)) return t;
    return types.front();
}

template<class Set, class Sequence>
bool sameMembers(const Set &current, const Sequence &expected)
{
    if (current.size() != expected.size()) return false;
    for (auto member : expected) if (current.find(member) == current.end()) return false;
    return true;
}
}
#endif
