#pragma once

#include "DesignInfo.h"

inline DesignInfo::DesignPin *uniqueFFDataDriver(DesignInfo::DesignCell *ff)
{
    for (auto pin : ff->getInputPins())
    {
        if (pin->getRefPinName() != "D") continue;
        auto net = pin->getNet();
        if (!net || net->checkIsPowerNet() || net->getDriverPins().size() != 1) return nullptr;
        return net->getDriverPins().front();
    }
    return nullptr;
}

inline bool isSRLQDriver(DesignInfo::DesignPin *driver)
{
    return driver && driver->getRefPinName() == "Q" && driver->getCell()->originallyIsShifter();
}

inline bool hasDirectSRLData(DesignInfo::DesignCell *ff)
{
    return isSRLQDriver(uniqueFFDataDriver(ff));
}
