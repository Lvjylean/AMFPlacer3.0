"""Validate the canonical SLR hierarchy and explicit legacy compatibility."""
import json
import subprocess
from device_placement_config import validate_device_placement_binary

def boundary_clustering_strategy(config):
    strategy = config.get('BoundaryClusteringStrategy', 'slr-hierarchical')
    if strategy == 'paper-hierarchical':
        return 'slr-hierarchical'
    if strategy not in ('slr-hierarchical', 'cr-slr-hierarchical', 'region-gain'):
        raise ValueError('Unknown BoundaryClusteringStrategy: ' + str(strategy))
    return strategy

def validate_paper_boundary_binary(config, binary):
    validate_device_placement_binary(config, binary)
    # Retain the Python function name for existing import sites.
    stage_mode = config.get('BoundaryStageMode', 'independent-max2')
    if stage_mode not in ('independent-max2', 'merged'):
        raise ValueError('BoundaryStageMode must be independent-max2 or merged')
    strategy = boundary_clustering_strategy(config)
    if 'BoundaryRegionClipping' in config and config['BoundaryRegionClipping'] not in ('true', 'false'):
        raise ValueError('BoundaryRegionClipping must be true or false')
    if config.get('BoundaryAwareClustering') != 'true' or strategy == 'region-gain':
        return
    if config.get('PhysicalBoundaryMode') != 'true':
        raise ValueError('SLR hierarchical clustering requires PhysicalBoundaryMode=true')
    result = subprocess.run([str(binary), '--capabilities'], capture_output=True, text=True, timeout=15)
    try:
        cap = json.loads(result.stdout)
        supported = result.returncode == 0 and cap.get('paper_hierarchical_boundaries_schema') == 2
    except (ValueError, AttributeError):
        cap, supported = {}, False
    if not supported:
        raise ValueError('Binary does not support SLR hierarchical boundaries; refusing silent fallback')
    if strategy == 'cr-slr-hierarchical' and cap.get('cr_slr_hierarchical_schema') != 1:
        raise ValueError('Binary does not support cr-slr-hierarchical; refusing silent fallback')

    # Old binaries understand only the explicit old spelling, not the new
    # spelling or the changed behavior when the strategy key is omitted.
    if config.get('BoundaryClusteringStrategy') != 'paper-hierarchical':
        if cap.get('boundary_clustering_strategy_schema') != 1 or cap.get('boundary_clustering_strategy_default') != 'slr-hierarchical':
            raise ValueError('Binary does not support the slr-hierarchical name/default; refusing silent fallback')
    if 'BoundaryStageMode' in config and cap.get('paper_boundary_stage_mode_schema') != 1:
        raise ValueError('Binary does not support BoundaryStageMode; refusing silent fallback')

    if 'BoundaryRegionClipping' in config:
        if config['BoundaryRegionClipping'] not in ('true', 'false'):
            raise ValueError('BoundaryRegionClipping must be true or false')
        if cap.get('paper_region_clipping_toggle_schema') != 1:
            raise ValueError('Binary does not support the isolated region-clipping toggle')
