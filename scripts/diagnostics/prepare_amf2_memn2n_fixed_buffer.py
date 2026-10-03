#!/usr/bin/env python3
"""Adapt the single synthesized reset BUFG to existing r10 fixed resources."""
import csv
import datetime as dt
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from prepare_amf2_core_netlists import ROOT, BINARY, save, digest

base = Path(sys.argv[1]).resolve()
source = base/'memn2n-retry06/amf-preparation'
assert json.loads((source/'status.json').read_text())['state'] != 'running'
target = source.with_name('fixed-buffer-inputs')
target.mkdir(exist_ok=False)
cells = list(csv.DictReader((source/'inventory/cells.tsv').open(), delimiter='\t'))
buffers = [r for r in cells if r['primitive'] == 'BUFGCE']
assert len(buffers) == 1
buffer = buffers[0]
csv.field_size_limit(64*1024*1024)
nets = [r for r in csv.DictReader((source/'inventory/nets.tsv').open(), delimiter='\t')
        if r['drivers'] == buffer['cell_id']+':O']
assert len(nets) == 1 and nets[0]['type'] == 'SIGNAL'
sinks = nets[0]['sinks'].split(',')
assert len(sinks) == 33762 and {p.split(':')[1] for p in sinks} == {'CLR', 'PRE'}
device = ROOT/'experiments/preflight/20260929-u250-fixed-resource-sites-181309/device-r10-fixed'
rows = [line.split('\t') for line in (device/'model/physical_structure.tsv').read_text().splitlines()]
fabric = [r for r in rows if r[0]=='SITE' and r[2] in ('SLICEL','SLICEM')]
center = [(min(float(r[i]) for r in fabric)+max(float(r[i]) for r in fabric))/2 for i in (3,4)]
sites = [r for r in rows if r[0]=='SITE' and r[2]=='BUFGCE' and r[6]=='0']
site = min(sites, key=lambda r: ((float(r[3])-center[0])**2+(float(r[4])-center[1])**2,r[1]))
(target/'fixed_units').write_text('# Fixed global reset buffer anchor; Vivado may relocate after strict import\n'
    + f"name=> {buffer['cell_name']} loc=> {site[1]} bel=> {site[1]}/BUFCE\n")
overrides={'vivado extracted device information file':str(device/'exportSiteLocation.zip'),
           'physical boundary model file':str(device/'model/physical_structure.tsv'),
           'fixed units file':str(target/'fixed_units')}
for key,name in (('cellType2fixedAmo file','cellType2fixedAmo'),('cellType2sharedCellType file','cellType2sharedCellType'),
                 ('sharedCellType2BELtype file','sharedCellType2BELtype')):
    overrides[key]=str(device/'compatibility'/name)
adapter=dict(cell=buffer['cell_name'],sink_count=len(sinks),sink_pins=['CLR','PRE'],
    signal_type='reset/control, not a STA clock',anchor_site=site[1],anchor_method='nearest valid BUFGCE to fabric bounding-box center',
    anchor_center=center,release_after_strict_import=True,device_extension=str(device),
    model_sha256=digest(device/'model/physical_structure.tsv'),fixed_units_sha256=digest(target/'fixed_units'),
    source_preparation=str(source))
save(target/'adapter.json',adapter)
items=json.loads((base/'catalog-v11.json').read_text())
for item in items:
    if item['case']=='memn2n':
        item.update(preparation_tag='amf-preparation-v6',exporter='export_amf2_core_inventory_v6.tcl',
                    config_overrides=overrides,global_reset_buffer_adapter=adapter,release_fixed_clock_buffers=True)
save(base/'catalog-v12.json',items)
print(json.dumps(adapter,indent=2))
