from pathlib import Path
import csv,json,hashlib,shutil

def chains_from_edges(edges):
    nxt={};prev={}
    for a,b in edges:
        assert a!=b and a not in nxt and b not in prev, 'Branch/duplicate SRL cascade'
        nxt[a]=b;prev[b]=a
    allnodes=set(nxt)|set(prev);seen=set();chains=[]
    for head in sorted(allnodes-set(prev)):
        chain=[];n=head
        while n is not None:
            assert n not in seen, 'SRL cascade cycle'
            seen.add(n);chain.append(n);n=nxt.get(n)
        chains.append(chain)
    assert seen==allnodes, 'SRL cascade cycle'
    return chains

def install(backend):
    b=Path(backend);p=b/'placement';f=p/'import_placement.tcl'
    original=f.read_text();before=hashlib.sha256(original.encode()).hexdigest()
    targets={}
    for line in (p/'requested.tsv').read_text().splitlines():
        n,t=line.split('\t');assert n not in targets;targets[n]=t
    edges=[(r['source'],r['sink']) for r in csv.DictReader((p/'srl_cascades.tsv').open(),delimiter='\t')]
    chains=chains_from_edges(edges)
    carry={r['cell'] for r in csv.DictReader((p/'carry_site_groups.tsv').open(),delimiter='\t')}
    nodes={n for c in chains for n in c};assert not nodes&carry,'SRL/CARRY submission overlap'
    assert nodes<=targets.keys(),'Missing chain target'
    out=['chain\tcell\ttarget']
    for i,c in enumerate(chains):
        for n in c:out.append(f'{i}\t{n}\t{targets[n]}')
    (p/'srl_chain_groups.tsv').write_text('\n'.join(out)+'\n')
    shutil.copyfile(Path(__file__).with_suffix('.tcl'),b/'inputs/srl_chain_import.tcl')
    count=original.count('amf_carry_import::place');assert count>1
    text=original.replace('amf_carry_import::place','amf_srl_import::place')
    marker='set result [catch {amf_srl_import::place'
    assert marker in text
    setup='source [file join $script_path .. inputs srl_chain_import.tcl]\namf_srl_import::init [file join $script_path srl_chain_groups.tsv] [file join $script_path .. reports srl_chain_import.tsv]\n'
    text=text.replace(marker,setup+marker,1)
    assert 'amf_carry_import::init' in text[:text.index(setup)]
    f.write_text(text)
    result=dict(chains=len(chains),members=len(nodes),edges=len(edges),max_chain_members=max(map(len,chains)),wrapped_calls=count,original_import_sha256=before,modified_import_sha256=hashlib.sha256(text.encode()).hexdigest(),targets_unchanged=True)
    (b/'reports/srl-chain-preflight.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
