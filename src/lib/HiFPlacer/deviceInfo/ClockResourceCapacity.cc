#include "ClockResourceCapacity.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <vector>

namespace
{
std::vector<std::string> splitTabs(const std::string &line)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;)
    {
        auto end = line.find('\t', start);
        result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return result;
        start = end + 1;
    }
}

int capacityInteger(const std::string &text)
{
    // Bound before converting, so malformed/overflowing input never wraps.
    if (text.empty() || text.size() > 5 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::runtime_error("Expected an integer in [0,65535]: " + text);
    int value = std::stoi(text);
    if (value > 65535) throw std::runtime_error("Capacity integer exceeds 65535: " + text);
    return value;
}
}

ClockResourceCapacityTable::ClockResourceCapacityTable(
    const std::string &file, const std::string &expectedPart,
    const std::map<std::string, int> &expectedRegions) : path(file)
{
    if (expectedPart.empty())
        throw std::runtime_error("clock resource capacity part is required with a capacity file");
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open clock resource capacity file: " + file);
    std::string line;
    unsigned int lineNumber = 0;
    try
    {
        if (!std::getline(input, line) || line != "AMF_CLOCK_CAPACITY\t1")
            throw std::runtime_error("Expected AMF_CLOCK_CAPACITY version 1 header");
        lineNumber = 1;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (std::any_of(line.begin(), line.end(), [](unsigned char c) {
                    return (c < 32 && c != '\t') || c == 127;
                }))
                throw std::runtime_error("Unexpected control character");
            const auto fields = splitTabs(line);
            if (fields[0] == "META")
            {
                if (fields.size() != 3 || fields[1].empty() || fields[2].empty() ||
                    !metadata.emplace(fields[1], fields[2]).second)
                    throw std::runtime_error("Malformed or duplicate META row");
            }
            else if (fields[0] == "CR")
            {
                if (fields.size() != 9 ||
                    !std::regex_match(fields[1], std::regex("X(0|[1-9][0-9]*)Y(0|[1-9][0-9]*)")))
                    throw std::runtime_error("Malformed CR row");
                ClockRegionCapacity capacity;
                capacity.slr = capacityInteger(fields[2]);
                capacity.hroute = capacityInteger(fields[3]);
                capacity.hdistr = capacityInteger(fields[4]);
                capacity.vroute = capacityInteger(fields[5]);
                capacity.vdistr = capacityInteger(fields[6]);
                capacity.halfColumnLimit = capacityInteger(fields[7]);
                capacity.bboxClockLimit = capacityInteger(fields[8]);
                if (!regions.emplace(fields[1], capacity).second)
                    throw std::runtime_error("Duplicate clock region: " + fields[1]);
            }
            else throw std::runtime_error("Unknown row type: " + fields[0]);
        }
        if (!input.eof()) throw std::runtime_error("Failed reading capacity file");
        for (const auto &key : {"part", "architecture", "rules_version", "resource_state", "vivado_version",
                                "device_sha256"})
            if (!metadata.count(key)) throw std::runtime_error(std::string("Missing metadata: ") + key);
        if (metadata.at("part") != expectedPart)
            throw std::runtime_error("Clock capacity part/config mismatch: " + metadata.at("part") + " != " + expectedPart);
        if (metadata.at("resource_state") != "nominal")
            throw std::runtime_error("Only nominal clock resource capacities are supported");
        if (metadata.at("rules_version") != "ultrascale-clock-v1")
            throw std::runtime_error("Unsupported clock architecture rules version: " + metadata.at("rules_version"));
        if (metadata.at("architecture") != "virtexuplus")
            throw std::runtime_error("Unsupported clock capacity architecture: " + metadata.at("architecture"));
        if (!std::regex_match(metadata.at("device_sha256"), std::regex("[0-9a-f]{64}")))
            throw std::runtime_error("device_sha256 must contain 64 lowercase hexadecimal digits");
        if (expectedRegions.empty() || regions.size() != expectedRegions.size())
            throw std::runtime_error("Clock capacity regions do not cover the loaded device exactly");
        for (const auto &region : regions)
        {
            auto expected = expectedRegions.find(region.first);
            if (expected == expectedRegions.end())
                throw std::runtime_error("Unknown clock region: " + region.first);
            if (region.second.slr != expected->second)
                throw std::runtime_error("Clock region SLR mismatch: " + region.first);
        }
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error("Clock resource capacity " + file + ":" + std::to_string(lineNumber) + ": " + error.what());
    }
}
