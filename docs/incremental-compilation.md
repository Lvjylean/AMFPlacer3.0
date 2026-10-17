# Full and incremental compilation source snapshot

This `incremental_compilation` branch consolidates the complete AMF source, full-design flow, native static cache, parallel OOC module flow, joint route-only backend, DFX boundary policy, CARRY legalizer repair, and OOC clock-ownership repair. It retains the existing Git ancestry at `b5322e1fb5ff3b862c00a8b97bc963f84494ff70`; that old commit alone is **not** the version used by the accepted experiments.

## Source and validation boundaries

- `src/` is byte-identical to the complete frozen source of CARRY build 38, **build-02**, including its native-cache and edge-policy parents. Accepted AMF executable SHA256: `7ef1ef3199a9587d1267290fec36605da7130345b1720185b1fbbe831b815542`.
- Current backend scripts come from `getrf-12ns-right3-clock-ownership-42`. Their hashes and original paths are retained in `docs/provenance/incremental-compilation-20261017/source-origins.json`.
- Full V1 run38 and corrected incremental run42 passed real routing, DRC, setup/hold, required consistency checks and bitstream acceptance. The incremental route-only flow preserves all 5,912 requested A/B cell locations. The V0 static cache was produced by an older binary: this is not a claim of same-binary V0/V1 cache generation.
- Clock-guidance experiment44 is preserved under `scripts/diagnostics/clock_guidance/`. `update_clock_routing -verbose` eliminated Route 35-4475 without `place_design` in that experiment. It is **not silently inserted into the accepted run42 backend**, and experiment44 did not regenerate bitstreams. The diagnostic is specific to the recorded clock driver and run layout.
- Source consolidation/build checks do not constitute a new full FPGA experiment. Original failed attempts and elapsed times remain in the provenance records. DCPs, bitstreams, runtime caches, build outputs and logs are not included in this source commit.

## Current workflow

1. Full compilation: retain dynamic-module membership during the ordinary full netlist import, capture the static model inside AMF, and create the common static shell with both RPs blackboxed. At initial full acceptance, reconcile cached static coordinates once against the final implementation and certify the cache and resource model.
2. Incremental frontend: A/B independently perform OOC synthesis, optimization and module-netlist export; AMF binds module ports to the cached static interfaces, adds the required equivalent interface buffers, and places the dynamic cells under fixed context and hard Pblocks.
3. Incremental backend: import both placed modules into one locked static shell and run **one joint `route_design`**. The native incremental path does not call `place_design` or assemble independently routed RMs.
4. Mandatory correctness checks include placement/region/interface retention, routing/DRC/timing, clock source and timing coverage, full `pr_verify`, and full/partial bitstreams. Utilization and the detailed 100-path report run separately from the measured incremental main path.

## Features and entry points

| Feature | Main implementation |
| --- | --- |
| Full static model capture and module binding | `src/lib/HiFPlacer/designInfo/native/`, `scripts/run_dfx_full.py`, `scripts/run_native_module.py` |
| Cache identity, coordinate and resource certification | `scripts/native_cache_contract.py`, `scripts/certify_native_resources.py`, `scripts/run_native_migration.py` |
| OOC input and interface-buffer import | `scripts/dfx_native_rm_export.tcl`, `scripts/dfx_native_rm_import.tcl`, `scripts/dfx_native_interface_buffers.tcl` |
| Parallel modules / joint routing / parallel acceptance | `scripts/run_parallel_incremental.py`, `scripts/dfx_joint_backend.tcl`, `scripts/run_parallel_acceptance.py` |
| DFX edge resource policy | `docs/research/native-dfx-boundary-site-policy.md`, `src/tests/check_boundary_site_policy.cc` |
| Region-aware CARRY legalizer | `src/lib/HiFPlacer/placement/legalization/{MacroLegalizer,RegionColumnDP,ColumnOverflow}.*` |
| OOC clock handoff and coverage | `scripts/dfx_ooc_clock_handoff.tcl`, `scripts/dfx_clock_contract.tcl`, `scripts/timing_coverage.py` |

The validated scheduler currently requires two modules named A/B, four Vivado threads and twelve AMF threads. It is not yet an arbitrary-module-count scheduler. Ordinary Pro uses `ProPrimitiveTiming=false`. Hard-region-specific legalizer changes retain the ordinary non-hard-region path.

For the approved edge policy, SLICE and DSP top/bottom clock-region edges are excluded. Side-edge corner PROHIBIT sites do not cause an entire left/right column to be excluded; the policy checks the actual interior side restrictions. BRAM is not included in this extra boundary margin. Effective PROHIBIT and resource certification remain necessary; an unprohibited site alone is not a general routability guarantee.

OOC synthesis/AMF retain the requested clock period. Only the placed-module handoff removes the verified single OOC primary clock so the static top-level clock remains authoritative. Extra timing exceptions or unsupported clock arrangements fail the contract instead of being silently discarded. Enable `clock_handoff_mode=inherit_static_primary`, `clock_contract_enabled=true` and provide the actual `clock_root_pin`, `clock_rm_port`, and `clock_timing_baseline` for a new supported job.

## Build and source checks

Linux prerequisites: CMake, Ninja, C++17 compiler, OpenMP, Boost (filesystem/program_options/system/log/thread), zlib, Python 3 and Tcl. Existing vendored third-party dependencies and their licensing are retained; `PaToH/libpatoh.a` is the inherited upstream dependency, not a newly generated build artifact.

From the checkout root:

```sh
AMF_MACHINE_CONFIG=configs/machines/source-release-build.json python3 scripts/amf3.py build --jobs 8 --profile --no-set-current
python3 scripts/validate_incremental_source.py --build-dir builds/<build-id>/build
```

`source-release-build.json` is for a portable isolated **build** only. Set host-specific Vivado/input paths before running FPGA experiments. No command above starts a placement/routing experiment or a monitor.

Use `python3 scripts/run_dfx_full.py --help` and `python3 scripts/run_parallel_incremental.py --help` for the full and incremental launch interfaces. `configs/experiments/incremental-compilation-validated-38/` preserves historical server-specific configurations for provenance; do not launch those files unchanged. New jobs need a new output directory, matching shell/cache/Pblock/resource identities, current binary hashes, and clock-handoff settings. Legacy `run_dfx_multi.py`/assembly helpers are retained for historical compatibility; they are not the current native incremental workflow.

## Evidence

`docs/provenance/incremental-compilation-20261017/` contains source mappings, original build/deployment manifests, the accepted full/incremental checks, clock repair and guidance results, and accounting limitations. Existing `no_clock=1` and `unconstrained_internal_endpoints=1` match the full baseline; reports do not claim those counts are zero. Final DCP and bitstream server paths are recorded there, but the large files remain on the servers.

Source-release validation: the isolated build and recorded source regressions passed. See `docs/provenance/incremental-compilation-20261017/release-validation-summary.json`. The SRL input audit executable was compiled only because it requires design inputs; it was not reported as an executed test.
