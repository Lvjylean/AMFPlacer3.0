#!/usr/bin/env python3
"""Immutable, read-only Vivado adapter validation; checkpoints stay on host."""
import argparse
import json
import os
from pathlib import Path
import shutil
import time
from parallel_runtime import command
from run_dfx import save, sha, tcl_word

def run(config, output):
    cfg=json.loads(Path(config).read_text())
    out=Path(output).resolve();out.mkdir(parents=True, exist_ok=False)
    frozen=out/'inputs';frozen.mkdir()
    for p in Path(__file__).parent.glob('*.tcl'):shutil.copy2(p,frozen/p.name)
    source=Path(cfg['checkpoint']).resolve()
    settings=dict(cfg.get('settings',{}),output=str(out),rm_checkpoint=str(source),vivado_threads=4)
    (frozen/'settings.tcl').write_text(''.join('set dfx('+k+') '+tcl_word(v)+'\n' for k,v in settings.items()))
    m=dict(state='running',runner_pid=os.getpid(),started_epoch=time.time(),source=str(source),
           source_sha256_before=sha(source),settings=settings,config=cfg,
           script_hashes={p.name:sha(p) for p in frozen.iterdir() if p.is_file()},
           scope='read-only checkpoint; metadata export only; no place, route, or checkpoint write')
    save(out/'manifest.json',m)
    try:
        m['process']=command([cfg['vivado'],'-mode','batch','-source',str(frozen/cfg['script']),
                              '-tclargs',str(frozen/'settings.tcl')],out/'exec',timeout=cfg.get('timeout_seconds',900))
        m['state']='completed'
    except Exception as e:m.update(state='failed',error=str(e))
    finally:
        m['source_sha256_after']=sha(source)
        m['source_unchanged']=m['source_sha256_before']==m['source_sha256_after']
        if not m['source_unchanged']:m.update(state='failed',error='Source checkpoint changed')
        m['finished_epoch']=time.time();m['wall_seconds']=m['finished_epoch']-m['started_epoch']
        m['outputs']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'}
        save(out/'manifest.json',m)
    return m['state']=='completed'
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--config',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();raise SystemExit(0 if run(a.config,a.output) else 1)
