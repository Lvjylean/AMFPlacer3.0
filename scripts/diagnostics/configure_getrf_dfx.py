#!/usr/bin/env python3
"""Create fixed GETRF DFX comparison configs within this independent checkout."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def save(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    configs=ROOT/'configs/experiments'
    fixture=ROOT/'experiments/preflight/getrf-dfx-fixture-01'
    info=json.loads((fixture/'fixture.json').read_text())
    template=json.loads((configs/'dfx-u250-template.json').read_text())
    previous='/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261013-dfx-incremental/repo'
    template={k:(v.replace(previous,str(ROOT)) if isinstance(v,str) else v) for k,v in template.items()}
    template.update(jobs='12',ProPrimitiveTiming='false',BipartiteMatchingBackend='legacy_components')
    save(configs/'getrf-dfx-amf-template.json',template)
    common=dict(IncrementalCompilation=True,part='xcu250-figd2104-2L-e',top='getrf_dfx_top',
      clock_name='dfx_clk',clock_period_ns=10,vivado='/Projects/Xilinx26/2026.1/Vivado/bin/vivado',
      vivado_threads=4,max_parallel=2,require_distinct_slrs=True,
      amf_config='getrf-dfx-amf-template.json',assembly_xdc_files=[],
      shell_run='../../experiments/runs/getrf-dfx-v0-shell-01')
    common['partitions']=[dict(id=id,active_partition=rp,pblock='pblock_'+id,
       pblock_ranges=[f'CLOCKREGION_X2Y{y}:CLOCKREGION_X2Y{y}'],
       expected_slr=f'SLR{slr}',rm_checkpoint='../../experiments/preflight/getrf-dfx-rm-synth-01/v1/rm.dcp')
       for id,rp,y,slr in zip(('a','b'),info['rp_paths'],(1,5),(0,1))]
    for version in ('v0','v1'):
        d=fixture/version
        job=dict(common,rtl_sources=[str(p.relative_to(ROOT)) for p in sorted(d.glob('*.v'))],
          ip_scripts=[str(p.relative_to(ROOT)) for p in sorted(d.glob('*_ip.tcl'))],
          xdc_files=[str((d/'clock.xdc').relative_to(ROOT))],
          prepare_worker_shells=version=='v0')
        for k in ('rtl_sources','ip_scripts','xdc_files'):job[k]=['../../'+s for s in job[k]]
        save(configs/f'getrf-dfx-{version}-full.json',job)
        if version=='v1':
            job=json.loads(json.dumps(job))
            for rp in job['partitions']:
                rp.update(rm_rtl_sources=job['rtl_sources'],rm_ip_scripts=job['ip_scripts'],
                          rm_top='getrf_dfx_rm',rm_clock_port='ap_clk')
                rp.pop('rm_checkpoint',None)
            save(configs/'getrf-dfx-v1-incremental.json',job)
    print('Configured V0 shell, V1 full Vivado reference and V1 parallel AMF increment.')
if __name__=='__main__':main()
