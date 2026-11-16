#pragma once
#include <array>
#include <map>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>
#include <utility>

// A site assignment, never a replacement PlacementUnit. One SRL primitive
// can share a physical LUT row only in the U250 eight-row mode.
namespace srl_site_sharing {
inline unsigned siteLimit(const std::map<std::string, std::string> &cfg, const std::string &device) {
    const auto it = cfg.find("SRLMaxPerSLICEM");
    if (it == cfg.end() || it->second == "1") return 1;
    if (it->second == "4") return 4;
    if (it->second == "8" && device == "U250") return 8;
    throw std::runtime_error("SRLMaxPerSLICEM must be 1, 4, or (on U250) 8");
}
struct Unit {
    int id = -1, srlCount = 0;
    std::string controls;
    std::vector<std::string> bels;
    std::set<size_t> externalQ31;
    // A key exists only for an SRL16E with all six shared input nets and known polarity.
    std::map<size_t, std::string> pairKeys;
};
struct Layout {
    int srlCount = 0;
    std::string controls;
    std::set<std::string> occupied;
    struct Primary {
        std::string key;
        int movableOwner = -1; // A standalone SRL16E may swap to O5.
    };
    std::array<Primary,8> primary;
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
    if (unit.srlCount <= 0 || (limit != 8 && unit.srlCount + current.srlCount > limit) ||
        current.assignments.count(unit.id) ||
        (!current.assignments.empty() && unit.controls != current.controls)) return false;

    // Try whole-macro offsets. Prefer a compatible half-row, then the
    // original offset. A member feeding a dedicated path keeps its O6 BEL.
    const int shifts[] = {0,-1,1,-2,2,-3,3,-4,4,-5,5,-6,6,-7,7};
    int bestPairs = -1;
    for (int shift : shifts) {
        std::vector<std::string> bels;
        std::vector<unsigned> swapRows;
        std::set<std::string> incoming;
        bool valid = true;
        int pairs = 0;
        for (size_t i = 0; i < unit.bels.size(); ++i) {
            auto bel = shiftedBEL(unit.bels[i], shift);
            if (bel.empty() || !incoming.insert(bel).second ||
                (unit.externalQ31.count(i) && bel != "A6LUT")) { valid = false; break; }
            if (current.occupied.count(bel)) {
                auto key = unit.pairKeys.find(i);
                if (limit != 8 || key == unit.pairKeys.end() || bel.size() != 5 ||
                    key->second.empty()) { valid = false; break; }
                unsigned row = bel[0] - 'A';
                std::string half = std::string(1,bel[0]) + "5LUT";
                const auto &primary = current.primary[row];
                if (current.occupied.count(half) || primary.key != key->second) { valid = false; break; }
                if (unit.bels.size() == 1) bel = half;
                else if (primary.movableOwner >= 0) swapRows.push_back(row);
                else { valid = false; break; }
                ++pairs;
            }
            bels.push_back(bel);
        }
        if (!valid || pairs <= bestPairs) continue;
        // The F7 selector and the second SRL data input would contend
        // for the first row's bypass. Test both old and incoming footprints.
        for (unsigned row = 0; row < 8 && valid; row += 2) {
            std::string mux = "F7MUX_" + std::string(1,'A'+row) + char('B'+row);
            if (!incoming.count(mux) && !current.occupied.count(mux)) continue;
            std::string base(1, 'A'+row);
            if (current.occupied.count(base+"5LUT") ||
                (incoming.count(base+"6LUT") && current.occupied.count(base+"6LUT"))) valid = false;
        }
        if (!valid) continue;
        result = current;
        result.controls = unit.controls;
        result.srlCount += unit.srlCount;
        for (unsigned row : swapRows) {
            std::string half = std::string(1, 'A'+row) + "5LUT";
            result.assignments.at(current.primary[row].movableOwner).at(0) = half;
            result.occupied.insert(half);
        }
        for (size_t i = 0; i < bels.size(); ++i) {
            const auto &bel = bels[i];
            result.occupied.insert(bel);
            auto key = unit.pairKeys.find(i);
            if (limit == 8 && key != unit.pairKeys.end() && bel.size()==5 && bel[1]=='6') {
                auto &primary = result.primary[bel[0]-'A'];
                primary.key = key->second;
                primary.movableOwner = unit.bels.size()==1 ? unit.id : -1;
            }
        }
        result.assignments[unit.id] = std::move(bels);
        bestPairs = pairs;
        if (limit != 8 || pairs == static_cast<int>(unit.pairKeys.size())) break;
    }
    return bestPairs >= 0;
}
}
