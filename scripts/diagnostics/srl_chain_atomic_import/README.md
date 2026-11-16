# SRL-chain atomic import adapter

This opt-in adapter is the exact helper used by the 2026-11-15 SRL-chain import and locked-place validation. It changes native transaction boundaries, while keeping every requested LOC/BEL unchanged. It does not run AMF, Vivado placement, or routing itself.

After the ordinary backend preparation (which already clears ordinary LUT groups while preserving LUT6_2 pairs, and installs atomic CARRY-site import), apply once:

```sh
python3 scripts/diagnostics/srl_chain_atomic_import/apply.py --backend /path/to/prepared-backend
```

The backend must contain `placement/import_placement.tcl`, `requested.tsv`, `srl_cascades.tsv`, `carry_site_groups.tsv`, and `inputs/carry_site_import.tcl`. The adapter derives complete acyclic chains, checks exact targets and absence of overlap with CARRY-site submissions, and writes its inventory and preflight report. Each chain is submitted once on its first reference; a rejected chain is never split or silently retried. Non-SRL cells retain the existing CARRY/native batching behavior. Applying it twice is rejected by the installer assertions; prepare a fresh backend instead.

The diagnostic logger is installed first because SRL native submissions use the same rejection-membership logger. This snapshot keeps the adapter explicit rather than silently changing all existing backend defaults.

Validation: `python3 scripts/diagnostics/srl_chain_atomic_import/test_wrapper.py` (Python 3 and Tcl 8.6). The four-chain native Vivado small case preserved all 12 targets and removed the 12 target shape warnings. Full import and locked place retained all 1,156,542 requested targets; all 216 chains / 936 SRL members succeeded. Subsequent routing preserved LOC/BEL for all 1,408,740 expanded primitives. No DCP, full layout, or runtime log is included here.
