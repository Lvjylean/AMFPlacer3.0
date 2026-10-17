#!/usr/bin/env python3
"""Prepare a new immutable deployment; never alter an existing run."""
import json, hashlib, shutil, time, tarfile
from pathlib import Path

ROOT = Path('/Projects/jinyang/workspace/AMFplacer3.0/experiments/preflight/20261014-getrf-static-containment/repo')
OTHER = Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261014-getrf-static-containment/repo')
REL = Path('experiments/deployments/full-amf-cr-20ns-11')
D = ROOT / REL
E = ROOT / 'experiments/evidence/static-containment-01/cr-20ns-01'
E.mkdir(exist_ok=False)
D.mkdir(exist_ok=False)
base = ROOT / 'experiments/deployments/full-amf-clock-region-10'
repair = ROOT / 'experiments/deployments/full-amf-static-membership-09'
incremental = ROOT / 'experiments/deployments/full-amf-incremental-site-import-06'
shutil.copytree(base/'scripts', D/'scripts')
(D/'configs').mkdir()
origins = {p.name: str(p) for p in (base/'scripts').iterdir() if p.is_file()}
for p in (repair/'scripts').iterdir():
    if p.is_file() and (not (D/'scripts'/p.name).exists() or p.name == 'dfx_full_backend.tcl'):
        shutil.copy2(p,D/'scripts'/p.name); origins[p.name]=str(p)
shutil.copy2(incremental/'scripts/dfx_backend.tcl',D/'scripts/dfx_backend.tcl')
origins['dfx_backend.tcl']=str(incremental/'scripts/dfx_backend.tcl')

def replace(name, old, new):
    p=D/'scripts'/name;s=p.read_text();assert s.count(old)==1,(name,old)
    p.write_text(s.replace(old,new))

replace('dfx_full_prepare.tcl',
 'source [file join [file dirname [info script]] dfx_static_containment.tcl]',
 'source [file join [file dirname [info script]] dfx_static_containment.tcl]\nsource [file join [file dirname [info script]] dfx_static_membership.tcl]')
replace('dfx_full_prepare.tcl',
 '        source [file join [file dirname [info script]] dfx_full_input.tcl]',
 '        amf_dfx::timed static_membership_recovered_opt {amf_static_membership::enforce recovered_opt 1}\n        source [file join [file dirname [info script]] dfx_full_input.tcl]')
replace('dfx_full_prepare.tcl',
 '        amf_dfx::timed reopen_synthesis {open_checkpoint $dfx(resume_synthesis_checkpoint)}',
 '        amf_dfx::timed reopen_synthesis {open_checkpoint $dfx(resume_synthesis_checkpoint)}\n'
 '        # Explicit user implementation XDC replaces the checkpoint clock.\n'
 '        # The usual context checks below verify its actual period.\n'
 '        amf_dfx::timed implementation_constraints {foreach f $dfx(xdc_files) {read_xdc $f}}')
replace('dfx_full_prepare.tcl',
 '    amf_dfx::timed opt {opt_design}',
 '    amf_dfx::timed opt {opt_design}\n    amf_dfx::timed static_membership_post_opt {amf_static_membership::enforce post_opt 1}')
replace('run_dfx.py',"'dfx_boundary_compatibility.tcl', 'run_dfx.py'", "'dfx_boundary_compatibility.tcl', 'dfx_site_import.tcl', 'run_dfx.py'")
replace('run_dfx_multi.py',"'dfx_backend.tcl','dfx_import.tcl','summarize_dfx.py'", "'dfx_backend.tcl','dfx_import.tcl','dfx_site_import.tcl','summarize_dfx.py'")

def save(path,j):path.write_text(json.dumps(j,indent=2)+'\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def template(source,name):
    c=json.loads(source.read_text());c.update(ClockPeriod='20',jobs='12',ProPrimitiveTiming='false')
    save(D/'configs'/name,c)

template(ROOT/'configs/experiments/getrf-full-amf-template-01.json','amf-072-20ns.json')
template(ROOT/'configs/experiments/dfx-u250-template.json','amf-small-20ns.json')
(D/'configs/clock-20ns.xdc').write_text('create_clock -name dfx_clk -period 20.000 [get_pins jtag/TCK]\n')
for kind,host in [('v0','072'),('v1','099'),('incremental','072')]:
    source=ROOT/f'configs/experiments/getrf-full-amf-cr-{kind}-{host}-01.json'
    j=json.loads(source.read_text());r=OTHER if host=='099' else ROOT
    j.update(clock_period_ns=20,amf_config=str(r/REL/f'configs/amf-{host}-20ns.json'),
             xdc_files=[str(r/REL/'configs/clock-20ns.xdc')])
    j['shell_run']=str((ROOT if kind=='incremental' else r)/'experiments/runs'/f'getrf-full-amf-cr-20ns-{("v0" if kind=="incremental" else kind)}-{host}-01')
    j.pop('prepare_from_full_run',None)
    j['performance_contract']['reference']='same V1 synthesis inputs, enlarged whole-CR hard regions, AMF binary/model/settings and explicit 20ns implementation clock'
    j['source_provenance']=dict(parent_config=str(source),clock_ns=20,
        geometry_preflight='experiments/preflight/static-clock-region-preflight-11',
        synthesis_reused=True,synthesis_original_clock_ns=10,
        implementation_constraints_reapplied=True,old_placement_or_shell_reused=False)
    # Archival front-end provenance remains in parent config; deployment needs
    # only the verified synthesis DCP and explicit implementation constraints.
    if kind=='v1':j.update(rtl_sources=[],ip_scripts=[])
    save(D/'configs'/f'getrf-{kind}-{host}-20ns.json',j)

for kind,old in [('v0','dfx-full-hard-regions-small-04.json'),('incremental','dfx-full-parent-incremental-small-01.json')]:
    j=json.loads((ROOT/'configs/experiments'/old).read_text())
    j.pop('prepare_from_full_run',None);j.pop('parent_run',None)
    j.update(clock_period_ns=20,static_region_granularity='clock_region',amf_config=str(D/'configs/amf-small-20ns.json'),
             xdc_files=[str(D/'configs/clock-20ns.xdc')],shell_run=str(ROOT/'experiments/runs/dfx-cr-20ns-small-v0-01'))
    for rp in j['partitions']:
        y=1 if rp['id']=='a' else 5
        rp['pblock_ranges']=[f'CLOCKREGION_X1Y{y}:CLOCKREGION_X3Y{y}']
    save(D/'configs'/f'small-{kind}-20ns.json',j)

save(E/'preparation.json',dict(created_epoch=time.time(),deployment=str(D),origins=origins,
     patches=['merge validated backend/import/membership fixes with CR geometry',
              'reload explicit XDC after synthesis recovery', 'snapshot site-import dependency for RM workers'],
     binary_unchanged=True,periodic_monitoring=False))
index=ROOT/'experiments/evidence/static-containment-01/launch.json'
j=json.loads(index.read_text())
for run in j['runs']:
    if str(run.get('run','')).endswith('backend-05') or str(run.get('base_run','')).endswith('backend-05'):
        run['current_state']='canceled_by_user_for_20ns_cr_experiment'
if j.get('pending_incremental'):
    j['pending_incremental'].update(state='canceled_by_user_before_launch',queue_stopped=True)
j['current_runs']={}
j['next_actions']=['Validate small 20ns whole-CR full/incremental pipeline, then launch GETRF full V0/V1 and dependent incremental V1 in new runs.']
j.setdefault('diagnostics',{})['cr_20ns_01']=dict(state='preparing_small_validation',evidence=str(E),deployment=str(D))
save(index,j)
print(json.dumps(dict(deployment=str(D),evidence=str(E)),indent=2))
