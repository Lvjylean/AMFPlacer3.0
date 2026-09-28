#!/usr/bin/env python3
"""One-time, reviewable instrumentation of AMF functional boundaries.

Tiny per-cell/per-pin accessors remain charged to their measured caller.
The generated inventory records every selected code location.
"""
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / 'src/lib/HiFPlacer'


def main():
    inventory = []
    defaults = {
        'ClusterPlacer': 'clustering', 'GlobalPlacer': 'global_orchestration',
        'GeneralSpreader': 'spreading', 'WirelengthOptimizer': 'qp_model',
        'InitialPacker': 'initial_packing', 'IncrementalBELPacker': 'incremental_packing',
        'ParallelCLBPacker': 'final_packing', 'CLBLegalizer': 'clb_legalization',
        'MacroLegalizer': 'macro_legalization', 'PlacementInfo': 'placement_bookkeeping',
        'PlacementTimingInfo': 'timing', 'PlacementTimingOptimizer': 'timing',
        'BoundaryAwareClusterer': 'boundary_clustering', 'RegionCapacityTracker': 'boundary_capacity',
        'GraphPartitioner': 'partition', 'SAPlacer': 'sa',
        'QPSolverWrapper': 'qp_solver', 'MinCostBipartiteMatcher': 'bipartite_matching',
        'DeviceInfo': 'input_device', 'DesignInfo': 'input_netlist',
        'PhysicalBoundaryModel': 'input_device',
    }
    only = {
        'DeviceInfo': {'DeviceInfo', 'mapClockRegionToArray', 'mapSiteToClockColumns', 'loadPCIEPinOffset', 'loadBELType2FalseBELType', 'printStat'},
        'DesignInfo': {'DesignInfo', 'loadClocks', 'updateFFControlSets', 'enhanceFFControlSetNets', 'loadUserDefinedClusterNets', 'printStat'},
        'PhysicalBoundaryModel': {'PhysicalBoundaryModel'},
        'RegionCapacityTracker': {'RegionCapacityTracker', 'assign'},
    }
    skip = {
        'addSiteIntoBin', 'addFF', 'addLUT', 'addToFFSet', 'addPU', 'checkCellCorrectness',
        'maxCardinalityMatching', 'compatibleInOneHalfCLB', 'checkNumMuxCompatibleInFFSet',
        'addFFGroup', 'addPUFailReason', 'getInternalPinsNum', 'updateScoreInSite',
        'incrementalUpdateScoreInSite', 'getMinXFromSites', 'getMinYFromSites',
        'getMaxXFromSites', 'getMaxYFromSites', 'getPUSiteNum', 'getMarcroCellNum',
        'findIdMaxWithRecurence', 'findCorrespondingColumn', 'legalSiteRange',
        'boundaryClusteringEnabled', 'regionTarget', 'getWorstSlackOfCell',
        'probabilituFunc', 'findMuxFromHalfCLB', 'prePackLegalizedMacros',
        'checkCompatibleFFs', 'mapCarryRelatedRouteThru', 'BFSExpandViaSpecifiedPorts',
        'drawNet', 'printMyself', 'isLUTsPackable',
    }
    for path in sorted(SOURCES.rglob('*.cc')):
        base = path.stem.split('_')[0]
        if base not in defaults or path.stem.endswith('PackingCLBCluster'):
            continue
        text = path.read_text()
        if 'AMF_PROFILE_FUNCTION' in text:
            raise RuntimeError('Already instrumented: ' + str(path))
        pattern = re.compile(r'^(?!\s*//)[^\n;{}]*?\b(' + base + r'(?:<[^>\n]+>)?(?:::\w+(?:<[^>\n]+>)?)*::(~?\w+))\s*\(', re.M)
        inserts = []
        for match in pattern.finditer(text):
            name, method = match[1], match[2]
            if method in skip or (base in only and method not in only[base]):
                continue
            depth, end = 1, match.end()
            while depth:
                if text[end] == '(': depth += 1
                if text[end] == ')': depth -= 1
                end += 1
            brace = text.find('{', end)
            if brace < 0 or ';' in text[end:brace]:
                continue
            category = defaults[base]
            lower = method.lower()
            if lower.startswith(('dump', 'printstat', 'draw')): category = 'diagnostic_output'
            if 'placementtcl' in lower: category = 'export_vivado'
            if 'timingdrivendetailed' in lower: category = 'detailed_placement'
            if base == 'PlacementInfo' and lower.startswith('adjust'): category = 'density_update'
            if method in ('checkClockUtilization', 'verifyDeviceForDesign', 'verifyAvailableCapacity'): category = 'validation'
            if method == 'auditPhysicalBoundaries' or (base == 'BoundaryAwareClusterer' and method == 'audit'): category = 'diagnostic_output'
            if method == 'clusterCriticalPathsByPhysicalRegion': category = 'boundary_clustering'
            inserts.append((brace+1, '\n    AMF_PROFILE_FUNCTION("' + category + '");'))
            inventory.append(dict(file=str(path.relative_to(ROOT)), function=name, category=category))
        if inserts:
            for pos, value in reversed(inserts): text = text[:pos] + value + text[pos:]
            include = os.path.relpath(ROOT/'src/lib/utils/RuntimeProfiler.h', path.parent)
            text = '#include "' + include + '"\n' + text
            path.write_text(text)
    target = ROOT/'docs/research/runtime-profile-instrumentation.json'
    target.write_text(json.dumps(dict(schema='amf-profile-inventory-v1', functions=inventory,
        scope='Functional boundaries, not every inline accessor; excluded helpers are charged to callers.'), indent=2)+'\n')
    print('Instrumented', len(inventory), 'functional boundaries')


if __name__ == '__main__':
    main()
