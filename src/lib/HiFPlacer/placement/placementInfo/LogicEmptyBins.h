#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace empty_logic {
struct Rect { int left, right, bottom, top; };

// Geometric identity only. Nominal slots are independently checked before use.
template<class Grid>
std::string gridKey(const Grid &grid, const std::string &resource, float width, float height)
{
    uint64_t hash = 14695981039346656037ULL;
    auto add = [&](float edge) {
        uint32_t bits;
        static_assert(sizeof(bits) == sizeof(edge));
        std::memcpy(&bits, &edge, sizeof(bits));
        hash = (hash ^ bits) * 1099511628211ULL;
    };
    add(grid.front().front()->left());
    for (auto bin : grid.front()) add(bin->right());
    add(grid.front().front()->bottom());
    for (const auto &row : grid) add(row.front()->top());
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << resource << '\t' << width << '\t' << height << '\t' << grid.front().size()
        << '\t' << grid.size() << '\t' << std::hex << hash;
    return out.str();
}

// Prefix counts of physically populated bins for constant-time sub-box queries.
struct Mask {
    int stride;
    std::vector<int> prefix;
    bool empty(int l, int r, int b, int t) const {
        return prefix[(t+1)*stride+r+1] - prefix[b*stride+r+1] ==
               prefix[(t+1)*stride+l] - prefix[b*stride+l];
    }
};

class DeviceTable {
    std::map<std::string, std::vector<Rect>> profiles;
    std::map<std::string, Mask> verified;
    std::mutex mutex;
public:
    void read(const std::string &path, const std::string &part) {
        std::ifstream input(path);
        std::string line;
        if (!std::getline(input, line) || line != "AMF_EMPTY_LOGIC_V1\t" + part)
            throw std::runtime_error("Cannot read matching empty logic bin table for " + part + ": " + path);
        std::vector<Rect> *current = nullptr;
        while (std::getline(input, line)) {
            if (line.rfind("grid\t", 0) == 0) {
                auto added = profiles.emplace(line.substr(5), std::vector<Rect>{});
                if (!added.second) throw std::runtime_error("Duplicate empty logic grid in " + path);
                current = &added.first->second;
            } else {
                Rect rect;
                std::string extra;
                std::istringstream fields(line);
                if (!current || !(fields >> rect.left >> rect.right >> rect.bottom >> rect.top) ||
                    fields >> extra || rect.left < 0 || rect.bottom < 0 ||
                    rect.right < rect.left || rect.top < rect.bottom)
                    throw std::runtime_error("Invalid empty logic rectangle in " + path + ": " + line);
                current->push_back(rect);
            }
        }
        if (input.bad() || profiles.empty()) throw std::runtime_error("Incomplete empty logic table: " + path);
    }

    // No PU coordinates or demands participate. Each matching grid is checked
    // once per construction of the native device bins.
    template<class Grid>
    const Mask *match(const Grid &grid, const std::string &resource, float width, float height) {
        if (profiles.empty()) return nullptr;
        const auto key = gridKey(grid, resource, width, height);
        const auto found = profiles.find(key);
        if (found == profiles.end()) return nullptr;
        std::lock_guard<std::mutex> lock(mutex);
        const auto cached = verified.find(key);
        if (cached != verified.end()) return &cached->second;
        const int nx = grid.front().size(), ny = grid.size();
        std::vector<unsigned char> zero(size_t(nx)*ny);
        for (const auto &r : found->second) {
            if (r.right >= nx || r.top >= ny) throw std::runtime_error("Empty rectangle outside grid " + key);
            for (int y=r.bottom; y<=r.top; ++y)
                for (int x=r.left; x<=r.right; ++x) zero[y*nx+x] = 1;
        }
        Mask mask{nx+1, std::vector<int>(size_t(nx+1)*(ny+1))};
        for (int y=0; y<ny; ++y) {
            int row = 0;
            for (int x=0; x<nx; ++x) {
                if (bool(zero[y*nx+x]) != (grid[y][x]->getNominalCapacity() == 0))
                    throw std::runtime_error("Empty logic table disagrees with device at " + key +
                                             " x=" + std::to_string(x) + " y=" + std::to_string(y));
                row += !zero[y*nx+x];
                mask.prefix[(y+1)*(nx+1)+x+1] = mask.prefix[y*(nx+1)+x+1] + row;
            }
        }
        return &verified.emplace(key, std::move(mask)).first->second;
    }

    void resetGrid() { verified.clear(); }
};
}
