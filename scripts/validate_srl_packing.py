"""Vivado round-trip of SRL/MUX BEL maps, independent of global placement."""
import json
from pathlib import Path
import shutil
import subprocess
import time
from inspect_amf_inputs import digest


def validate(root,args):
    from amf3 import git,machine,save,stamp
    source=(root/args.packing_run).resolve()
    if json.loads((source/'status.json').read_text())['state']!='completed':raise ValueError('Packing run incomplete')
    run=root/'experiments/runs'/('getrf-u250-srl-audit-'+stamp())
    for name in ('inputs','reports','logs'):(run/name).mkdir(parents=True,exist_ok=False)
    shutil.copy2(source/'reports/packing.tsv',run/'inputs/packing.tsv')
    script=run/'inputs/validate_srl_packing.tcl';shutil.copy2(root/'scripts/validate_srl_packing.tcl',script)
    dcp=(root/args.dcp).resolve()
    command=[machine()['vivado'],'-mode','batch','-notrace','-nojournal','-log',str(run/'logs/vivado.log'),'-source',str(script),'-tclargs',str(dcp),str(run/'inputs/packing.tsv'),str(run/'reports')]
    save(run/'manifest.json',dict(source_commit=git('rev-parse','HEAD'),git_status=git('status','--porcelain'),packing_run=str(source),
        source_manifest_sha256=digest(source/'manifest.json'),dcp=str(dcp),dcp_sha256=digest(dcp),command=command,
        inputs_sha256={p.name:digest(p) for p in (run/'inputs').iterdir()}))
    save(run/'status.json',dict(state='running'));print(run,flush=True);start=time.monotonic()
    with (run/'logs/console.log').open('w') as log:result=subprocess.run(command,cwd=run,stdout=log,stderr=subprocess.STDOUT)
    save(run/'status.json',dict(state='completed' if result.returncode==0 else 'failed',exit_code=result.returncode,elapsed_seconds=time.monotonic()-start))
    if result.returncode:raise RuntimeError('SRL physical audit failed: '+str(run))
