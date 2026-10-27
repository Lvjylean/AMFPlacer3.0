"""Input identity and binary checks for the explicit E2 feature."""
import hashlib
import json
import subprocess
from pathlib import Path

def validate(config,binary):
    if config.get('SllPlanningEnable','false')!='true':return
    if config.get('ProPrimitiveTiming','false')!='false':raise ValueError('E2 requires ordinary Pro')
    caps=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True,timeout=15))
    for key in ('virtual_sll_resource_schema','paired_sll_timing_schema','receiver_group_fixed_route_schema'):
        if caps.get(key)!=1:raise ValueError('Binary lacks E2 capability '+key)
    if config.get('SllJointPlacementEnable','false')=='true' and caps.get('joint_sll_qp_density_schema')!=1:
        raise ValueError('Binary lacks joint SLL QP/density capability')
    table=Path(config['SllChannelFile'])
    with table.open() as f:header=[next(f).rstrip('\n') for _ in range(3)]
    if header[0]!='#part\t'+config['physical device part']:raise ValueError('SLL part mismatch')
    if header[1]!='#coordinate_sha256\t'+config['SllCoordinateMapSha256']:raise ValueError('SLL coordinate map mismatch')
    if config.get('SllChannelFileSha256'):
        sha=hashlib.sha256(table.read_bytes()).hexdigest()
        if sha!=config['SllChannelFileSha256']:raise ValueError('SLL channel file hash mismatch')
