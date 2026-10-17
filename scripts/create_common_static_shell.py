"""Optional one-time derivation from an accepted full reference; never reruns AMF/place/route."""
import argparse
import json
import shutil
import time
from pathlib import Path
from run_dfx import sha, save
from run_dfx_multi import load, settings, vivado
from common_static_contract import record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    p.add_argument('--output', required=True)
    args = p.parse_args()
    job = load(args.config)
    source = Path(job['shell_run'])
    parent = json.loads((source/'manifest.json').read_text())
    if parent['state'] != 'completed' or not parent['acceptance']['implementation_verified']:
        raise ValueError('Full reference has not passed implementation acceptance')
    reference = source/'base_routed.dcp'
    if sha(reference) != parent['outputs']['base_routed.dcp']:
        raise ValueError('Full reference DCP changed')
    from common_static_contract import signature
    if signature(job) != signature(parent['job']):
        raise ValueError('Full reference conditions do not match requested static shell')
    out = Path(args.output).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out/'inputs').mkdir();(out/'logs').mkdir()
    scripts = Path(__file__).resolve().parent
    names = ('dfx_export_common_static.tcl', 'dfx_common_static_shell.tcl',
             'dfx_multi_common.tcl', 'dfx_common.tcl', 'dfx_boundary_compatibility.tcl')
    for name in names: shutil.copy2(scripts/name, out/'inputs'/name)
    state = dict(schema='amf-common-static-derivation-v1', state='running',
                 source=str(source), parent_manifest_sha256=sha(source/'manifest.json'),
                 source_reference_sha256=sha(reference), job=job,
                 script_hashes={n:sha(out/'inputs'/n) for n in names},
                 amf_rerun=False, place_or_route_rerun=False, dcp_storage='server-only')
    started = time.monotonic()
    save(out/'manifest.json', state)
    try:
        config = settings(job, out, dict(reference_checkpoint=str(reference)))
        state['stages'] = [vivado(job, out, 'dfx_export_common_static.tcl', config)]
        if state['stages'][0]['exit_code']:
            raise RuntimeError('Common static derivation failed; preserve this output')
        state['common_static_shell'] = record(job, out, reference)
        state.update(state='completed', contract_sha256=sha(out/'common_static_shell.json'))
    except Exception as error:
        state.update(state='failed', error=str(error))
        raise
    finally:
        state['wall_seconds'] = time.monotonic()-started
        save(out/'manifest.json', state)


if __name__ == '__main__':main()
