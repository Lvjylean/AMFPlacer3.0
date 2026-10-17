"""Exact prepared-input reuse after the recorded CARRY non-progress failure."""
import json, shutil, subprocess
from pathlib import Path
from run_dfx import sha, save, amf_configuration
FILES=['base_synth.dcp','base_opt.dcp','netlist.txt','netlist.zip','cells.tsv','clocks',
 'hard_regions.tsv','hard_region_sites.tsv','hard_region_sites.before_policy.tsv','full_edge_policy_audit.tsv',
 'srl_controls.tsv','dsp_registered_outputs.txt','resources.tsv','resource_regions.tsv','regions.tsv',
 'full_input_coverage.tsv','export_profile.tsv','infrastructure.tsv','static_routing_policy.tsv',
 'post_opt_static_membership_summary.tsv','dfx_preflight_drc.rpt']

def recover(job,cfg,out):
 spec=job['recover_exact_preparation'];parent=Path(spec['parent']);mp=parent/'manifest.json'
 if sha(mp)!=spec['manifest_sha256']:raise ValueError('Preparation parent changed')
 m=json.loads(mp.read_text());old=m['job'];stop=json.loads(Path(spec['stop_evidence']).read_text())
 if sha(Path(spec['stop_evidence']))!=spec['stop_evidence_sha256']:raise ValueError('Stop evidence changed')
 if stop['parent_manifest_sha256']!=spec['manifest_sha256']:raise ValueError('Wrong stopped parent')
 if m['state']!='failed' or m['error']!='amf failed; preserve this run':raise ValueError('Unexpected recovery parent state')
 stages={s['name']:s for s in m['stages']}
 if stages['prepare']['exit_code']!=0 or stages['full_edge_policy']['exit_code']!=0:raise ValueError('Incomplete preparation')
 if stages['amf']['exit_code']!=-15:raise ValueError('Unexpected stopped AMF status')
 if sha(job['amf_binary'])!=job['amf_binary_sha256']:raise ValueError('Candidate binary changed')
 permitted={'amf_binary','amf_binary_sha256','shell_run','recover_exact_preparation'}
 if {k:v for k,v in job.items() if k not in permitted}!={k:v for k,v in old.items() if k not in permitted}:
  raise ValueError('Recovery design/constraints/settings changed')
 if cfg!=amf_configuration(old):raise ValueError('AMF template changed')
 for group in ['rtl_sources','ip_scripts','xdc_files','assembly_xdc_files']:
  for name in job.get(group,[]):
   if sha(name)!=m['source_inputs'][name]['sha256']:raise ValueError('Frozen source changed')
 original_cfg=json.loads((parent/'amf_config.json').read_text())
 for key,digest in m['input_hashes'].items():
  if sha(original_cfg[key])!=digest:raise ValueError('Original AMF input/model changed: '+key)
 proof={}
 for name in FILES:
  source=parent/name;before=sha(source)
  subprocess.run(['cp','--reflink=auto','--',str(source),str(out/name)],check=True)
  if sha(out/name)!=before or sha(source)!=before:raise ValueError('Recovery copy changed: '+name)
  proof[name]={'sha256':before,'bytes':source.stat().st_size}
 shutil.copy2(parent/'stage_times.tsv',out/'inherited_stage_times.tsv')
 shutil.copy2(parent/'stage_intervals.tsv',out/'inherited_stage_intervals.tsv')
 result=dict(parent=str(parent),parent_manifest_sha256=spec['manifest_sha256'],files=proof,
  parent_wall_seconds=m['wall_seconds'],inherited_prepare_seconds=stages['prepare']['seconds'],
  inherited_failed_amf_seconds=stages['amf']['seconds'],synthesis_repeated=False,
  optimization_repeated=False,input_export_repeated=False,recovery_not_cold_start=True,
  old_binary_sha256=m['binary_sha256'],new_binary_sha256=job['amf_binary_sha256'])
 save(out/'preparation_recovery.json',result);return result
