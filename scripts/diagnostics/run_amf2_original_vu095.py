#!/usr/bin/env python3
"""Run immutable upstream AMF2 inputs and binary through the recorded entry point."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from inspect_amf_inputs import digest


def save(path, value):
    temporary = path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value, indent=2)+'\n')
    temporary.replace(path)


def prepare(batch, build):
    repository = Path('/Projects/jinyang/AMF-Placer')
    record = json.loads((build/'manifest.json').read_text())
    assert record['state'] == 'completed' and not record['source_modified']
    binary = build/'build/AMFPlacer'
    assert digest(binary) == record['binaries']['AMFPlacer']
    commit = record['source_commit']
    items = []
    for name in ('memn2n', 'optimsoc', 'minimap2'):
        bundle = ROOT/'data/reference/amf2-cases-20260930/cases'/name
        case = json.loads((bundle/'case.json').read_text())
        pair = json.loads((bundle/'backend_pair.json').read_text())
        checked = {}
        for key, value in dict(case['inputs'], original_config=case['original_config']).items():
            relative = Path(value['source']).relative_to(ROOT).as_posix()
            original = subprocess.check_output(['git','-C',str(repository),'show',commit+':'+relative])
            sha = hashlib.sha256(original).hexdigest()
            if sha != value['sha256'] or digest(Path(value['snapshot'])) != sha:
                raise RuntimeError('Input differs from upstream Git blob: '+relative)
            checked[key] = dict(path=value['snapshot'], upstream_path=relative, sha256=sha)
        config = {k:json.loads(v) for k,v in re.findall(r'^\s*"([^"\n]+)"\s*:\s*("(?:[^"\\]|\\.)*")',
                  (bundle/'original_config.jsonc').read_text(), re.M)}
        for key, value in case['inputs'].items():
            config[key] = value['snapshot']
        dcp = Path(pair['chosen']['dcp'])
        if digest(dcp) != pair['chosen']['sha256']:
            raise RuntimeError('DCP changed: '+str(dcp))
        effective = batch/'inputs'/(name+'_config.json')
        save(effective, config)
        provenance = dict(case=name, experiment='original AMF2 full VU095 benchmark',
            input_dcp_sha256=pair['chosen']['sha256'], upstream_commit=commit,
            verified_upstream_inputs=checked, amf_clock_period_ns=float(config['ClockPeriod']),
            vivado_clock_source='Unmodified complete original VU095 DCP constraints; no clock overrides',
            original_clocks_tsv=str(bundle/'clocks.tsv'), original_clocks_sha256=digest(bundle/'clocks.tsv'),
            dcp_pair=pair, paper_case=name!='optimsoc',
            paper_scope_note='OptimSoC excluded by AMF2 paper due to CDC timing; supplemental run' if name=='optimsoc' else 'Included in AMF2 paper',
            synthesis_performed=False, core_extraction=False, source_initialization_repair=False,
            tool_version_limitation='Vivado 2024.2 available; paper used 2020.2/2021.2')
        provenance_path = batch/'inputs'/(name+'_provenance.json')
        save(provenance_path, provenance)
        items.append(dict(case=name, config=str(effective), provenance=str(provenance_path),
                          dcp=str(dcp), binary=str(binary), cells=case['cell_count']))
    save(batch/'catalog.json', items)
    return items


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('batch', type=Path)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--mode', choices=('amf','backend'), default='amf')
    args = parser.parse_args()
    batch = args.batch.resolve()
    output = batch/(args.mode+'_status.json')
    if output.exists():
        raise RuntimeError('Refusing duplicate batch stage: '+str(output))
    items = prepare(batch,args.build) if args.mode=='amf' else json.loads((batch/'catalog.json').read_text())
    state = dict(state='running', mode=args.mode, cases=[], concurrency=1)
    save(output,state)
    for item in items:
        if args.mode=='backend':
            state['waiting_for_case']=item['case']
            save(output,state)
            deadline=time.monotonic()+7200
            while True:
                amf_runs={x['case']:x for x in json.loads((batch/'amf_status.json').read_text())['cases']}
                prior=amf_runs.get(item['case'],{})
                audit=batch/'reports'/(item['case']+'-connectivity')/'comparison-canonical.json'
                if prior.get('state') in ('completed','failed') and audit.exists():break
                if time.monotonic()>deadline:raise RuntimeError('Timed out waiting for placement / connectivity audit')
                time.sleep(15)
            comparison=json.loads(audit.read_text())
            if prior['state']!='completed' or comparison['state']!='matched':
                state['cases'].append(dict(case=item['case'],state='skipped',reason='AMF or connectivity gate failed'))
                save(output,state)
                continue
            if comparison['dcp_binding']['input_sha256']!=digest(Path(item['dcp'])):
                raise RuntimeError('Connectivity audit DCP binding changed')
            config=json.loads(Path(item['config']).read_text())
            if comparison['input_netlist_sha256']!=digest(Path(config['vivado extracted design information file'])):
                raise RuntimeError('Connectivity audit AMF input binding changed')
            provenance=json.loads(Path(item['provenance']).read_text())
            provenance['canonical_connectivity_audit']=dict(path=str(audit),sha256=digest(audit),result=comparison)
            backend_provenance=batch/'inputs'/(item['case']+'_backend_provenance.json')
            save(backend_provenance,provenance)
            item['provenance']=str(backend_provenance)
            state.pop('waiting_for_case',None)
        command = ['python3','scripts/amf3.py','full-run','--run-prefix','amf2-original-'+item['case']+'-vu095-'+args.mode,
                   '--binary',item['binary'],'--config',item['config'],'--dcp',item['dcp'],'--input-provenance',item['provenance']]
        if args.mode=='amf':
            command += ['--amf-only']
        else:
            command += ['--placement-run',prior['run_directory'], '--upstream-backend', '--allow-import-repair']
        case = dict(case=item['case'],state='running',command=command,started=dt.datetime.now().astimezone().isoformat())
        state['cases'].append(case)
        save(output,state)
        log_path = batch/'logs'/(item['case']+'-'+args.mode+'.log')
        begin = time.monotonic()
        with log_path.open('w') as log:
            process = subprocess.Popen(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            case['pid'] = process.pid
            save(output,state)
            code = process.wait()
        run = next((s for s in log_path.read_text().splitlines() if s.startswith(str(ROOT/'experiments/runs'))),None)
        case.update(state='completed' if code==0 else 'failed',exit_code=code,run_directory=run,
                    elapsed_seconds=time.monotonic()-begin,finished=dt.datetime.now().astimezone().isoformat())
        save(output,state)
        print(json.dumps(case),flush=True)
    state['state'] = 'completed' if all(x['state']=='completed' for x in state['cases']) else 'failed'
    save(output,state)
    return int(state['state']!='completed')


if __name__=='__main__':
    sys.exit(main())
