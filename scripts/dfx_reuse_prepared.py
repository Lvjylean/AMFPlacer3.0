"""Reuse completed immutable inputs; regenerate only SRL semantics from the opt DCP."""
import json
from pathlib import Path
import shutil
from run_dfx import sha
from run_dfx_multi import static_containment_enabled


def reuse(job, cfg, out, manifest):
    parent = Path(job['reuse_prepared_input_run']).resolve()
    old = json.loads((parent / 'manifest.json').read_text())
    prepare = [s for s in old.get('stages', []) if s['name'] == 'prepare' and s['exit_code'] == 0]
    if not prepare or old.get('amf_placement_scope') != 'whole_design':
        raise ValueError('Parent has no completed full preparation')
    if static_containment_enabled(job) or static_containment_enabled(old['job'], historical_default=True):
        raise ValueError('Prepared-input reuse is restricted to uncontained static routing')
    for key in ('part', 'top', 'clock_name', 'clock_period_ns', 'synthesis_sha256'):
        if job.get(key) != old['job'].get(key):
            raise ValueError('Prepared-input parent mismatch: ' + key)
    keys = ('id', 'active_partition', 'pblock', 'pblock_ranges', 'expected_slr')
    if [{k:r.get(k) for k in keys} for r in job['partitions']] != [{k:r.get(k) for k in keys} for r in old['job']['partitions']]:
        raise ValueError('Prepared-input RP geometry mismatch')
    for key in ('rtl_sources', 'ip_scripts', 'xdc_files', 'assembly_xdc_files'):
        if [sha(p) for p in job.get(key, []) if Path(p).is_file()] != [sha(p) for p in old['job'].get(key, []) if Path(p).is_file()]:
            raise ValueError('Prepared-input source/constraint mismatch: ' + key)
    if old['binary_sha256'] != manifest['binary_sha256']:
        raise ValueError('AMF binary changed')
    old_cfg = json.loads((parent / 'amf_config.json').read_text())
    for key, value in cfg.items():
        if key in ('dumpDirectory', 'BoundaryReportDirectory', 'vivado extracted design information file', 'clock file'):
            continue
        if 'file' in key or key == 'mergedSharedCellType2sharedCellType':
            if sha(value) != sha(old_cfg[key]):
                raise ValueError('AMF model mismatch: ' + key)
        elif str(value) != str(old_cfg.get(key)):
            raise ValueError('AMF parameter mismatch: ' + key)
    if sha(parent / 'modeled_sites.txt') != sha(out / 'modeled_sites.txt'):
        raise ValueError('Modeled site inventory changed')
    if not any(row.split('\t')[0] == 'write_opt_dcp' and row.split('\t')[2] == '0' for row in (parent / 'stage_times.tsv').read_text().splitlines()):
        raise ValueError('Incomplete opt checkpoint')
    files = ('base_opt.dcp', 'netlist.zip', 'cells.tsv', 'hard_regions.tsv', 'hard_region_sites.tsv', 'clocks', 'clockInfo.txt',
             'full_input_coverage.tsv', 'infrastructure.tsv', 'dedicated_interface_pins.tsv', 'dsp_registered_outputs.txt', 'static_routing_policy.tsv')
    provenance = {'parent':str(parent), 'parent_manifest_sha256':sha(parent / 'manifest.json'), 'files':{},
                  'original_prepare_seconds':prepare[-1]['seconds'], 'preparation_reused':True,
                  'placement_reused':False, 'routing_reused':False, 'mutable_parent_backend_allowed':True}
    expected = {Path(old_cfg[k]).name:v for k,v in old.get('input_hashes', {}).items() if k in old_cfg}
    for name in files + ('srl_controls.tsv',):
        source = parent / name
        before = sha(source)
        if name in expected and before != expected[name]:
            raise ValueError('Frozen parent input hash mismatch: ' + name)
        target = out / ('srl_controls.before.tsv' if name == 'srl_controls.tsv' else name)
        shutil.copy2(source, target)
        if sha(target) != before or sha(source) != before:
            raise ValueError('Input changed during copy: ' + name)
        provenance['files'][name] = before
    shutil.copy2(parent / 'stage_times.tsv', out / 'source_stage_times.tsv')
    manifest['prepared_input_reuse'] = provenance
    manifest['stages'].append({'name':'prepare', 'exit_code':0, 'seconds':0.0, 'reused':True,
                               'source_run':str(parent), 'source_seconds':prepare[-1]['seconds']})
    return provenance


def verify_srl_refresh(out):
    import csv
    before = list(csv.reader((out / 'srl_controls.before.tsv').open(), delimiter='\t'))
    after = list(csv.reader((out / 'srl_controls.tsv').open(), delimiter='\t'))
    if len(before) != len(after) or before[0] != after[0]:
        raise ValueError('SRL metadata coverage changed')
    changed = 0
    for old, new in zip(before[1:], after[1:]):
        if len(old) != 8 or len(new) != 8 or old[:4] + old[5:] != new[:4] + new[5:]:
            raise ValueError('Non-inversion SRL metadata changed')
        changed += old[4] != new[4]
    summary = dict(line.rstrip('\n').split('\t') for line in (out / 'srl_refresh_summary.tsv').open())
    if int(summary['rows']) != len(after)-1 or int(summary['changed_rows']) != changed:
        raise ValueError('SRL refresh summary mismatch')
    if len(after) > 1 and int(summary['unknown_rows']) == len(after)-1:
        raise ValueError('All SRL attributes remain UNKNOWN; stop before AMF')
    return summary
