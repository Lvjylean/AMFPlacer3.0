#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>

// A site assignment, never a replacement PlacementUnit. One SRL primitive
// consumes one of the four experimental SRL slots and one distinct LUT BEL.
namespace srl_site_sharing {
struct Unit {
    int id = -1, srlCount = 0;
    std::string controls;
    std::vector<std::string> bels;
    std::set<size_t> externalQ31;
};
struct Layout {
    int srlCount = 0;
    std::string controls;
    std::set<std::string> occupied;
    std::map<int, std::vector<std::string>> assignments;
};
inline std::string shiftedBEL(const std::string &bel, int shift) {
    if (bel.size() == 5 && bel.substr(1) == "6LUT") {
        char letter = static_cast<char>(bel[0] + shift);
        return letter >= 'A' && letter <= 'H' ? std::string(1, letter) + "6LUT" : "";
    }
    if (bel.size() == 8 && bel.substr(0, 6) == "F7MUX_" && shift % 2 == 0) {
        char first = static_cast<char>(bel[6] + shift);
        char second = static_cast<char>(bel[7] + shift);
        if (first >= 'A' && second <= 'H' && (first - 'A') % 2 == 0 && second == first + 1)
            return "F7MUX_" + std::string(1, first) + std::string(1, second);
    }
    return "";
}
inline bool append(const Unit &unit, const Layout &current, Layout &result, int limit = 4) {
    if (unit.srlCount <= 0 || unit.srlCount + current.srlCount > limit ||
        current.assignments.count(unit.id) ||
        (!current.assignments.empty() && unit.controls != current.controls)) return false;
    // Preserve the original BELs where possible. Shift the whole original macro
    // together, so SRL cascades and F7 input-pair relationships remain intact.
    std::vector<int> shifts{0};
    for (int n = 1; n < 8; ++n) { shifts.push_back(-n); shifts.push_back(n); }
    for (int shift : shifts) {
        auto occupied = current.occupied;
        std::vector<std::string> bels;
        bool valid = true;
        for (size_t i = 0; i < unit.bels.size(); ++i) {
            auto bel = shiftedBEL(unit.bels[i], shift);
            if (bel.empty() || !occupied.insert(bel).second ||
                (unit.externalQ31.count(i) && bel != "A6LUT")) { valid = false; break; }
            bels.push_back(bel);
        }
        if (!valid) continue;
        result = current;
        result.controls = unit.controls;
        result.srlCount += unit.srlCount;
        result.occupied = std::move(occupied);
        result.assignments[unit.id] = std::move(bels);
        return true;
    }
    return false;
}
}
