#ifndef AMF_RAM_FF_PROFILES_H
#define AMF_RAM_FF_PROFILES_H
#include "DesignInfo.h"

namespace MemoryFFRules {
// Fixed-BEL U250 witnesses from tests/physical/audit_ram_pair_profiles.py.
// Entries follow AFF, AFF2, ... HFF2. "*" is independent D and "-" is unavailable.
// Named pins require the same physical signal. Select one whole-site profile.
struct RAMFFProfile {
    DesignInfo::DesignCellType type;
    const char *sources[16];
};
static const RAMFFProfile ramProfiles[] = {
    // RAM32M16_read
    {DesignInfo::CellType_RAM32M16, {"DOA[0]", "DOA[1]", "DOB[0]", "DOB[1]", "DOC[0]", "DOC[1]", "DOD[0]", "DOD[1]",
                            "DOE[0]", "DOE[1]", "DOF[0]", "DOF[1]", "DOG[0]", "DOG[1]", "DOH[0]", "DOH[1]"}},
    // RAM32M16_alternate_read
    {DesignInfo::CellType_RAM32M16, {"DOA[1]", "DOA[0]", "DOB[1]", "DOB[0]", "DOC[1]", "DOC[0]", "DOD[1]", "DOD[0]",
                            "DOE[1]", "DOE[0]", "DOF[1]", "DOF[0]", "DOG[1]", "DOG[0]", "DOH[1]", "DOH[0]"}},
    // RAM32M16_alternate_write
    {DesignInfo::CellType_RAM32M16, {"DIA[1]", "DIA[0]", "DIB[1]", "DIB[0]", "DIC[1]", "DIC[0]", "DID[1]", "DID[0]",
                            "DIE[1]", "DIE[0]", "DIF[1]", "DIF[0]", "DIG[1]", "DIG[0]", "DIH[1]", "DIH[0]"}},
    // RAM32M_read
    {DesignInfo::CellType_RAM32M, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "DOA[0]", "DOA[1]", "DOB[0]", "DOB[1]", "DOC[0]", "DOC[1]", "DOD[0]", "DOD[1]"}},
    // RAM32M_alternate_read
    {DesignInfo::CellType_RAM32M, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "DOA[1]", "DOA[0]", "DOB[1]", "DOB[0]", "DOC[1]", "DOC[0]", "DOD[1]", "DOD[0]"}},
    // RAM32M_alternate_write
    {DesignInfo::CellType_RAM32M, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "DIA[1]", "DIA[0]", "DIB[1]", "DIB[0]", "DIC[1]", "DIC[0]", "DID[1]", "DID[0]"}},
    // RAM64M8_read
    {DesignInfo::CellType_RAM64M8, {"*", "DOA", "*", "DOB", "*", "DOC", "*", "DOD",
                            "*", "DOE", "*", "DOF", "*", "DOG", "*", "DOH"}},
    // RAM64M8_shared_write
    {DesignInfo::CellType_RAM64M8, {"*", "DIA", "*", "DIB", "*", "DIC", "*", "DID",
                            "*", "DIE", "*", "DIF", "*", "DIG", "*", "DIH"}},
    // RAM64M_read
    {DesignInfo::CellType_RAM64M, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "*", "DOA", "*", "DOB", "*", "DOC", "*", "DOD"}},
    // RAM64M_shared_write
    {DesignInfo::CellType_RAM64M, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "*", "DIA", "*", "DIB", "*", "DIC", "*", "DID"}},
    // RAM64X1D_read
    {DesignInfo::CellType_RAM64X1D, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "*", "*", "*", "*", "*", "DPO", "*", "SPO"}},
    // RAM64X1D_shared_write
    {DesignInfo::CellType_RAM64X1D, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "*", "*", "*", "*", "*", "D", "*", "D"}},
    // RAM256X1S_read
    {DesignInfo::CellType_RAM256X1S, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "-", "-", "-", "-", "O", "O", "*", "-"}},
    // RAM32X1D_both_local
    {DesignInfo::CellType_RAM32X1D, {"*", "*", "*", "*", "*", "*", "*", "*",
                            "*", "*", "*", "*", "DPO", "DPO", "SPO", "SPO"}},
};
} // namespace MemoryFFRules
#endif
