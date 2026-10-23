#!/usr/bin/env python3
"""Collect small preparation evidence into the existing batch report bundle."""
import json
from pathlib import Path
import shutil
import sys

catalog = Path(sys.argv[1]).resolve()
batch = Path(sys.argv[2]).resolve()
assert (batch/'netlist_gate.json').is_file()
output = batch/'reports/preparation'
output.mkdir(parents=True, exist_ok=True)
shutil.copy2(catalog, output/'catalog.json')
shutil.copy2(batch/'netlist_gate.json', output/'netlist_gate.json')
index = []
for item in json.loads(catalog.read_text()):
    source = Path(item['directory'])
    prep = source/item.get('preparation_tag','amf-preparation')
    target = output/item['case']
    target.mkdir(exist_ok=True)
    for name in ('synthesis_status.json','input_manifest.json','synthesis_summary.tsv','clocks.rpt',
                 'check_timing.rpt','utilization.rpt','timing_summary.rpt','divider_properties.rpt'):
        if (source/name).is_file():shutil.copy2(source/name,target/name)
    (target/'amf-inputs').mkdir(exist_ok=True)
    for name in ('status.json','netlist.manifest.json','r10_config.json','input_provenance.json','inputs.json','dsp_certification.json'):
        if (prep/name).is_file():shutil.copy2(prep/name,target/'amf-inputs'/name)
    manifest=json.loads((prep/'netlist.manifest.json').read_text())
    synthesis=json.loads((source/'synthesis_status.json').read_text())
    index.append(dict(case=item['case'],amf_cells=manifest['amf_cell_count'],amf_period_ns=item['amf_period_ns'],
                      vivado_period_ns=item['vivado_period_ns'],amf_netlist=str(prep/'netlist.zip'),
                      post_synth_dcp=str(source/'post_synth.dcp'),post_opt_dcp=str(source/'post_opt.dcp'),
                      artifacts=synthesis['artifacts'],scope='compute-core OOC',dcp_downloaded=False))
(output/'input_index.json').write_text(json.dumps(index,indent=2)+'\n')
print(output)
