#!/usr/bin/env python3
"""Gate all three prepared core netlists, then run r10 full flows sequentially."""
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from convert_netlist_inventory import digest

ROOT=Path(__file__).resolve().parents[2]
BINARY=ROOT/'builds/build-20260929-185044-227956-c70df682/build/AMFPlacer'
EXPECTED_BINARY='087dc49db826304f551809fe6c75d30db85ea4014ec8850eedd1741518d94bd7'


def save(path,data):
    tmp=path.with_suffix(path.suffix+'.tmp')
    tmp.write_text(json.dumps(data,indent=2)+'\n')
    tmp.replace(path)


def main():
    catalog=Path(sys.argv[1]).resolve()
    output=Path(sys.argv[2]).resolve()
    if digest(BINARY)!=EXPECTED_BINARY:
        raise RuntimeError('Unexpected AMF binary')
    items=json.loads(catalog.read_text())
    if {i['case'] for i in items}!={'minimap2','optimsoc','memn2n'}:
        raise RuntimeError('Incomplete three-case catalog')
    gate=[]
    for item in items:
        directory=Path(item['directory'])
        prep=directory/item.get('preparation_tag','amf-preparation')
        status=json.loads((prep/'status.json').read_text())
        synthesis=json.loads((directory/'synthesis_status.json').read_text())
        if status['state']!='completed' or synthesis['state']!='completed':
            raise RuntimeError('Netlists must all be complete before placement: '+item['case'])
        synthesis_log=(directory/'console.log').read_text(errors='replace')
        if re.search(r'could not open \$readmem|cannot open.*\.mem',synthesis_log,re.I):
            raise RuntimeError('Unresolved memory initialization: '+item['case'])
        if digest(directory/'post_opt.dcp')!=synthesis['artifacts']['post_opt.dcp']['sha256']:
            raise RuntimeError('DCP hash changed: '+item['case'])
        if digest(prep/'netlist.zip')!=status['netlist']['output_sha256']:
            raise RuntimeError('AMF input changed: '+item['case'])
        timing=(directory/'check_timing.rpt').read_text()
        checks={name:int(re.search(r'checking '+name+r' \((\d+)\)',timing).group(1))
                for name in ('no_clock','constant_clock','unconstrained_internal_endpoints','multiple_clock','loops','latch_loops')}
        if any(checks.values()):
            raise RuntimeError('Core timing coverage gate failed: '+repr(checks))
        summary=(directory/'synthesis_summary.tsv').read_text()
        if 'black_boxes\t0\n' not in summary or 'part\txcu250-figd2104-2L-e\n' not in summary:
            raise RuntimeError('Black-box/target gate failed')
        config=json.loads((prep/'r10_config.json').read_text())
        reference=json.loads((ROOT/'experiments/runs/getrf-u250-full-20260929-133345-334380/config.json').read_text())
        allowed={'vivado extracted design information file','clock file','ClockPeriod',
                 'DSP registered outputs file','dumpDirectory','BoundaryReportDirectory'}
        overrides=item.get('config_overrides',{})
        if overrides:
            if item['case']!='memn2n' or not item.get('release_fixed_clock_buffers'):
                raise RuntimeError('Unexpected resource adapter')
            device=ROOT/'experiments/preflight/20260929-u250-fixed-resource-sites-181309/device-r10-fixed'
            expected={'vivado extracted device information file':str(device/'exportSiteLocation.zip'),
                      'physical boundary model file':str(device/'model/physical_structure.tsv'),
                      'fixed units file':str(directory/'fixed-buffer-inputs/fixed_units')}
            for key,name in (('cellType2fixedAmo file','cellType2fixedAmo'),('cellType2sharedCellType file','cellType2sharedCellType'),
                             ('sharedCellType2BELtype file','sharedCellType2BELtype')):
                expected[key]=str(device/'compatibility'/name)
            if overrides!=expected or any(config.get(k)!=v for k,v in expected.items()):
                raise RuntimeError('Physical resource compatibility inputs changed')
            adapter=item['global_reset_buffer_adapter']
            if digest(Path(expected['fixed units file']))!=adapter['fixed_units_sha256']:
                raise RuntimeError('Fixed buffer anchor changed')
            allowed.update(expected)
        differences={k:dict(reference=reference.get(k),effective=config.get(k)) for k in set(config)|set(reference)
                     if config.get(k)!=reference.get(k)}
        if set(differences)-allowed:
            raise RuntimeError('Non-r10 algorithm settings: '+repr(differences))
        item['amf_cell_count']=status['netlist']['amf_cell_count']
        item['preparation']=str(prep)
        gate.append(dict(case=item['case'],timing_checks=checks,config_differences=differences,
                         derived_input_sha256={p.name:digest(p) for p in directory.iterdir() if p.suffix in ('.sv','.xdc','.tcl')},
                         input_dcp_sha256=digest(directory/'post_opt.dcp'),netlist_sha256=digest(prep/'netlist.zip'),
                         config_sha256=digest(prep/'r10_config.json'),amf_cell_count=item['amf_cell_count']))
    output.mkdir(parents=True,exist_ok=False)
    save(output/'netlist_gate.json',dict(all_three_ready=True,catalog=str(catalog),catalog_sha256=digest(catalog),
                                       timestamp=dt.datetime.now().astimezone().isoformat(),binary_sha256=EXPECTED_BINARY,cases=gate))
    state=dict(state='running',concurrency=1,order=[i['case'] for i in sorted(items,key=lambda x:x['amf_cell_count'])],
               started=dt.datetime.now().astimezone().isoformat(),cases=[])
    save(output/'status.json',state)
    for item in sorted(items,key=lambda x:x['amf_cell_count']):
        prep=Path(item['preparation'])
        command=['python3','scripts/amf3.py','full-run','--profile','--run-prefix','amf2-'+item['case']+'-u250-core-r10',
                 '--binary',str(BINARY),'--config',str(prep/'r10_config.json'),
                 '--dcp',str(Path(item['directory'])/'post_opt.dcp'),'--input-provenance',str(prep/'input_provenance.json')]
        if item.get('release_fixed_clock_buffers'):
            command.append('--release-fixed-clock-buffers')
        record=dict(case=item['case'],state='running',command=command,started=dt.datetime.now().astimezone().isoformat())
        state['cases'].append(record)
        save(output/'status.json',state)
        begin=time.monotonic()
        with (output/(item['case']+'.log')).open('w') as log:
            process=subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            record['pid']=process.pid
            save(output/'status.json',state)
            code=process.wait()
        lines=(output/(item['case']+'.log')).read_text().splitlines()
        run=next((x for x in lines if x.startswith(str(ROOT/'experiments/runs'))),None)
        record.update(state='completed' if code==0 else 'failed',exit_code=code,run_directory=run,
                      elapsed_seconds=time.monotonic()-begin,finished=dt.datetime.now().astimezone().isoformat())
        if run and (Path(run)/'reports/summary.json').exists():
            record['summary']=json.loads((Path(run)/'reports/summary.json').read_text())
        save(output/'status.json',state)
        print(json.dumps(record),flush=True)
    state.update(state='completed' if all(r['exit_code']==0 for r in state['cases']) else 'failed',
                 finished=dt.datetime.now().astimezone().isoformat())
    save(output/'status.json',state)
    return 0 if state['state']=='completed' else 1


if __name__=='__main__':
    sys.exit(main())
