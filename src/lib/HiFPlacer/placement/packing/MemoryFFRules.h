#ifndef AMF_MEMORY_FF_RULES_H
#define AMF_MEMORY_FF_RULES_H
#include <array>
#include <cstddef>

// U250 phase-1 policy; see MEM-FF-D in slice-packing-rules.json.
// SRL16E/SRLC32E/RAM64X1S 6LUT footprints; MUX select reservations
// are precomputed once per site by prepareMemoryFFPacking.
namespace MemoryFFRules {
inline unsigned slotBit(unsigned row, unsigned lane) {
    return 1u << ((row / 4) * 8 + lane * 4 + row % 4);
}
template<class Net>
unsigned allowedSlots(unsigned localOnlySecondaryRows, const std::array<Net*,8>& primaryQ,
                      const std::array<Net*,8>& secondaryQ,
                      Net* data, unsigned blockedQ, unsigned localOnlyPrimaryRows=0) {
    if (!data) return 0;
    unsigned mask=0;
    for (unsigned row=0;row<8;++row) {
        // SRL16 consumes the occupied row's independent primary bypass.
        // Its local Q remains usable. Other rows retain their X bypass.
        if (!(localOnlyPrimaryRows & (1u<<row)) || primaryQ[row]==data)
            mask |= slotBit(row,0);
        // The independent Q2 bypass needs resources consumed by a 32/64-bit
        // memory LUT. Its own O6 can still drive Q2. Empty LUTs stay reserved
        // for any required route-through: this policy never adds logic LUTs.
        if (!(localOnlySecondaryRows & (1u<<row)) || secondaryQ[row]==data)
            mask |= slotBit(row,1);
    }
    return mask & ~blockedQ;
}
// Same-control FFs need not preserve the greedy four-cell group boundaries.
// Bounded augmenting paths: at most 16 FFs x 16 slots, no allocation or scan
// beyond this site. A failed insertion cannot alter the caller's candidate.
inline bool augmentSlot(unsigned ff, const std::array<unsigned,16>& masks,
                        std::array<int,16>& owner, unsigned &visited) {
    for (unsigned slot=0;slot<16;++slot) {
        unsigned bit=1u<<slot;
        if (!(masks[ff]&bit) || (visited&bit)) continue;
        visited |= bit;
        if (owner[slot]<0 || augmentSlot(owner[slot],masks,owner,visited)) {
            owner[slot]=ff;
            return true;
        }
    }
    return false;
}
inline bool matchSameControl(const std::array<unsigned,16>& masks, unsigned count,
                             std::array<int,16>& owner) {
    owner.fill(-1);
    if(count>16) return false;
    for(unsigned ff=0;ff<count;++ff) {
        unsigned visited=0;
        if(!augmentSlot(ff,masks,owner,visited)) return false;
    }
    return true;
}
// Exact four-slot matching, not a first-fit rejection. At most 4! witnesses.
inline bool matchBank(const std::array<unsigned,4>& masks, unsigned count,
                      std::array<int,4>& assignment, unsigned pos=0, unsigned used=0) {
    if (count>4) return false;
    if (pos==count) return true;
    for (unsigned row=0;row<4;++row) if ((masks[pos] & (1u<<row)) && !(used & (1u<<row))) {
        assignment[pos]=row;
        if (matchBank(masks,count,assignment,pos+1,used | (1u<<row))) return true;
    }
    assignment[pos]=-1;
    return false;
}
}
#endif
