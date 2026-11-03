#ifndef AMF_CARRY_LUT_RULES_H
#define AMF_CARRY_LUT_RULES_H

#include "DesignInfo.h"

namespace CarryLUTRules
{
// Match InitialPacker's existing DI absorption rule. A LUT6_2 admitted via
// S/O6 owns one full LUT; its O5 fanout must not create another DI-side BEL.
// These are constant-time field/size checks, with no allocation or net walk.
inline bool canUseDirectDI(DesignInfo::DesignCell *cell, DesignInfo::DesignNet *net)
{
    return cell && cell->isLUT() && !cell->isLUT6() && net && net->getPinsBeDriven().size() == 1;
}
}

#endif
