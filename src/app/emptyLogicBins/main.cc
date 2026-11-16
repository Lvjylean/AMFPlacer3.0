#include "DieFirstSpreading.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

using Span = std::pair<int, int>;

static void writeGrid(std::ostream &out, const die_spread::Grid &grid,
                      const std::string &resource, float width, float height)
{
    out << "grid\t" << empty_logic::gridKey(grid, resource, width, height) << '\n';
    std::vector<Span> previous;
    int bottom = 0;
    for (int y = 0; y <= int(grid.size()); ++y) {
        std::vector<Span> spans;
        if (y < int(grid.size())) {
            for (int x = 0; x < int(grid[y].size()); ++x) {
                if (grid[y][x]->getNominalCapacity() != 0) continue;
                if (!spans.empty() && spans.back().second == x-1) spans.back().second = x;
                else spans.emplace_back(x,x);
            }
        }
        if (spans == previous) continue;
        for (const auto &span : previous)
            out << span.first << '\t' << span.second << '\t' << bottom << '\t' << y-1 << '\n';
        previous = std::move(spans);
        bottom = y;
    }
}

int main(int argc, char **argv) {
    try {
        if (argc < 5 || (argc-3)%2)
            throw std::invalid_argument("exportLogicEmptyBins CONFIG NEW_OUTPUT WIDTH HEIGHT [WIDTH HEIGHT ...]");
        auto cfg = parseJSONFile(argv[1]);
        cfg.erase("logic empty bins file");
        // A historical configuration may point to completed reports. Only
        // the device and bin constructors are needed to inventory resources.
        cfg["SllPlanningEnable"] = "false";
        cfg.erase("clock geometry report file");
        cfg.erase("external floorplan report file");
        const auto incremental = cfg.find("IncrementalCompilation");
        if (incremental != cfg.end() && incremental->second == "true")
            throw std::invalid_argument("Use a full-device configuration without static reservations");
        const auto deviceName = cfg.count("device") ? cfg.at("device") : "VCU108";
        DeviceInfo device(cfg, deviceName);
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        std::vector<float> seams;
        if (auto model = device.getPhysicalBoundaryModel())
            for (const auto &b : model->getBoundaries())
                if (b.active && b.kind == "SLR" && b.orientation == 'Y') seams.push_back(b.coordinate);
        const auto part = cfg.find("physical device part");
        std::ostringstream content;
        content << "AMF_EMPTY_LOGIC_V1\t" << (part == cfg.end() ? deviceName : part->second) << '\n';
        std::set<std::string> keys;
        for (int i=3; i<argc; i+=2) {
            const float w=std::stof(argv[i]), h=std::stof(argv[i+1]);
            if (!(w>0 && h>0 && std::isfinite(w) && std::isfinite(h)))
                throw std::invalid_argument("Bin dimensions must be finite and positive");
            placement.createGridBins(w,h);
            for (const std::string resource : {"SLICEL_LUT","SLICEL_FF"}) {
                auto &raw = placement.getBinGrid(placement.getSharedBELTypeId(resource));
                auto emit = [&](const die_spread::Grid &grid) {
                    if (keys.insert(empty_logic::gridKey(grid,resource,w,h)).second)
                        writeGrid(content,grid,resource,w,h);
                };
                emit(raw);
                if (!seams.empty()) {
                    // Fresh device bins contain no placed members.
                    die_spread::GridView view(raw,placement.getCompatiblePlacementTable(),seams,0,{},{});
                    emit(view.rows);
                }
            }
        }
        const int fd = ::open(argv[2], O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) throw std::runtime_error("Could not create a new empty logic bin table");
        const std::string bytes = content.str();
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto n = ::write(fd, bytes.data()+offset, bytes.size()-offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            offset += size_t(n);
        }
        if (::close(fd) != 0 || offset != bytes.size()) {
            std::filesystem::remove(argv[2]);
            throw std::runtime_error("Could not save empty logic bin table");
        }
        return 0;
    } catch (const std::exception &err) {
        std::cerr << err.what() << '\n';
        return 1;
    }
}
