#include "../lib/HiFPlacer/placement/packing/DSPRegisteredOutputs.h"
#include <iostream>
#include <functional>

int main()
{
    using Outputs = std::map<std::string, std::vector<std::string>>;
    const std::string header = "AMF_DSP_REGISTERED_OUTPUTS 1\n";
    auto parse = [&](const std::string &rows, const Outputs &outputs) {
        std::istringstream input(header + rows);
        return readRegisteredDSPOutputs(input, outputs);
    };
    int rejected = 0;
    auto reject = [&](const std::string &rows, const Outputs &outputs) {
        try {parse(rows, outputs);}
        catch (const std::runtime_error &) {++rejected; return;}
        throw std::runtime_error("Invalid metadata was accepted");
    };
    Outputs registered{{"dsp", {"P[0]", "P[47]"}}};
    if (parse("dsp PREG=1\n", registered) != std::set<std::string>{"dsp"})
        throw std::runtime_error("PREG-enabled P-only DSP was not recognized");
    if (!parse("", registered).empty())
        throw std::runtime_error("Unlisted DSP acquired a register");
    reject("dsp PREG=0\n", registered);
    reject("other PREG=1\n", registered);
    reject("dsp PREG=1\ndsp PREG=1\n", registered);
    reject("dsp PREG=1 extra\n", registered);
    reject("dsp PREG=1\n", {{"dsp", {"P[0]", "ACOUT[0]"}}});
    reject("dsp PREG=1\n", {{"dsp", {"P[48]"}}});
    reject("dsp PREG=1\n", {{"dsp", {"P[x]"}}});
    reject("dsp PREG=1\n", {{"dsp", {}}});
    std::istringstream badHeader("AMF_DSP_REGISTERED_OUTPUTS 2\n");
    try {readRegisteredDSPOutputs(badHeader, registered);}
    catch (const std::runtime_error &) {++rejected;}
    if (rejected != 9) throw std::runtime_error("Missing rejection");
    std::cout << "DSP registered-output contract: valid cases and 9 rejection cases passed\n";
}
