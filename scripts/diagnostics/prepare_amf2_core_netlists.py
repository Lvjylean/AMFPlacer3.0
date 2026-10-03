#!/usr/bin/env python3
"""Export/validate U250 OOC netlists; never starts placement or routing."""
import argparse
from collections import defaultdict
import concurrent.futures
import csv
import datetime as dt
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from convert_netlist_inventory import convert, digest

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT/'builds/build-20260929-185044-227956-c70df682/build/AMFPlacer'


def save(p, data):
    q=p.with_suffix(p.suffix+'.tmp')
    q.write_text(json.dumps(data,indent=2)+'\n')
    q.replace(p)


def one(item):
    source=Path(item['directory'])
    synthesis=json.loads((source/'synthesis_status.json').read_text())
    if synthesis['state']!='completed':
        raise RuntimeError('Synthesis not complete: '+str(source))
    dcp=source/'post_opt.dcp'
    if digest(dcp)!=synthesis['artifacts']['post_opt.dcp']['sha256']:
        raise RuntimeError('Synthesis DCP changed')
    directory=source/item.get('preparation_tag','amf-preparation')
    directory.mkdir(exist_ok=False)
    inventory=directory/'inventory'
    state=dict(case=item['case'],state='running',started=dt.datetime.now().astimezone().isoformat(),stages=[])
    save(directory/'status.json',state)

    def stage(name, command):
        begin=time.monotonic()
        with (directory/(name+'.log')).open('w') as f:
            p=subprocess.run(list(map(str,command)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
        state['stages'].append(dict(name=name,command=list(map(str,command)),seconds=time.monotonic()-begin,exit_code=p.returncode))
        save(directory/'status.json',state)
        if p.returncode:
            raise RuntimeError(name+' failed: '+str(directory/(name+'.log')))

    try:
        exporter=ROOT/'scripts/diagnostics'/item.get('exporter','export_amf2_core_inventory.tcl')
        shutil.copy2(exporter,directory/exporter.name)
        stage('export',['/Projects/Xilinx/Vivado/2024.2/bin/vivado','-mode','batch','-notrace','-nojournal',
                        '-log',directory/'vivado.log','-source',directory/exporter.name,'-tclargs',dcp,inventory])
        manifest=convert(inventory/'cells.tsv',inventory/'nets.tsv',directory/'netlist.zip',dcp,inventory/'binding.json')
        if len(manifest['clocks'])!=1:
            raise RuntimeError('Expected one core clock; inspect inventory: '+str(manifest['clocks']))
        # Certify only DSPs with PREG=1 and exclusively registered P outputs
        # actually driving canonical internal leaf pins. Do not infer this from
        # the top clock period or from the DSP primitive type alone.
        cells={r['cell_id']:r for r in csv.DictReader((inventory/'cells.tsv').open(),delimiter='\t')}
        preg={r['cell']:int(r['PREG']) for r in csv.DictReader((inventory/'dsp_preg.tsv').open(),delimiter='\t')}
        used=defaultdict(set)
        csv.field_size_limit(64*1024*1024)
        for r in csv.DictReader((inventory/'nets.tsv').open(),delimiter='\t'):
            if not any(not p.startswith('PORT:') for p in r['sinks'].split(',') if p):
                continue
            ident,pin=r['drivers'].split(':',1)
            if ident in cells and cells[ident]['primitive']=='DSP48E2':
                used[cells[ident]['cell_name']].add(pin)
        names=sorted(n for n,pins in used.items() if preg[n]==1 and all(re.fullmatch(r'P\[(?:[0-9]|[1-3][0-9]|4[0-7])\]',p) for p in pins))
        (directory/'dsp_registered_outputs.txt').write_text('AMF_DSP_REGISTERED_OUTPUTS 1\n'+''.join(n+' PREG=1\n' for n in names))
        save(directory/'dsp_certification.json',dict(certified_count=len(names),used_outputs={n:sorted(p) for n,p in used.items()},preg=preg))
        config=json.loads((ROOT/'experiments/runs/getrf-u250-full-20260929-133345-334380/config.json').read_text())
        config['vivado extracted design information file']=str(directory/'netlist.zip')
        config['clock file']=str(directory/'netlist.clocks')
        config['ClockPeriod']=str(item['amf_period_ns'])
        config['DSP registered outputs file']=str(directory/'dsp_registered_outputs.txt')
        config['dumpDirectory']=str(directory/'unused-inspection-dump')
        config.pop('BoundaryReportDirectory',None)
        overrides=item.get('config_overrides',{})
        if set(overrides)-{'vivado extracted device information file','physical boundary model file','fixed units file',
                           'cellType2fixedAmo file','cellType2sharedCellType file','sharedCellType2BELtype file'}:
            raise RuntimeError('Only physical input compatibility overrides are accepted')
        config.update(overrides)
        save(directory/'r10_config.json',config)
        save(directory/'input_provenance.json',dict(input_dcp_sha256=digest(dcp),scope='compute-core OOC',
             source_manifest=str(source/'input_manifest.json'),source_manifest_sha256=digest(source/'input_manifest.json'),
             vivado_clock_source='Original corresponding core clock period mapped to OOC boundary; see input_manifest.json',
             original_amf_period_ns=item['amf_period_ns'],vivado_core_period_ns=item['vivado_period_ns'],
             io_timing_scope='register-to-register; external I/O budgets not specified',
             r10_reference='experiments/runs/getrf-u250-full-20260929-133345-334380',
             global_reset_buffer_adapter=item.get('global_reset_buffer_adapter')))
        stage('inspect',['python3','scripts/amf3.py','inspect','--binary',BINARY,'--config',directory/'r10_config.json'])
        lines=(directory/'inspect.log').read_text().splitlines()
        inspect_dir=Path(next(x for x in lines if x.startswith(str(ROOT/'experiments/preflight/input-inspection-'))))
        shutil.copy2(inspect_dir/'inputs.json',directory/'inputs.json')
        state.update(state='completed',inspection_directory=str(inspect_dir),netlist=manifest,
                     finished=dt.datetime.now().astimezone().isoformat())
    except Exception as e:
        state.update(state='failed',error=str(e),finished=dt.datetime.now().astimezone().isoformat())
    save(directory/'status.json',state)
    print(json.dumps({k:state[k] for k in ('case','state','error') if k in state}),flush=True)
    return state


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('catalog',type=Path)
    p.add_argument('--case',choices=['minimap2','optimsoc','openpiton','memn2n'])
    args=p.parse_args()
    items=json.loads(args.catalog.read_text())
    if args.case: items=[i for i in items if i['case']==args.case]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        states=list(pool.map(one,items))
    sys.exit(0 if all(s['state']=='completed' for s in states) else 1)
