#!/usr/bin/env python3
"""Create a first DFX configuration and locked shell from an RTL bblock marker."""
import argparse
import datetime as dt
from incremental_config import normalize_incremental_config
import json
import re
from pathlib import Path
import shutil
import subprocess
import time
from run_dfx import save, sha, tcl_word, SCRIPT


def run(args):
    path = Path(args.config).resolve()
    job = normalize_incremental_config(json.loads(path.read_text()))
    if job.get('IncrementalCompilation') not in (True,'true'): raise ValueError('Explicit DFX enable required')
    for key in ('rtl_sources','xdc_files','part','top','pblock','pblock_ranges','clock_name','clock_period_ns','vivado'):
        if key not in job: raise ValueError('Missing shell key: '+key)
    hashes = {}
    for key in ('rtl_sources','xdc_files'):
        if not isinstance(job[key], list) or not job[key]: raise ValueError('Expected nonempty path list: '+key)
        job[key] = [str((path.parent / v).resolve()) for v in job[key]]
        for v in job[key]: hashes[v] = sha(v)
    job['vivado'] = str((path.parent/job['vivado']).resolve())
    job.setdefault('vivado_threads',4)
    out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=False)
    settings=dict(job, output=str(out))
    # Preserve Tcl list structure with quoting at both the list-element and value levels.
    for k in ('rtl_sources','xdc_files'):
        if isinstance(settings[k],list):settings[k]=' '.join(tcl_word(x) for x in settings[k])
    ranges = settings['pblock_ranges']
    if not isinstance(ranges, list) or not ranges or any(not re.fullmatch(r'[A-Z0-9_]+_X[0-9]+Y[0-9]+:[A-Z0-9_]+_X[0-9]+Y[0-9]+', x) for x in ranges):
        raise ValueError('Expected explicit Vivado Pblock range tokens')
    settings['pblock_ranges'] = ' '.join(ranges)
    (out/'settings.tcl').write_text('\n'.join('set dfx('+k+') '+tcl_word(v) for k,v in settings.items())+'\n')
    for n in ('dfx_initialize.tcl','dfx_common.tcl'):shutil.copy2(SCRIPT/n,out/n)
    record=dict(schema='amf-dfx-shell-v1',job=job,input_hashes=hashes,amf_invoked=False,
                started=dt.datetime.now().astimezone().isoformat(),state='running',dcp_storage='server-only')
    save(out/'manifest.json',record);t=time.monotonic()
    try:
        command=[job['vivado'],'-mode','batch','-source',str(out/'dfx_initialize.tcl'),'-tclargs',str(out/'settings.tcl')]
        with (out/'console.log').open('w') as log:
            result=subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT)
        record['exit_code']=result.returncode
        if result.returncode:raise RuntimeError('DFX shell creation failed; see '+str(out/'console.log'))
        names=('base_routed.dcp','static_locked.dcp','base_full.bit','base_partial.bit')
        record['outputs']={n:sha(out/n) for n in names}
        record['state']='completed'
    except Exception as error:
        record['state']='failed';record['error']=str(error);raise
    finally:
        record['wall_seconds']=time.monotonic()-t
        save(out/'manifest.json',record);save(out/'status.json',{'state':record['state']})
    return out


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    print(run(p.parse_args()))
