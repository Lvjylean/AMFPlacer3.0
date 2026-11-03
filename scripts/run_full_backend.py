"""Preserve AMF sites and validate exported BELs before invoking the backend."""
import collections
import json
import re
import shutil
import subprocess
import zipfile
from placement_features import route_enabled
from pathlib import Path
from inspect_amf_inputs import digest
from srl_cascades import correct_and_validate
from full_design_metadata import SITE_ONLY, FIXED_LEAF


def escape_error_payload(match):
    # The final upstream batch omits '[' escaping in its double-quoted error
    # dump. Preserve already escaped names and keep Tcl from evaluating cells.
    result=[]
    escaped=False
    for char in match.group(1):
        if char in '[$' and not escaped:
            result.append('\\')
        result.append(char)
        escaped = not escaped if char == '\\' else False
    return 'puts $fo "' + ''.join(result) + '"'


def canonical_export_names(assignments, cell_types, raw):
    """Restore only proven literal export names after Tcl list unescaping.

    Vivado resolves the original bare placement names containing doubled
    backslashes. Our Tcl list reader consumes one escape layer. A restoration
    must occur verbatim in both the input and export, with the same target.
    The placement commands themselves are never rewritten.
    """
    tokens = raw.split()
    literal = {name: target for name, target in zip(tokens[::2], tokens[1::2]) if '\\' in name}
    resolved = dict(assignments)
    aliases = []
    for name, target in assignments.items():
        if name in cell_types or '\\' not in name:
            continue
        canonical = name.replace('\\', '\\\\')
        if canonical not in cell_types or literal.get(canonical) != target:
            continue
        if canonical in resolved:
            raise ValueError('Duplicate canonical AMF cell: ' + canonical)
        resolved[canonical] = resolved.pop(name)
        aliases.append(dict(parsed_name=name, input_name=canonical, target=target))
    return resolved, aliases


def predeclare_async_targets(script, assignments, groups):
    """Install ASYNC targets in a bounded transaction before partial placement.

    Packing groups intentionally split at clocks/control sets. They are not
    Vivado's import linkage boundary, so even one place_cell per group is unsafe.
    Reuse their complete member inventory without traversing the netlist again.
    LOC setters also trigger linkage: temporarily suspend ASYNC only while
    writing the targets, then restore it even on failure. No optimization,
    place_cell, checkpoint, or backend operation runs with ASYNC suspended.
    """
    lines = groups.read_text().splitlines()
    if not lines or lines[0] != 'AMF_ASYNC_GROUPS 1':
        raise ValueError('Invalid synchronization group schema')
    targets = {}
    for line in lines[1:]:
        if not line.strip():
            continue
        _, name = line.split()
        if name in targets:
            raise ValueError('Duplicate ASYNC import member: ' + name)
        target = assignments.get(name, '')
        if not re.fullmatch(r'SLICE_X\d+Y\d+/[A-H]FF2?', target):
            raise ValueError('Missing/invalid ASYNC target: ' + name)
        if any(c in name for c in '{}\\\r\n'):
            raise ValueError('Unsupported ASYNC Tcl identifier: ' + name)
        targets[name] = target
    if not targets:
        return script, 0
    marker = 'place_design -unplace\n'
    if script.count(marker) != 1:
        raise ValueError('Expected one unplacement boundary for ASYNC targets')
    pairs = '\n'.join('    {'+name+'} {'+target+'}' for name, target in targets.items())
    setup = '\n# Declare the complete ASYNC inventory before any partial placement.\napply {{} {\n'
    setup += '    set started [clock milliseconds]\n    set targets {\n'+pairs+'\n    }\n'
    setup += r'''
    set members [dict create]
    dict for {name target} $targets {
        set cell [get_cells -quiet $name]
        if {[llength $cell] != 1 || ![get_property ASYNC_REG $cell]} {
            error "Missing/non-ASYNC preconstraint member: $name"
        }
        lassign [split $target /] site bel
        if {[get_property IS_LOC_FIXED $cell] && [get_property LOC $cell] ne $site} {
            error "ASYNC preconstraint conflicts with fixed LOC: $name"
        }
        if {[get_property IS_BEL_FIXED $cell] && [lindex [split [get_property BEL $cell] .] end] ne $bel} {
            error "ASYNC preconstraint conflicts with fixed BEL: $name"
        }
        dict set members $name $cell
    }
    set allMembers [get_cells -quiet [dict keys $targets]]
    if {[llength $allMembers] != [dict size $targets]} {error "Incomplete ASYNC collection"}
    set failed [catch {
        set_property ASYNC_REG false $allMembers
        dict for {name target} $targets {
            set_property BEL [lindex [split $target /] 1] [dict get $members $name]
        }
        dict for {name target} $targets {
            set_property LOC [lindex [split $target /] 0] [dict get $members $name]
        }
    } failure options]
    # Restore before returning or propagating an error. A restore failure is
    # fatal too: the caller must never continue to placement/routing in that case.
    set_property ASYNC_REG true $allMembers
    set restored [get_property ASYNC_REG $allMembers]
    if {[llength $restored] != [dict size $members]} {error "Incomplete ASYNC restore"}
    foreach value $restored {
        if {![string is boolean -strict $value] || !$value} {error "ASYNC restore failed"}
    }
    if {$failed} {return -options $options $failure}
    set elapsed [expr {[clock milliseconds]-$started}]
    set report [open [file join $::script_path .. reports async_preconstraints.tsv] w]
    puts $report "cells\t[dict size $targets]\nelapsed_ms\t$elapsed\ntargets_changed\t0\nasync_restored\t1"
    close $report
    puts "AMF_ASYNC_PRECONSTRAINTS cells=[dict size $targets] elapsed_ms=$elapsed"
}}
'''
    return script.replace(marker, marker+setup, 1), len(targets)


def prepare(root, source, directory, manifest):
    upstream = manifest.get('upstream_backend', False)
    source_manifest=directory/'inputs/placement_source_manifest.json'
    shutil.copy2(source/'manifest.json',source_manifest)
    shutil.copy2(source/'config.json',directory/'inputs/placement_source_config.json')
    generated=source/'placement/DumpCLBPacking-first-0.tcl'
    original=generated.read_text()
    # AMF 2.0's exporter emits the count as a Tcl command. Split its existing
    # import and backend calls so their time and placement retention are measured.
    adapted,n=re.subn(r'^\$errorNum\s*$', 'puts "AMF_INITIAL_PLACEMENT_ERROR_BATCHES=$errorNum"',original,flags=re.M)
    adapted=re.sub(r'puts \$fo "((?:\\.|[^"\\])*)"',escape_error_payload,adapted,flags=re.S)
    if not adapted.endswith('place_design\nroute_design\n'):
        raise ValueError('Unexpected generated Tcl backend footer')
    adapted=adapted[:-len('place_design\nroute_design\n')]
    batches=list(re.finditer(r'^set result \[catch \{place_cell \{.*?\}\}\]$', adapted, re.M | re.S))
    # Long imports otherwise provide no progress between the first placement
    # warning and the final audit. Keep commands and assignments unchanged.
    for index in range(len(batches)-1,-1,-1):
        if (index+1)%500==0 or index+1==len(batches):
            end=batches[index].end()
            adapted=adapted[:end]+'\nputs "AMF_IMPORT_BATCHES_ATTEMPTED '+str(index+1)+'/'+str(len(batches))+'"; flush stdout'+adapted[end:]
    adapted=re.sub(r'(set result \[catch \{place_cell \{.*?\}\})\]',
        r'\1 amf3_place_error]\nif {$result} {incr amf3_import_error_events; puts "AMF_IMPORT_REJECTED: $amf3_place_error"}', adapted, flags=re.S)
    adapted=adapted.replace('set result [catch {place_cell  $placeBatch }]',
        'set result [catch {place_cell  $placeBatch } amf3_place_error]\n       if {$result} {incr amf3_import_error_events; puts "AMF_IMPORT_RETRY_REJECTED: $amf3_place_error"}')
    raw=directory/'placement/requested.list'
    raw.write_text('\n'.join(re.findall(r'\[catch \{place_cell \{(.*?)\}\}\]',original,re.S)))
    normalizer=directory/'inputs/normalize_placement.tcl'
    normalizer.write_text(r"""
set f [open [lindex $argv 0] r];set raw [read $f];close $f
if {[llength $raw] % 2} {error "Odd placement list length"}
array set seen {}
set f [open [lindex $argv 1] w]
foreach {name target} $raw {
    if {[info exists seen($name)]} {error "Duplicate AMF cell: $name"}
    set seen($name) 1
    puts $f "$name\t$target"
}
close $f
""")
    subprocess.run(['tclsh',str(normalizer),str(raw),str(directory/'placement/requested.tsv')],check=True)
    assignments=dict(line.split('\t') for line in (directory/'placement/requested.tsv').read_text().splitlines())
    if not assignments: raise ValueError('No placement assignments found')
    cfg=json.loads((source/'config.json').read_text())
    fixed_inputs = {}
    inherited_fixed = {}
    if cfg.get('fixed units file') and not upstream:
        fixed_path = Path(cfg['fixed units file'])
        expected = json.loads((source/'manifest.json').read_text())['inputs']['fixed units file']['sha256']
        if digest(fixed_path) != expected:
            raise ValueError('Fixed input assignments changed after AMF execution')
        for line in fixed_path.read_text().splitlines()[1:]:
            m = re.fullmatch(r'name=> (\S+) loc=> (\S+) bel=> (\S+)', line)
            if not m:
                raise ValueError('Invalid fixed input assignment: ' + line)
            name, site, bel = m.groups()
            if name in fixed_inputs:
                raise ValueError('Duplicate fixed input cell: ' + name)
            fixed_inputs[name] = dict(site=site, bel=bel)
            if name in assignments:
                if assignments[name].split('/')[0] != site:
                    raise ValueError('AMF moved a fixed input cell: ' + name)
            else:
                # The legacy exporter omits locked I/O/clock buffers. They are
                # inputs to placement, not missing movable-cell decisions.
                if any(c in name + site for c in '{}\\'):
                    raise ValueError('Unsupported fixed input Tcl name: ' + name)
                inherited_fixed[name] = site
                assignments[name] = site
        shutil.copy2(fixed_path, directory/'inputs/fixed_units')
    counts=collections.Counter(); cell_types={}
    srl_kinds={}; srl_edges=[]; fixed_kinds={}
    with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
        with z.open(z.namelist()[0]) as f:
            for line in f:
                if line.startswith(b'curCell=> '):
                    fields=line.decode().split();name,kind=fields[1],fields[3]
                    if kind.startswith('SRL'):srl_kinds[name]=kind
                    if kind in ('VCC','GND'):continue
                    cell_types[name]=kind
                    counts[kind]+=1
                    if name in fixed_inputs:fixed_kinds[name]=kind
                    if name in inherited_fixed and kind not in SITE_ONLY:
                        if kind not in FIXED_LEAF:
                            raise ValueError('Unreviewed inherited fixed primitive: ' + kind)
                        fixed = fixed_inputs[name]
                        target = fixed['bel']
                        if not re.fullmatch(re.escape(fixed['site']) + r'/[A-Za-z0-9_]+', target):
                            raise ValueError('Invalid fixed interface BEL: ' + target)
                        assignments[name] = inherited_fixed[name] = target
                elif b'/Q31' in line and b' dir=> IN ' in line and b'drivepin=> ' in line:
                    driver_cell,pin=line.decode().split('drivepin=> ')[1].strip().rsplit('/',1)
                    if pin=='Q31':srl_edges.append((driver_cell,name))
    commands = ['place_cell [list {' + name + '} {' + target + '}]'
                for name, target in inherited_fixed.items()]
    if cfg.get('CompositeIOTimingBoundary') == 'true':
        contract = ['cell\tsite\tbel\ttarget_mode']
        final_locks = []
        for name, fixed in fixed_inputs.items():
            kind = fixed_kinds[name]
            if kind not in SITE_ONLY | FIXED_LEAF:
                raise ValueError('Unsupported fixed interface contract: ' + kind)
            site, bel = fixed['site'], fixed['bel'].rsplit('/', 1)[-1]
            mode = 'site' if kind in SITE_ONLY else 'bel'
            target = site if mode == 'site' else site+'/'+bel
            if any(c in name+target for c in '{}\\\n\r'):
                raise ValueError('Unsupported fixed interface Tcl identifier: ' + name)
            commands.append('place_cell [list {'+name+'} {'+target+'}]')
            final_locks.append('set_property IS_LOC_FIXED true [get_cells {'+name+'}]')
            if mode == 'bel': final_locks.append('set_property IS_BEL_FIXED true [get_cells {'+name+'}]')
            assignments[name] = target
            contract.append(f'{name}\t{site}\t{bel}\t{mode}')
        # Related I/O placement can clear earlier PHY BEL locks. Lock after all placements.
        commands.extend(final_locks)
        (directory/'placement/fixed_contract.tsv').write_text('\n'.join(contract)+'\n')
        shutil.copy2(root/'scripts/full_interface_contract.tcl', directory/'inputs/full_interface_contract.tcl')
        manifest['interface_contract_sha256'] = digest(directory/'inputs/full_interface_contract.tcl')
        manifest['fixed_contract_sha256'] = digest(directory/'placement/fixed_contract.tsv')
        if cfg.get('ASYNC_REG groups file'):
            group_file = Path(cfg['ASYNC_REG groups file'])
            expected = json.loads((source/'manifest.json').read_text())['inputs']['ASYNC_REG groups file']['sha256']
            if digest(group_file) != expected: raise ValueError('ASYNC_REG groups changed after AMF execution')
            shutil.copy2(group_file, directory/'placement/async_groups.txt')
    adapted += '\n# Reapply declared fixed input targets after legacy unplacement.\n' + '\n'.join(commands) + '\n'
    assignments,name_aliases=canonical_export_names(assignments,cell_types,raw.read_text())
    covered=collections.Counter(cell_types[name] for name in assignments if name in cell_types)
    extra=set(assignments)-cell_types.keys()
    (directory/'reports/export_name_aliases.json').write_text(json.dumps(
        dict(count=len(name_aliases),aliases=name_aliases,placement_commands_changed=False),indent=2)+'\n')
    if extra:raise ValueError('AMF emitted cells absent from input: '+repr(sorted(extra)[:5]))
    if upstream:
        corrections = []
    else:
        assignments,corrections=correct_and_validate(assignments,srl_kinds,srl_edges)
    for change in corrections:
        # The migration is restricted to a site occupied solely by this SRL.
        # Replace its target in both the placement command and diagnostic dump.
        if change['before'] not in adapted:raise ValueError('SRL export target absent from Tcl')
        adapted=adapted.replace(change['before'],change['after'])
    groups = directory/'placement/async_groups.txt'
    if groups.exists():
        adapted, async_count = predeclare_async_targets(adapted, assignments, groups)
        manifest['async_import_preconstraints'] = dict(
            cells=async_count, targets_changed=False,
            policy='bounded ASYNC suspension for BEL/LOC declaration; restore before place_cell')
    (directory/'placement/import_placement.tcl').write_text(adapted)
    with (directory/'placement/bel_corrections.tsv').open('w') as f:
        f.write('cell\tbefore\tafter\n')
        for x in corrections:f.write(x['cell']+'\t'+x['before']+'\t'+x['after']+'\n')
    with (directory/'placement/srl_cascades.tsv').open('w') as f:
        f.write('source\tsink\n')
        for a,b in srl_edges:f.write(a+'\t'+b+'\n')
    (directory/'reports/srl_cascades.json').write_text(json.dumps(
        dict(checked=0 if upstream else len(srl_edges),violations=None if upstream else 0,
             validation_deferred_to_vivado=upstream,bel_corrections=corrections),indent=2)+'\n')
    with (directory/'placement/requested.tsv').open('w') as f:
        for name,target in assignments.items():f.write(name+'\t'+target+'\n')
    metrics=dict(input_cells=sum(counts.values()),assigned_cells=len(assignments),
                 raw_amf_assigned_cells=len(assignments)-len(inherited_fixed),
                 inherited_fixed_input_cells=len(inherited_fixed),
                 input_by_type=dict(counts),assigned_by_type=dict(covered),
                 missing_by_type={k:counts[k]-covered[k] for k in counts if counts[k]!=covered[k]})
    (directory/'reports/amf_coverage.json').write_text(json.dumps(metrics,indent=2)+'\n')
    if upstream:
        metrics['omission_policy'] = 'Original exporter hands unexported resources to Vivado; no fixed-cell replay or BEL corrections'
        (directory/'reports/amf_coverage.json').write_text(json.dumps(metrics,indent=2)+'\n')
    if metrics['missing_by_type'] and not upstream:
        raise ValueError('Incomplete AMF BEL export: '+repr(metrics['missing_by_type']))
    if manifest.get('release_fixed_clock_buffers'):
        clocks = {name: fixed_inputs[name]['site'] for name, kind in fixed_kinds.items() if kind == 'BUFGCE'}
        if not clocks:
            raise ValueError('Clock release requires declared fixed BUFGCE input cells')
        with (directory/'placement/clock_buffer_release.tsv').open('w') as f:
            f.write('cell\timported_site\n')
            for name, site in sorted(clocks.items()):f.write(name+'\t'+site+'\n')
        manifest['released_fixed_clock_buffers'] = clocks
    for name in ('resources.json','resources.tsv','cascades.tsv'):
        if source.resolve()!=directory.resolve() and (source/'placement'/name).exists():
            shutil.copy2(source/'placement'/name,directory/'placement'/name)
    if route_enabled(cfg):
        planned=source/'placement/sll-plan.tcl'
        if not planned.is_file():raise ValueError('E2 SLL plan missing from AMF output')
        if planned.resolve()!=(directory/'placement/sll-plan.tcl').resolve():shutil.copy2(planned,directory/'placement/sll-plan.tcl')
        helper=root/'scripts/sll_routes.tcl'
        shutil.copy2(helper,directory/'inputs/sll_routes.tcl')
        manifest['sll_plan_sha256']=digest(planned)
        manifest['sll_route_helper_sha256']=digest(helper)
        manifest['sll_parallel_files_sha256']={}
        for name in ('sll_parallel.py','sll_parallel.tcl','sll_parallel_worker.tcl'):
            helper=root/'scripts'/name
            shutil.copy2(helper,directory/'inputs'/name)
            manifest['sll_parallel_files_sha256'][name]=digest(helper)

    script=directory/'inputs/full_backend.tcl';shutil.copy2(root/'scripts/full_backend.tcl',script)
    from placement_features import validate as validate_features
    validate_features(cfg)
    manifest['sll_route_constraint_enabled']=route_enabled(cfg)
    script.write_text('set ::env(AMF_SLL_ROUTE_CONSTRAINTS) '+str(int(route_enabled(cfg)))+'\n'+script.read_text())
    if route_enabled(cfg):
        workers=int(cfg.get('SllConstraintWorkers',4))
        if not 1<=workers<=8:raise ValueError('SllConstraintWorkers must be in [1,8]')
        script.write_text('set ::env(AMF_SLL_CONSTRAINT_WORKERS) '+str(workers)+'\n'+script.read_text())
        manifest['sll_constraint_workers']=workers
    shutil.copy2(root/'scripts/sll_usage.tcl',directory/'inputs/sll_usage.tcl')
    manifest['sll_usage_helper_sha256']=digest(root/'scripts/sll_usage.tcl')
    acceptance=root/'scripts/import_acceptance.tcl'
    if acceptance.is_file():
        shutil.copy2(acceptance,directory/'inputs/import_acceptance.tcl')
        manifest['import_acceptance_sha256']=digest(acceptance)
    manifest['placement_source']=dict(run=str(source),manifest_snapshot=str(source_manifest),manifest_sha256=digest(source_manifest),
        original_tcl=str(generated),original_tcl_sha256=digest(generated),import_tcl_sha256=digest(directory/'placement/import_placement.tcl'),
        adapter_diagnostic_lines=n,assignments_changed=bool(corrections),site_assignments_changed=False,
        bel_corrections=corrections)
    manifest['placement_source']['inherited_fixed_input_assignments'] = inherited_fixed
    manifest['placement_source']['fixed_input_bels'] = fixed_inputs
    manifest['placement_source']['audit_name_aliases'] = name_aliases
    manifest['backend_script_sha256']=digest(script)
    diagnostic=root/'scripts/diagnostics/export_boundary_timing_samples.tcl'
    if diagnostic.is_file():
        shutil.copy2(diagnostic,directory/'inputs'/diagnostic.name)
        manifest['boundary_diagnostic_sha256']=digest(diagnostic)
    manifest['runner_files_sha256']={}
    for name in ('run_full_flow.py','run_full_backend.py','summarize_full_flow.py','summarize_face_detect_flow.py','srl_cascades.py','full_design_metadata.py','inspect_amf_inputs.py','amf3.py','build_physical_boundaries.py','diagnostics/analyze_boundary_timing_samples.py'):
        origin=root/'scripts'/name
        if origin.is_file():
            snapshot=directory/'inputs'/name
            snapshot.parent.mkdir(parents=True,exist_ok=True)
            if name != 'run_full_flow.py' or not snapshot.exists():
                shutil.copy2(origin,snapshot)
            manifest['runner_files_sha256'][name]=digest(snapshot)
    return script
