# Lightweight CARRY DI eligibility repair

The initial packer accepts a direct DI LUT only when it is a LUT smaller than LUT6 and its DI output net has exactly one sink. Two subsequent slot reconstruction paths previously tested only macro membership and LUT type broadly. A LUT6_2 admitted via S/O6 was consequently registered again via DI/O5, sometimes at a different bit or site.

`CarryLUTRules::canUseDirectDI` now shares the original type and single-sink conditions across all three paths. Macro membership and hard-region checks remain at their existing call sites. S-side assignment, routing, assertions, and LUT/FF legality checks are unchanged. Netlist connectivity is unchanged: rejecting a DI slot does not remove its net.

The helper is inline, O(1), and uses existing type flags and vector size. It adds no allocation, cache, global scan, or placement-iteration pass. Comprehensive scans and timing counters belong only to the excluded diagnostic test executable.

`checkCarryDIEligibility` validates coverage of all macro LUTs, unique slot ownership, single/paired disjointness, legitimate LUT pairs, original addCarry assertions, repeated final mapping, and export-facing slot legality. Synthetic fixtures include valid pairs/DI-only, same-bit and cross-bit LUT6_2, input order, multi-fanout, LUT6-only DI, and a two-site carry chain. Real-case validation uses unchanged inputs and algorithm settings with isolated output directories. It is not full placement or routing acceptance.
