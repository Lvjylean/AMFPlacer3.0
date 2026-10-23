"""Bounded fresh-shell pin-access certification, opt-in for full V0 capture.

The reusable input contains device topology only. Each generation reads the
current locked shell's route owners; previous design occupancy is never reused.
"""
import json
import sys
import time
from pathlib import Path
from run_dfx import sha, save

POLICY = 'fixed-static-ff-q-v1'
TOPOLOGY_FILES = ('inventory.tsv.gz', 'bels.tsv.gz', 'bel_pins.tsv.gz',
                  'site_pins.tsv.gz', 'external_graph.tsv.gz',
                  'node_attributes.tsv.gz', 'wanted_nodes.txt', 'identity.tsv')

def enabled(job):
    return job.get('native_pin_access_automatic', False) is True

def validate_request(job, capability):
    if not enabled(job): return
    if (not job.get('native_cache_capture') or not job.get('prepare_common_static_shell', True)
            or job.get('native_dfx_edge_policy') != 'clock-region-slice-dsp-v1'
            or capability.get('native_inline_pin_cache_schema') != 1
            or job.get('native_pin_access_policy') != POLICY):
        raise ValueError('Automatic pin cache requires full capture, empty shell, resource policy and capable binary')
    budget = float(job.get('native_pin_cache_budget_seconds', 180))
    if not 0 < budget <= 180: raise ValueError('Pin-cache budget must be positive and at most 180 seconds')
    if not Path(job['native_pin_access_topology']).is_file():
        raise ValueError('Missing certified device topology manifest')

def verify_topology(manifest, part, modules):
    manifest = Path(manifest)
    data = json.loads(manifest.read_text())
    if data.get('schema') != 'amf-pin-topology-v1' or data.get('part') != part:
        raise ValueError('Pin topology device/schema mismatch')
    if sorted(data['modules']) != sorted(modules): raise ValueError('Pin topology RP inventory mismatch')
    expected = set(TOPOLOGY_FILES) | {m+'_sites.txt' for m in modules}
    if set(data['files']) != expected: raise ValueError('Incomplete or design-dependent topology closure')
    root = Path(data['root'])
    for name, digest in data['files'].items():
        if sha(root/name) != digest: raise ValueError('Topology changed: '+name)
    return data

def prepare(job, out):
    if not enabled(job): return {}
    started = time.monotonic(); out = Path(out)
    data = verify_topology(job['native_pin_access_topology'], job['part'], [p['id'] for p in job['partitions']])
    data['source_manifest_sha256'] = sha(job['native_pin_access_topology'])
    data['preflight_seconds'] = time.monotonic()-started
    data['budget_seconds'] = float(job.get('native_pin_cache_budget_seconds', 180))
    save(out/'inputs/pin_topology.json', data)
    if data['preflight_seconds'] >= data['budget_seconds']: raise TimeoutError('Topology verification exceeded cache budget')
    return dict(native_pin_access_automatic=1, native_pin_topology_root=data['root'],
                native_pin_topology_vivado=data['vivado'], native_pin_cache_budget_seconds=data['budget_seconds'],
                native_pin_preflight_seconds=data['preflight_seconds'])

def remaining(out):
    """Count preflight plus all wall time from the in-session collector onward.

    This conservatively includes the baseline close/seal work between them.
    It excludes the already-required opening of the common static checkpoint.
    """
    out = Path(out); marker = out/'native_cache/pin_access/started_epoch.txt'
    if not marker.exists(): return None
    cfg = json.loads((out/'inputs/pin_topology.json').read_text())
    return cfg['budget_seconds']-cfg['preflight_seconds']-(time.time()-float(marker.read_text()))

def check_budget(out):
    left = remaining(out)
    if left is not None and left <= 0: raise TimeoutError('Automatic pin-cache 180-second budget exceeded')

def certificate(job, out, shell_sha, full_sha, cfg, stage):
    out = Path(out); pin = out/'native_cache/pin_access'
    if not (pin/'started_epoch.txt').is_file(): raise ValueError('Missing timed fresh-shell collection')
    check_budget(out)
    topology = json.loads((out/'inputs/pin_topology.json').read_text())
    # Verify again at use time: full placement may have taken hours.
    verified = verify_topology(out/'inputs/pin_topology.json', job['part'], [p['id'] for p in job['partitions']])
    observed = dict(line.split('\t', 1) for line in (pin/'identity.tsv').read_text().splitlines())
    if observed['part'] != topology['part'] or observed['vivado'] != topology['vivado']:
        raise ValueError('Opened static shell topology identity mismatch')
    if observed.get('complete') != '1': raise ValueError('Incomplete fresh occupancy collection')
    check_budget(out)
    stage('native_pin_access_classification', [sys.executable, str(out/'inputs/native_pin_access_classify.py'),
          '--topology', verified['root'], '--occupancy', str(pin/'occupied_nodes.tsv'),
          '--output', str(pin/'classification')])
    directory = pin/'classification'; classification = json.loads((directory/'classification.json').read_text())
    coverage = dict(classification, status='FULL_SCOPE_INVENTORY_AND_FIRST_HOP_ANALYSIS_COMPLETE',
                    source_sha_unchanged=True, source_dcp={'static_shell':{'sha256':shell_sha}},
                    topology_hashes=topology['files'], fresh_static_occupancy=True)
    save(directory/'coverage.json', coverage)
    proof = dict(static_shell_sha256=shell_sha, full_reference_sha256=full_sha,
                 static_routes_preserved_and_verified=True, collector=observed,
                 occupancy_sha256=sha(pin/'occupied_nodes.tsv'),
                 topology_source_manifest_sha256=topology['source_manifest_sha256'],
                 reused_design_occupancy=False,
                 scope='complete SLICE/DSP first-hop classification; hard FF Q exits only')
    save(directory/'provenance.json', proof)
    result = dict(identity=cfg['Native static identity'], device=cfg['device'],
                  clock_period=cfg['ClockPeriod'], static_shell_sha256=shell_sha,
                  policy=POLICY, ff_q_mask_file=str(directory/'ff_q_mask.tsv'))
    hashes = {}
    for key, filename in {'bel_pin_access':'bel_pin_access.tsv.gz', 'site_pin_access':'site_pin_access.tsv.gz',
                          'external_access':'external_access.tsv.gz', 'coverage':'coverage.json',
                          'provenance':'provenance.json'}.items():
        path = directory/filename; digest = sha(path)
        result[key+'_file'] = str(path); result[key+'_sha256'] = digest; hashes[key] = digest
    save(pin/'certificate.json', result); check_budget(out)
    policy = dict(version=POLICY, cache_format=4, all_classifications_embedded=True,
                  archive_sha256=hashes, hard_mask_rows=len(classification['hard_ff_rows']),
                  automatic_from_fresh_static_shell=True,
                  limitations='first-hop only; LUT/DSP partial, boundary and special access are evidence, not blanket BEL bans')
    return str(pin/'certificate.json'), policy

def finish(out, policy):
    from certify_native_pin_access import serialized_identity
    out = Path(out)
    meta, _ = serialized_identity(out/'native_cache/sealed.bin')
    for key, digest in policy['archive_sha256'].items():
        if meta.get('pin_archive/'+key) != digest: raise ValueError('Embedded pin archive differs: '+key)
    check_budget(out)
    cfg = json.loads((out/'inputs/pin_topology.json').read_text())
    seconds = cfg['budget_seconds']-remaining(out)
    result = dict(state='completed', budget_seconds=cfg['budget_seconds'], added_seconds_conservative=seconds,
                  preflight_seconds=cfg['preflight_seconds'], extra_dcp_open_seconds=0,
                  ordinary_seal_and_close_included=True, embedded_archives_readback_exact=True)
    save(out/'native_cache/pin_access/generation.json', result)
    return result
