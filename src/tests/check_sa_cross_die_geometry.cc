#include "DeviceInfo.h"
#include "simpleJSON.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Expected real U250 config");
        auto cfg = parseJSONFile(argv[1]);
        DeviceInfo device(cfg, cfg.at("device"));
        if (device.getClockRegionNumY() != 16) throw std::runtime_error("Unexpected U250 CR row count");
        for (int a = 0; a < 16; ++a) for (int b = 0; b < 16; ++b)
            if (device.getSLRBoundaryCount(a,b) != std::abs(a/4-b/4))
                throw std::runtime_error("Real device seam mapping differs from expected U250 topology");
        // Pin these representative points to the actual export, not an SA-invented boundary.
        for (const auto p : {std::make_pair(239.f,0),std::make_pair(240.f,1),
                             std::make_pair(479.f,1),std::make_pair(480.f,2),
                             std::make_pair(719.f,2),std::make_pair(720.f,3)})
        {
            int x,y;device.getClockRegionByLocation(70,p.first,x,y);
            if (device.getSLRBoundaryCount(0,y) != p.second)
                throw std::runtime_error("Fixed endpoint mapping around physical seam failed");
        }
        std::cout << "256 real-device row pairs and 6 seam-adjacent terminal coordinates passed\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
