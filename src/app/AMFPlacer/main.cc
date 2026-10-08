/**
 * @file main.cc
 * @author Tingyuan Liang (tliang@connect.ust.hk)
 * @brief AMF-Placer Main file which directly pass the arguments to the AMFPacer workflow
 * @version 0.1
 * @date 2021-05-31
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "AMFPlacer.h"
#include "../../lib/utils/RuntimeProfiler.h"

int main(int argc, const char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--capabilities")
    {
        std::cout << "{\"schema\":\"amf-capabilities-v1\",\"clock_resource_capacity_schema\":1,\"external_floorplan_initialization_schema\":1,\"paper_hierarchical_boundaries_schema\":2,\"global_placement_inspection_schema\":1}\n";
        return 0;
    }
    amf_profile::Session profileSession;
    AMF_PROFILE_FUNCTION("process_control");
    if (argc != 2 && !(argc == 4 && (std::string(argv[2]) == "--inspect-global-placement" || std::string(argv[2]) == "--inspect-initial-placement" || std::string(argv[2]) == "--inspect-packing" || std::string(argv[2]) == "--inspect-input" || std::string(argv[2]) == "--legalize-resources")))
    {
        std::cerr << "Usage: " << argv[0] << " <config JSON file> [--inspect-global-placement <report.json> | --inspect-input <report.json> | --inspect-initial-placement <report.json> | --legalize-resources <directory>]\n";
        return 1;
    }
    try
    {
        AMFPlacer placer(argv[1]);
        if (argc == 4 && std::string(argv[2]) == "--legalize-resources")
            placer.legalizeResources(argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-global-placement")
            placer.run("", "", argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-initial-placement")
            placer.run("", argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-packing")
            placer.run(argv[3]);
        else if (argc == 4)
            placer.inspectInputs(argv[3]);
        else
            placer.run();
    }
    catch (const std::exception &error)
    {
        std::cerr << "AMF_ERROR: " << error.what() << "\n";
        return 2;
    }
    return 0;
}
