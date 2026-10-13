#include "ClockResourceCapacity.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const std::string &message)
{
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()> &operation, const std::string &message)
{
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "Expected rejection: " + message);
}
std::string replace(std::string text, const std::string &from, const std::string &to)
{
    auto offset = text.find(from);
    require(offset != std::string::npos, "Bad test replacement");
    return text.replace(offset, from.size(), to);
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("amf-clock-capacity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tsv");
    try
    {
        const std::string part = "xcu250-figd2104-2L-e";
        const std::map<std::string, int> geometry{{"X0Y0", 0}, {"X1Y0", 0}};
        const std::string valid =
            "AMF_CLOCK_CAPACITY\t1\nMETA\tpart\t" + part +
            "\nMETA\tarchitecture\tvirtexuplus\nMETA\trules_version\tultrascale-clock-v1"
            "\nMETA\tresource_state\tnominal\nMETA\tvivado_version\t2024.2\nMETA\tdevice_sha256\t" +
            std::string(64, 'a') + "\nMETA\trules_sha256\t" + std::string(64, 'b') +
            "\nCR\tX0Y0\t0\t24\t24\t24\t24\t12\t24\nCR\tX1Y0\t0\t20\t21\t22\t23\t7\t19\n";
        auto parse = [&](const std::string &text, const std::string &expectedPart = "xcu250-figd2104-2L-e") {
            { std::ofstream file(path); file << text; }
            return ClockResourceCapacityTable(path.string(), expectedPart, geometry);
        };
        auto table = parse(valid);
        require(table.getRegions().size() == 2, "Two regions loaded");
        const auto &first = table.getRegions().at("X0Y0"), &second = table.getRegions().at("X1Y0");
        require(first.bboxClockLimit == 24 && first.halfColumnLimit == 12, "First region capacities");
        require(second.hroute == 20 && second.hdistr == 21 && second.vroute == 22 && second.vdistr == 23 &&
                second.halfColumnLimit == 7 && second.bboxClockLimit == 19, "Distinct per-region capacities");
        require(table.getMetadata().count("rules_sha256") == 1, "Extra provenance metadata preserved");
        auto zero = parse(replace(valid, "20\t21\t22\t23\t7\t19", "0\t0\t0\t0\t0\t0"));
        require(zero.getRegions().at("X1Y0").halfColumnLimit == 0, "Zero capacity is a closed resource");
        rejects([&] { parse(valid, "xcvu095-ffva2104-2-e"); }, "part mismatch");
        rejects([&] { parse(valid, ""); }, "missing expected part");
        rejects([&] { parse(replace(valid, "AMF_CLOCK_CAPACITY\t1", "AMF_CLOCK_CAPACITY\t2")); }, "schema version");
        rejects([&] { parse(valid + "META\tpart\t" + part + "\n"); }, "duplicate metadata");
        rejects([&] { parse(valid + "CR\tX0Y0\t0\t24\t24\t24\t24\t12\t24\n"); }, "duplicate region");
        rejects([&] { parse(replace(valid, "CR\tX1Y0", "CR\tX2Y0")); }, "unknown region");
        rejects([&] { parse(replace(valid, "CR\tX1Y0\t0\t20", "CR\tX1Y0\t1\t20")); }, "SLR mismatch");
        rejects([&] { parse(valid.substr(0, valid.find("CR\tX1Y0"))); }, "missing region");
        rejects([&] { parse(replace(valid, "nominal", "remaining")); }, "non-nominal capacity");
        rejects([&] { parse(replace(valid, "virtexuplus", "spartanup")); }, "unsupported architecture");
        rejects([&] { parse(replace(valid, "ultrascale-clock-v1", "unverified-v1")); }, "unsupported rules");
        rejects([&] { parse(replace(valid, std::string(64, 'a'), "bad-hash")); }, "malformed hash");
        rejects([&] { parse(replace(valid, "20\t21", "-1\t21")); }, "negative capacity");
        rejects([&] { parse(replace(valid, "20\t21", "65536\t21")); }, "out of range");
        rejects([&] { parse(replace(valid, "20\t21", "999999999999999999999999\t21")); }, "integer overflow");
        rejects([&] { parse(replace(valid, "20\t21", "20foo\t21")); }, "trailing integer text");
        rejects([&] { parse(replace(valid, "20\t21", "20\t\t21")); }, "extra field");
        rejects([&] { parse(valid + "BOGUS\tvalue\n"); }, "unknown row");
        ClockRegionCapacity legacy;
        require(legacy.bboxClockLimit == 24 && legacy.halfColumnLimit == 12 && legacy.hroute == -1,
                "Legacy preserves 24/12 without inventing track capacity");
        std::filesystem::remove(path);
        std::cout << "Clock capacity parser checks passed: per-region/zero values, provenance, strict input rejection, legacy defaults\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 2;
    }
}
