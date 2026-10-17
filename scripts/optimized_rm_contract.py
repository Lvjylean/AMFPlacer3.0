"""Require an actual successful default opt, its interface proof and immutable DCP."""
import json
from pathlib import Path
from run_dfx import sha


def verify_optimized(manifest, job):
    path=Path(manifest).resolve(); data=json.loads(path.read_text())
    if data.get('state')!='completed' or data.get('process',{}).get('exit_code')!=0:
        raise ValueError('Standalone RM opt did not complete')
    for key in ('part','clock_period_ns','vivado_threads'):
        if data.get(key)!=job.get(key):raise ValueError('RM opt contract mismatch: '+key)
    command=data['process']['command']
    if command[0]!=job['vivado']:raise ValueError('RM opt tool mismatch')
    for prefix in ('input','output'):
        if sha(data[prefix])!=data[prefix+'_sha256']:raise ValueError('RM opt artifact changed: '+prefix)
    rows=[r.split('\t') for r in (path.parent/'stage_times.tsv').read_text().splitlines()]
    for stage in ('open_rm_synthesis','rm_ooc_opt','write_optimized_rm'):
        found=[r for r in rows if r[0]==stage]
        if len(found)!=1 or found[0][2]!='0':raise ValueError('Missing successful RM opt stage: '+stage)
    values=dict(r.split('\t') for r in (path.parent/'validation.tsv').read_text().splitlines())
    if (values.get('interface_exact')!='1' or values.get('unresolved_blackboxes')!='0'
            or values.get('opt_executed')!='1' or int(values.get('port_bits','0'))<1
            or float(values.get('clock_period_ns','0'))!=float(job['clock_period_ns'])):
        raise ValueError('Incomplete RM opt validation')
    if (path.parent/'ports_before.tcldata').read_bytes()!=(path.parent/'ports_after.tcldata').read_bytes():
        raise ValueError('RM optimization changed interface')
    script=Path(command[command.index('-source')+1])
    # The inherited producer differs only by its post-DCP utilization report.
    accepted={sha(Path(__file__).parent/'dfx_rm_opt.tcl'),
              'e3328d929c2a66a7f33dfef548d26f50d29b2d727e29c4cec7cf786a38e9a9b9'}
    if sha(script) not in accepted:
        raise ValueError('Unknown standalone optimization script')
    return dict(data,manifest_sha256=sha(path))
