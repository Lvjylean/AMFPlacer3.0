#include "DeviceInfo.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const std::string &message)
{
    if (!value) throw std::runtime_error(message);
}
}

// The fixture must have X0Y0 and X1Y0, both in SLR0, with SLICE sites in each
// half. This exercises production DeviceInfo construction without a netlist.
int main(int argc, char **argv)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("amf-clock-device-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tsv");
    try
    {
        require(argc == 2, "Usage: checkClockCapacityDevice two_clock_region_device.zip");
        const std::map<std::string, std::string> baseConfig{{"vivado extracted device information file", argv[1]}};
        {
            // DeviceInfo's existing optional-key reads mutate its configuration.
            // Each independent construction must receive a fresh configuration.
            auto config = baseConfig;
            DeviceInfo legacy(config, "clock-capacity-fixture");
            require(!legacy.hasClockResourceCapacityTable(), "Legacy mode is explicit");
            for (auto column : legacy.getClockColumns())
                require(column->getClockNumLimit() == 12, "Legacy actual half-column remains 12");
            std::ostringstream report;
            legacy.writeClockResourceCapacityJson(report);
            require(report.str().find("\"mode\":\"legacy_24_12\"") != std::string::npos &&
                    report.str().find("\"hroute\":null") != std::string::npos, "Legacy inspection does not invent tracks");
        }
        {
            std::ofstream table(path);
            table << "AMF_CLOCK_CAPACITY\t1\nMETA\tpart\txcu250-figd2104-2L-e\n"
                  << "META\tarchitecture\tvirtexuplus\nMETA\trules_version\tultrascale-clock-v1\n"
                  << "META\tresource_state\tnominal\nMETA\tvivado_version\t2024.2\nMETA\tdevice_sha256\t"
                  << std::string(64, 'a') << "\nCR\tX0Y0\t0\t24\t24\t24\t24\t12\t24\n"
                  << "CR\tX1Y0\t0\t20\t21\t22\t23\t7\t19\n";
        }
        auto config = baseConfig;
        config["clock resource capacity file"] = path.string();
        config["clock resource capacity part"] = "xcu250-figd2104-2L-e";
        DeviceInfo device(config, "clock-capacity-fixture");
        require(device.hasClockResourceCapacityTable(), "Device table mode");
        require(device.getClockRegionCapacity(1, 0).bboxClockLimit == 19, "Region limit loaded");
        require(device.getClockRegionCapacity(1, 0).vdistr == 23, "All nominal tracks preserved");
        bool seen[2] = {false, false};
        for (auto site : device.getSites())
        {
            if (site->getName().find("SLICE") != 0) continue;
            const int x = site->getClockRegionX();
            require(x == 0 || x == 1, "Unexpected fixture region");
            require(site->getClockHalfColumn() != nullptr, "Production site mapping present");
            require(site->getClockHalfColumn()->getClockNumLimit() == (x == 0 ? 12U : 7U),
                    "Packing's actual half-column receives its own region capacity");
            seen[x] = true;
        }
        require(seen[0] && seen[1], "Fixture covers both differing capacities");
        for (const auto &row : device.getClockRegions().at(0).at(1)->getClockColumns())
            for (auto column : row)
                require(column->getClockNumLimit() == 7, "Empty rough columns also receive region capacity");
        std::ostringstream report;
        device.writeClockResourceCapacityJson(report);
        const std::string expectedRows =
            "\"clock_regions\":[{\"name\":\"X0Y0\",\"slr\":0,\"hroute\":24,\"hdistr\":24,"
            "\"vroute\":24,\"vdistr\":24,\"half_column_limit\":12,\"bbox_clock_limit\":24},"
            "{\"name\":\"X1Y0\",\"slr\":0,\"hroute\":20,\"hdistr\":21,\"vroute\":22,"
            "\"vdistr\":23,\"half_column_limit\":7,\"bbox_clock_limit\":19}]";
        require(report.str().find("\"mode\":\"device_table\"") != std::string::npos &&
                report.str().find("\"part\":\"xcu250-figd2104-2L-e\"") != std::string::npos &&
                report.str().find("\"archive_hash_validation_in_cpp\":false") != std::string::npos &&
                report.str().find(expectedRows) != std::string::npos,
                "Inspection preserves every capacity in its corresponding region and reports provenance limits");
        std::filesystem::remove(path);
        std::cout << "Clock capacity DeviceInfo checks passed: explicit legacy, per-region data, packing half-column mapping, inspection\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 2;
    }
}
