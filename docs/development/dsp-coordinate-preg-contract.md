# DSP coordinates and PREG contract

This repair is isolated from the frozen 20261022 SLL study. It does not change the SLL function, first-order coefficient, QP injection, stage budgets, or defaults.

## Hard-macro coordinates

A member assigned to site `(sx, sy)` with macro offset `(dx, dy)` constrains the PU anchor to `(sx-dx, sy-dy)`. `getNonCLBPackedAnchors()` collects and checks every member before writeback. `setPULocationToPackedSite()` writes each hard PU once, then updates the existing grid; the visualization override uses the same collection. Conflicting member sites, nonfinite coordinates, and changes to locked hard PUs fail explicitly. DSP export additionally checks the member coordinate against the exported site. LOC/BEL selection is not modified.

## Authoritative DSP properties

`export_dsp_timing_properties.tcl` only opens an existing DCP, exports DSP48E2 properties, and closes it. It does not place, optimize, route, or write a checkpoint. The schema records PREG, AREG, ACASCREG, BREG, BCASCREG, MREG, CREG, DREG, ADREG, the source DCP SHA-256, and the input netlist ZIP SHA-256. The caller must obtain these hashes from the actual files; the extraction manifest records and checks the DCP again after extraction.

A full-input configuration enables this path with:

- `DSP timing properties file`: exported TSV path.
- `DSP timing properties sha256`: SHA-256 of that exact TSV.
- `DSP timing source DCP sha256`: source checkpoint hash.

DesignInfo checks the metadata bytes, verifies the actual netlist ZIP hash, rejects duplicate/unknown/missing DSPs, and sets an explicit PREG value before initial packing. Legacy graph inference cannot overwrite either 0 or 1. In the timing graph this value takes precedence over `DSPCritical`. The old `DSP registered outputs file` cannot be combined with this metadata. Without this new input, legacy behavior is retained; an old configuration does not silently gain authoritative attributes.

The current graph has one node per cell. The supported exact output-boundary case is PREG=1 with all used outputs in P[0..47]. Used non-P or mixed outputs with PREG=1 are rejected, requiring a pin-level extension rather than silently cutting a combinational bypass. PREG=0 retains the existing combinational cell approximation, including designs with AREG/BREG/MREG or other internal pipeline properties; this repair does not claim to model those internal pin-to-pin stages. Native static-cache input with authoritative metadata is rejected until a corresponding cache binding is implemented.

## Validation without placement

`prepare_dsp_regression.py` creates two explicitly labeled fixtures from existing artifacts: coordinate replay from frozen PU member offsets plus exported sites; independent synthetic FF–LUT–DSP–LUT–FF timing cones using all real PREG values. It preserves the original metadata and labels the derived netlist hash separately.

- `checkDSPMacroCoordinates`: production hard-site writeback, visualization overrides before writeback, repeated writeback, grid coordinates, exported DSP Tcl, locked/conflicting sites, singleton and nonfinite inputs.
- `checkDSPTimingProperties`: complete real DSP property/output compatibility plus malformed/mixed input rejection.
- `checkDSPTimingGraph`: production DesignInfo loader, authoritative precedence over inference, register classification with both DSPCritical settings, levelization, and arrival propagation (2-edge path after PREG=1 vs 4 edges with PREG=0).
- `checkDSPRegisteredOutputs`: compatibility of the previous narrow metadata parser.
- `checkSllQuantity` and `checkSllVisualizationDiagnostics`: original model, finite differences, first-order QP coefficient, and frozen objective diagnostics.

These checks do not run AMF global/detailed placement or Vivado placement/routing. They establish coordinate and graph invariants, not new placement quality, WNS, or SLL results.
