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

int main(int argc, const char **argv)
{
    if (argc != 2 && !(argc == 4 && (std::string(argv[2]) == "--inspect-input" || std::string(argv[2]) == "--legalize-resources")))
    {
        std::cerr << "Usage: " << argv[0] << " <config JSON file> [--inspect-input <report.json> | --legalize-resources <directory>]\n";
        return 1;
    }
    try
    {
        AMFPlacer placer(argv[1]);
        if (argc == 4 && std::string(argv[2]) == "--legalize-resources")
            placer.legalizeResources(argv[3]);
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
