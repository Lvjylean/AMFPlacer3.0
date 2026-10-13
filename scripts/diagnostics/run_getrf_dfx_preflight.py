#!/usr/bin/env python3
"""Run the GETRF RM synthesis probe in an exclusive, recorded directory."""
import datetime,hashlib,json,os,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    fixture=ROOT/'experiments/preflight/getrf-dfx-fixture-01'
    out=ROOT/'experiments/preflight/getrf-dfx-rm-synth-01'
    out.mkdir(exist_ok=False)
    t=time.monotonic()
    cmd=['/Projects/Xilinx26/2026.1/Vivado/bin/vivado','-mode','batch','-source',str(ROOT/'scripts/diagnostics/getrf_dfx_rm_preflight.tcl'),'-tclargs',str(fixture),str(out)]
    m=dict(state='running',pid=os.getpid(),started=datetime.datetime.now().astimezone().isoformat(),command=cmd,fixture_sha256=sha(fixture/'fixture.json'))
    def save():(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    save()
    with (out/'vivado.log').open('w') as f:
        rc=subprocess.call(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT)
    m.update(state='completed' if rc==0 else 'failed',exit_code=rc,wall_seconds=time.monotonic()-t)
    if rc==0:
        a=(out/'v0/cells.tsv').read_text();b=(out/'v1/cells.tsv').read_text()
        m['netlist_changed']=a!=b
        m['ports_identical']=(out/'v0/ports.tsv').read_bytes()==(out/'v1/ports.tsv').read_bytes()
        m['cells']={v:sum(1 for _ in (out/v/'cells.tsv').open())-1 for v in ('v0','v1')}
        m['outputs']={str(p.relative_to(out)):sha(p) for v in ('v0','v1') for p in [out/v/'rm.dcp',out/v/'cells.tsv',out/v/'ports.tsv']}
        if not m['netlist_changed'] or not m['ports_identical']:m.update(state='failed',error='RM must change with an identical interface');rc=2
    save();return rc
if __name__=='__main__':sys.exit(main())

