#!/usr/bin/env python3
"""Attach ordinary Vivado primitive inspection to the visible logic checker."""
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import time
from verify_minimap2_logic_rewrites import Design, parameters, verify


def bind_aliases(pool, rendered, pairs):
    count=0
    for left,right in pairs:
        if not left or not right:raise ValueError('Empty hierarchical net alias')
        for net in (left,right):
            if net not in rendered:rendered[net]=pool.add('\t'+net+'\ts')
        pool.union(rendered[left],rendered[right]);count+=1
    return count


def attach(design,directory,models,alias_directory):
    rendered={}
    for name,value in design.pool.ids.items():
        if name.startswith('$'):continue
        prefix,net,index=name.split('\t')
        name=prefix+net+('' if index=='s' else '['+index+']')
        if name in rendered and design.pool.root(rendered[name])!=design.pool.root(value):
            raise ValueError('Ambiguous rendered net '+name)
        rendered[name]=value
    kinds={}
    with (directory/'cells.tsv').open() as f:
        next(f)
        for line in f:
            name,kind=line.rstrip('\n').split('\t');kinds[name]=kind
    values={n:dict(models[k]['defaults']) for n,k in kinds.items()}
    with (directory/'parameters.tsv').open() as f:
        next(f)
        for line in f:
            n,p,v=line.rstrip('\n').split('\t')
            if v:values[n][p]=v
    pins=collections.defaultdict(dict)
    directions=collections.defaultdict(dict)
    connected_net_names=set()
    with (directory/'pins.tsv').open() as f:
        next(f)
        for line in f:
            cell,pin,direction,net=line.rstrip('\n').split('\t')
            if net:connected_net_names.add(net)
            else:net='__UNCONNECTED__/'+cell+'/'+pin
            if net not in rendered:rendered[net]=design.pool.add('\t'+net+'\ts')
            bit=rendered[net]
            match=re.fullmatch(r'(.+)\[(\d+)\]',pin)
            p,index=(match[1],int(match[2])) if match else (pin,None)
            pins[cell].setdefault(p,{})[index]=bit;directions[cell][p]=direction
    opaque_names=[n for n,c in design.cells.items() if c[0] in design.opaque]
    if len(opaque_names)!=2 or set(design.opaque)!={'xdma_v4_1_31_udma_ram_top','xdma_v4_1_31_udma_wrapper'}:
        raise ValueError('Unexpected opaque scope')
    coverage=collections.Counter()
    for name in kinds:
        parent=[n for n in opaque_names if name.startswith(n+'/')]
        if len(parent)!=1:raise ValueError('Primitive outside protected scope: '+name)
        coverage[parent[0]]+=1
    if set(coverage)!=set(opaque_names):raise ValueError('Incomplete protected scope')
    completed=dict(line.split('\t',1) for line in (directory/'completion.tsv').read_text().splitlines())
    if int(completed['canonical_cells'])!=len(kinds):raise ValueError('Incomplete primitive export')
    alias_meta=dict(line.split('\t',1) for line in (alias_directory/'completion.tsv').read_text().splitlines())
    if alias_meta['input_sha256']!=completed['input_sha256'] or int(alias_meta['queried_nets'])!=len(connected_net_names):
        raise ValueError('Incomplete or mismatched hierarchical aliases')
    with (alias_directory/'aliases.tsv').open() as stream:
        if next(stream).strip()!='net\tsegment':raise ValueError('Unexpected alias schema')
        aliases=bind_aliases(design.pool,rendered,(line.rstrip('\n').split('\t') for line in stream))
    if aliases!=int(alias_meta['alias_pairs']):raise ValueError('Incomplete alias pairs')
    for n in opaque_names:del design.cells[n]
    consts=0
    for name,kind in kinds.items():
        ports={}
        for p,bits in pins[name].items():
            indices=list(bits)
            if indices==[None]:ports[p]=(bits[None],)
            else:
                if None in indices or set(indices)!=set(range(max(indices)+1)):raise ValueError('Noncontiguous pin bus')
                ports[p]=tuple(bits[i] for i in sorted(indices,reverse=True))
        if kind in ('GND','VCC'):
            for p,bits in ports.items():
                if directions[name][p]=='OUT':
                    for bit in bits:design.pool.union(bit,0 if kind=='GND' else 1)
            consts+=1;continue
        if name in design.cells:raise ValueError('Duplicate protected primitive '+name)
        design.cells[name]=(kind,parameters(kind,values[name].items()),ports)
    design.opaque=collections.Counter()
    return dict(inspected_cells=len(kinds),constant_cells=consts,attached_cells=len(kinds)-consts,scope_counts=dict(coverage),completion=completed,hierarchical_alias_pairs=aliases,alias_completion=alias_meta)


def main():
    source,protected,aliases,output=map(lambda x:Path(x).resolve(),sys.argv[1:5])
    output.mkdir(parents=True,exist_ok=False);started=time.monotonic()
    for n in ['verify_minimap2_full_logic.py','verify_minimap2_logic_rewrites.py','compare_minimap2_functional_netlists.py']:
        shutil.copy2(Path(__file__).with_name(n),output/n)
    models=json.loads((protected/'models.json').read_text())
    manifest=dict(source=str(source),protected=str(protected),aliases=str(aliases),scripts={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.py')})
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    base=Design(source/'input/functional.v');manifest['input_attachment']=attach(base,protected/'input',models,aliases/'input')
    print('INPUT_WITH_PROTECTED',len(base.cells),flush=True)
    for label in ['amf3','native']:
        result=Design(source/label/'functional.v');attachment=attach(result,protected/label,models,aliases/label)
        renamed=[]
        for name in list(result.cells):
            old=name.removesuffix('_comp')
            if old!=name and old in base.cells and old not in result.cells and re.fullmatch('LUT[1-6]',result.cells[name][0]):
                result.cells[old]=result.cells.pop(name);renamed.append({'old':old,'new':name})
        dest=output/label;dest.mkdir();report=verify(base,result,dest)
        report.update(protected_attachment=attachment,candidate_cell_name_mapping=renamed,
                      scope='All canonical logical primitives, including two formerly opaque XDMA modules, using authorized Vivado properties',
                      assumptions=['same device primitive semantics and power-up initialization','binary digital values after common GSR release','no physical delays, metastability or setup/hold violations'],
                      full_primitive_rewrite_certificate_passed=report['visible_structural_certificate_passed'])
        (dest/'rewrite_verification.json').write_text(json.dumps(report,indent=2))
        print(label,report['counts'],report['full_primitive_rewrite_certificate_passed'],'conflicts',len(report['net_mapping_conflicts']),'LUT truth checks',len(report['lut_truth_table_rewrites']),flush=True)
        del result
    manifest['elapsed_seconds']=time.monotonic()-started
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))


if __name__=='__main__':main()
