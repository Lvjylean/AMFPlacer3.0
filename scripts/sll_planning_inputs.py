"""Input identity and binary checks for the explicit E2 feature."""
import hashlib
import json
import subprocess
from pathlib import Path

def validate(config,binary):
    from placement_features import validate as validate_features, CHOICES
    validate_features(config)
    new_keys=set(CHOICES)-{'SllQuantityEnable','SllDynamicTauEnable','SllPlanningEnable','SllRegionPolicy'}
    if new_keys.intersection(config):
        caps=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True,timeout=15))
        if caps.get('placement_feature_switch_schema')!=1:
            raise ValueError('Binary lacks decoupled placement feature switches')
    if config.get('SllPlanningEnable','false')!='true':return
    if config.get('ProPrimitiveTiming','false')!='false':raise ValueError('E2 requires ordinary Pro')
    caps=json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True,timeout=15))
    for key in ('virtual_sll_resource_schema','paired_sll_timing_schema','receiver_group_fixed_route_schema'):
        if caps.get(key)!=1:raise ValueError('Binary lacks E2 capability '+key)
    if config.get('SllJointPlacementEnable','false')=='true' and caps.get('joint_sll_qp_density_schema')!=1:
        raise ValueError('Binary lacks joint SLL QP/density capability')
    if config.get('SllInitializationPolicy') is not None:
        if config['SllInitializationPolicy']!='local-wire-capacity' or caps.get('simple_sll_initialization_schema')!=1:
            raise ValueError('SLL initialization policy requires the matching simple-initialization binary')
    if config.get('SllProjectionPolicy') is not None:
        if config['SllProjectionPolicy']!='qp-nearest-capacity' or caps.get('qp_sll_projection_schema')!=1:
            raise ValueError('SLL projection policy requires the matching QP-projection binary')
    if config.get('SllQpDisplacementPolicy') is not None:
        if config['SllQpDisplacementPolicy']!='shared-pre-projection' or caps.get('bounded_sll_qp_projection_schema')!=1:
            raise ValueError('SLL displacement policy requires the matching bounded-projection binary')
    if config.get('SllTilePlanningEnable','false')=='true' and caps.get('tile_sll_global_schema')!=1:
        raise ValueError('Binary lacks tile-global SLL planning capability')

    if config.get('SllIncrementalScope'):
        if config['SllIncrementalScope']!='internal-active-nets' or caps.get('incremental_sll_planning_schema')!=1:
            raise ValueError('Binary lacks scoped incremental SLL planning')
        if not Path(config.get('SllReservedResourceFile','')).is_file():
            raise ValueError('Incremental SLL reservation file missing')
    if config.get('SllPlanningAllowTransientOverflow','false')=='true' and caps.get('transient_sll_capacity_schema')!=1:
        raise ValueError('Binary lacks transient SLL capacity with strict final legalization')
    table=Path(config['SllChannelFile'])
    with table.open() as f:header=[next(f).rstrip('\n') for _ in range(3)]
    if header[0]!='#part\t'+config['physical device part']:raise ValueError('SLL part mismatch')
    if header[1]!='#coordinate_sha256\t'+config['SllCoordinateMapSha256']:raise ValueError('SLL coordinate map mismatch')
    if config.get('SllChannelFileSha256'):
        sha=hashlib.sha256(table.read_bytes()).hexdigest()
        if sha!=config['SllChannelFileSha256']:raise ValueError('SLL channel file hash mismatch')
