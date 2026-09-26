"""Preserve AMF sites and validate exported BELs before invoking the backend."""
import collections
import json
import re
import shutil
import subprocess
import zipfile
from inspect_amf_inputs import digest
from srl_cascades import correct_and_validate


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


def prepare(root, source, directory, manifest):
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
        r'\1 amf3_place_error]\nif {$result} {puts "AMF_IMPORT_REJECTED: $amf3_place_error"}', adapted, flags=re.S)
    adapted=adapted.replace('set result [catch {place_cell  $placeBatch }]',
        'set result [catch {place_cell  $placeBatch } amf3_place_error]\n       if {$result} {puts "AMF_IMPORT_RETRY_REJECTED: $amf3_place_error"}')
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
    counts=collections.Counter(); covered=collections.Counter(); extra=set(assignments)
    srl_kinds={}; srl_edges=[]
    with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
        with z.open(z.namelist()[0]) as f:
            for line in f:
                if line.startswith(b'curCell=> '):
                    fields=line.decode().split();name,kind=fields[1],fields[3]
                    if kind.startswith('SRL'):srl_kinds[name]=kind
                    if kind in ('VCC','GND'):continue
                    counts[kind]+=1
                    if name in assignments:covered[kind]+=1;extra.discard(name)
                elif b'/Q31' in line and b' dir=> IN ' in line and b'drivepin=> ' in line:
                    driver_cell,pin=line.decode().split('drivepin=> ')[1].strip().rsplit('/',1)
                    if pin=='Q31':srl_edges.append((driver_cell,name))
    if extra:raise ValueError('AMF emitted cells absent from input: '+repr(sorted(extra)[:5]))
    assignments,corrections=correct_and_validate(assignments,srl_kinds,srl_edges)
    for change in corrections:
        # The migration is restricted to a site occupied solely by this SRL.
        # Replace its target in both the placement command and diagnostic dump.
        if change['before'] not in adapted:raise ValueError('SRL export target absent from Tcl')
        adapted=adapted.replace(change['before'],change['after'])
    (directory/'placement/import_placement.tcl').write_text(adapted)
    with (directory/'placement/bel_corrections.tsv').open('w') as f:
        f.write('cell\tbefore\tafter\n')
        for x in corrections:f.write(x['cell']+'\t'+x['before']+'\t'+x['after']+'\n')
    with (directory/'placement/srl_cascades.tsv').open('w') as f:
        f.write('source\tsink\n')
        for a,b in srl_edges:f.write(a+'\t'+b+'\n')
    (directory/'reports/srl_cascades.json').write_text(json.dumps(
        dict(checked=len(srl_edges),violations=0,bel_corrections=corrections),indent=2)+'\n')
    with (directory/'placement/requested.tsv').open('w') as f:
        for name,target in assignments.items():f.write(name+'\t'+target+'\n')
    metrics=dict(input_cells=sum(counts.values()),assigned_cells=len(assignments),
                 input_by_type=dict(counts),assigned_by_type=dict(covered),
                 missing_by_type={k:counts[k]-covered[k] for k in counts if counts[k]!=covered[k]})
    (directory/'reports/amf_coverage.json').write_text(json.dumps(metrics,indent=2)+'\n')
    if metrics['missing_by_type']:
        raise ValueError('Incomplete AMF BEL export: '+repr(metrics['missing_by_type']))
    for name in ('resources.json','resources.tsv','cascades.tsv'):
        if source.resolve()!=directory.resolve() and (source/'placement'/name).exists():
            shutil.copy2(source/'placement'/name,directory/'placement'/name)
    script=directory/'inputs/full_backend.tcl';shutil.copy2(root/'scripts/full_backend.tcl',script)
    manifest['placement_source']=dict(run=str(source),manifest_snapshot=str(source_manifest),manifest_sha256=digest(source_manifest),
        original_tcl=str(generated),original_tcl_sha256=digest(generated),import_tcl_sha256=digest(directory/'placement/import_placement.tcl'),
        adapter_diagnostic_lines=n,assignments_changed=bool(corrections),site_assignments_changed=False,
        bel_corrections=corrections)
    manifest['backend_script_sha256']=digest(script)
    diagnostic=root/'scripts/diagnostics/export_boundary_timing_samples.tcl'
    if diagnostic.is_file():
        shutil.copy2(diagnostic,directory/'inputs'/diagnostic.name)
        manifest['boundary_diagnostic_sha256']=digest(diagnostic)
    manifest['runner_files_sha256']={}
    for name in ('run_full_flow.py','run_full_backend.py','summarize_full_flow.py','summarize_face_detect_flow.py','srl_cascades.py','inspect_amf_inputs.py','amf3.py','build_physical_boundaries.py','diagnostics/analyze_boundary_timing_samples.py'):
        origin=root/'scripts'/name
        if origin.is_file():
            snapshot=directory/'inputs'/name
            snapshot.parent.mkdir(parents=True,exist_ok=True)
            if name != 'run_full_flow.py' or not snapshot.exists():
                shutil.copy2(origin,snapshot)
            manifest['runner_files_sha256'][name]=digest(snapshot)
    return script
