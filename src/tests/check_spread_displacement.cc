// Regression for two-dimensional spreading step limits; calls the production PU method.
#include "PlacementInfo.h"
#include <cmath>
#include <iostream>

int main()
{
    struct Case
    {
        const char *name;
        float targetX, targetY, alpha, expectedX, expectedY;
        bool hasHistory;
    };
    const Case cases[] = {
        {"pure-y-positive", 20, 300, 0.5f, 20, 210, true},
        {"pure-x-positive", 120, 200, 0.5f, 30, 200, true},
        {"pure-y-negative", 20, 100, 0.5f, 20, 190, true},
        {"pure-x-negative", -80, 200, 0.5f, 10, 200, true},
        {"diagonal-3-4", 50, 240, 0.5f, 26, 208, true},
        {"below-limit", 26, 208, 0.5f, 23, 204, true},
        {"exact-limit", 26, 208, 1.0f, 26, 208, true},
        {"zero-movement", 20, 200, 0.5f, 20, 200, true},
        {"zero-forgetting", 120, 300, 0.0f, 20, 200, true},
        {"first-spread-no-history", 20, 300, 0.5f, 20, 300, false},
    };
    int failures = 0;
    for (const auto &c : cases)
    {
        PlacementInfo::PlacementUnit pu(c.name, 0, PlacementInfo::PlacementUnitType_UnpackedCell);
        pu.setAnchorLocation(20, 200);
        if (c.hasHistory)
            pu.recordSpreadLocatin();
        pu.setSpreadLocation_WithLimitDisplacement(c.targetX, c.targetY, c.alpha, 10);
        const bool finite = std::isfinite(pu.X()) && std::isfinite(pu.Y());
        const bool expected = std::fabs(pu.X() - c.expectedX) < 1e-4f &&
                              std::fabs(pu.Y() - c.expectedY) < 1e-4f;
        const bool bounded = !c.hasHistory ||
                             std::hypot(pu.X() - 20, pu.Y() - 200) <= 10.0001f;
        const bool ok = finite && expected && bounded;
        std::cout << (ok ? "PASS " : "FAIL ") << c.name << " actual=("
                  << pu.X() << "," << pu.Y() << ") expected=("
                  << c.expectedX << "," << c.expectedY << ")\n";
        failures += !ok;
    }
    std::cout << "failures=" << failures << " total=" << sizeof(cases) / sizeof(cases[0]) << "\n";
    return failures ? 1 : 0;
}
