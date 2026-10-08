"""Reject silently ignored paper-anchor configurations on older frozen binaries."""
import json
import subprocess

def validate_paper_boundary_binary(config, binary):
    strategy = config.get('BoundaryClusteringStrategy', 'region-gain')
    if strategy not in ('region-gain', 'paper-hierarchical'):
        raise ValueError('Unknown BoundaryClusteringStrategy: ' + strategy)
    if strategy != 'paper-hierarchical':
        return
    if config.get('BoundaryAwareClustering') != 'true' or config.get('PhysicalBoundaryMode') != 'true':
        raise ValueError('Paper boundary anchors require physical mode and boundary clustering')
    result = subprocess.run([str(binary), '--capabilities'], capture_output=True, text=True, timeout=15)
    try:
        supported = result.returncode == 0 and json.loads(result.stdout).get('paper_hierarchical_boundaries_schema') == 2
    except (ValueError, AttributeError):
        supported = False
    if not supported:
        raise ValueError('Binary does not support paper hierarchical boundaries; refusing silent fallback')
