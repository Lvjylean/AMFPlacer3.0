"""Explicit immutable recovery of one fully imported, unrouted native module."""
import json
from pathlib import Path
from run_dfx import amf_configuration, sha
from parallel_runtime import verify_hashes


def verify_completed_module(job, part, frontend, scripts):
    recovery = part['reuse_completed_module']
    parent = Path(recovery['module']).resolve()
    branch = Path(frontend['parent_branch']).resolve()
    if parent != branch / 'module':
        raise ValueError('Completed module and frontend have different parents')
    previous = json.loads((branch / 'manifest.json').read_text())
    module = json.loads((parent / 'manifest.json').read_text())
    if previous['state'] != 'completed' or module['state'] != 'amf_placed':
        raise ValueError('Completed module recovery requires successful import')
    if previous['module_manifest_sha256'] != sha(parent / 'manifest.json'):
        raise ValueError('Completed module manifest changed')
    if module['amf_import_acceptance']['amf_loc_bel_exact'] is not True:
        raise ValueError('Completed module has no exact coordinate acceptance')
    if module['static_dcp_opened'] or module['place_design_executed']:
        raise ValueError('Completed module violates native placement-only contract')
    expected_job = dict(job, module_id=part['id'], active_partition=part['active_partition'],
                        pblock=part['pblock'], rm_clock_port=part['rm_clock_port'])
    for key in ('module_id', 'active_partition', 'pblock', 'part', 'clock_name',
                'clock_period_ns', 'vivado', 'vivado_threads', 'native_static_cache',
                'native_dfx_edge_policy', 'native_pin_access_policy',
                'native_local_pin_access_policy', 'clock_handoff_mode', 'rm_clock_port'):
        if module['job'].get(key) != expected_job.get(key):
            raise ValueError('Completed module configuration mismatch: ' + key)
    for key, value in [('static_checkpoint', Path(job['common_static_shell_run']) / 'common_static_locked.dcp'),
                       ('reference_checkpoint', Path(job['shell_run']) / 'base_routed.dcp')]:
        if Path(module['job'][key]).resolve() != value.resolve():
            raise ValueError('Completed module static parent mismatch: ' + key)
    if amf_configuration(module['job']) != amf_configuration(job):
        raise ValueError('Completed module AMF parameters changed')
    if sha(module['job']['rm_checkpoint']) != frontend['optimization']['output_sha256']:
        raise ValueError('Completed module optimized input mismatch')
    # Every sourced Tcl dependency remains identical; only scheduler/solver code changes.
    for name, digest in module['script_hashes'].items():
        if name.endswith('.tcl') and sha(Path(scripts) / name) != digest:
            raise ValueError('Completed module Tcl dependency changed: ' + name)
    names = ('manifest.json', 'rm_amf_placed.dcp', 'imported.dcp', 'placement.tsv',
             'blocked_sites.txt', 'allowed_sites.txt', 'cells.tsv',
             'interface_buffers.tsv', 'lut_pin_assignments.tsv')
    required = {str(parent / name) for name in names} | {str(branch / 'manifest.json')}
    if set(recovery['hashes']) != required:
        raise ValueError('Incomplete completed-module recovery hash coverage')
    verify_hashes(module['input_hashes'])
    verify_hashes(recovery['hashes'])
    for name in ('rm_amf_placed.dcp', 'imported.dcp'):
        if sha(parent / name) != module['outputs'][name]['sha256']:
            raise ValueError('Completed module DCP changed')
    if sha(parent / 'lut_pin_assignments.tsv') != module['lut_pin_plan_sha256']:
        raise ValueError('Completed module LUT pin plan changed')
    if module['binary_sha256'] != recovery['binary_sha256']:
        raise ValueError('Completed module binary provenance mismatch')
    return dict(module=str(parent), module_manifest_sha256=sha(parent / 'manifest.json'),
                module_dcp_sha256=module['outputs']['rm_amf_placed.dcp']['sha256'],
                cells=module['amf_import_acceptance']['cells'],
                binary_sha256=module['binary_sha256'], hashes=recovery['hashes'],
                inherited_branch_seconds=previous['seconds'],
                inherited_module_seconds=module['total_wall_seconds'],
                inherited_stages=module['stages'])
