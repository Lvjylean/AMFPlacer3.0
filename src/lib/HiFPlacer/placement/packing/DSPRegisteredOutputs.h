#pragma once
#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// This deliberately narrow contract certifies DSP48E2s whose used outputs are
// all P bits and whose DCP PREG is 1. Mixed registered/bypass outputs require a
// pin-level timing model and must not be collapsed into one register node.
inline std::set<std::string> readRegisteredDSPOutputs(
    std::istream &input, const std::map<std::string, std::vector<std::string>> &usedOutputs)
{
    std::string line;
    if (!std::getline(input, line) || line != "AMF_DSP_REGISTERED_OUTPUTS 1")
        throw std::runtime_error("Invalid DSP registered-output metadata header");
    std::set<std::string> names;
    while (std::getline(input, line))
    {
        std::istringstream row(line);
        std::string name, attribute, extra;
        if (!(row >> name >> attribute) || attribute != "PREG=1" || (row >> extra))
            throw std::runtime_error("Invalid DSP registered-output metadata row: " + line);
        if (!names.insert(name).second)
            throw std::runtime_error("Duplicate DSP registered-output metadata: " + name);
        auto found = usedOutputs.find(name);
        if (found == usedOutputs.end() || found->second.empty())
            throw std::runtime_error("Unknown or unused DSP in registered-output metadata: " + name);
        for (const auto &pin : found->second)
        {
            if (pin.size() < 4 || pin.substr(0, 2) != "P[" || pin.back() != ']')
                throw std::runtime_error("DSP has a used non-P output: " + name + "/" + pin);
            const auto index = pin.substr(2, pin.size() - 3);
            if (index.empty() || index.find_first_not_of("0123456789") != std::string::npos ||
                index.size() > 2 || std::stoi(index) > 47)
                throw std::runtime_error("Invalid DSP P output: " + name + "/" + pin);
        }
    }
    return names;
}
