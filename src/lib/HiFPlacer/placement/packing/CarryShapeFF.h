#ifndef AMF_CARRY_SHAPE_FF_H
#define AMF_CARRY_SHAPE_FF_H

#include <array>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace CarryShapeFF {
// Entries use physical row indices for same-site primary CFF/EFF/HFF members in a DCP
// CARRY_CHAIN template. Initial packing separately verifies the actual O->D net.
using Rows = std::map<std::string, std::array<std::string, 8>>;

inline Rows read(const std::map<std::string,std::string> &config)
{
    Rows rows;
    auto file = config.find("CARRY shape FF file");
    if (file == config.end()) return rows;
    std::ifstream input(file->second);
    std::string line;
    if (!std::getline(input, line) || (line != "AMF_CARRY_SHAPE_FF 1" && line != "AMF_CARRY_SHAPE_FF 2"))
        throw std::runtime_error("Invalid CARRY shape FF file: " + file->second);
    const bool allowCFF = line == "AMF_CARRY_SHAPE_FF 2";
    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string carry, ff, bel, extra;
        if (!(fields >> carry >> bel >> ff) || fields >> extra || (bel != "EFF" && bel != "HFF" && !(allowCFF && bel == "CFF")))
            throw std::runtime_error("Invalid CARRY shape FF entry: " + line);
        auto &slot = rows[carry][bel[0] - 'A'];
        if (!slot.empty()) throw std::runtime_error("Duplicate CARRY shape FF slot: " + line);
        slot = ff;
    }
    if (!input.eof()) throw std::runtime_error("Error reading CARRY shape FF file: " + file->second);
    return rows;
}
}
#endif
