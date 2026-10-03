#!/usr/bin/env python3
"""Check visible MiniMap2 netlists by explicit local sequential rewrite rules.

This is a fail-closed, design-specific structural certificate checker, not a
general Verilog/formal tool. Protected module bodies remain outside the proof.
Functional rules assume binary stable signals, equal power-up initialization,
GSR released, and no physical delays or setup/hold violations.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import time

from compare_minimap2_functional_netlists import parse, TOKEN, ATTR


def ident(value):
    return value[1:] if value.startswith('\\') else value


def declaration(tokens):
    index = 1
    bounds = None
    if tokens[index] == '[':
        end = tokens.index(']', index)
        a, b = ''.join(tokens[index+1:end]).split(':')
        bounds = (int(a), int(b))
        index = end + 1
    if len(tokens) != index + 1:
        raise ValueError('Unsupported declaration: ' + str(tokens))
    return ident(tokens[index]), bounds


def split_concat(tokens):
    result, start, depth = [], 0, 0
    for i, token in enumerate(tokens):
        if token == ',' and depth == 0:
            result.append(tokens[start:i]); start = i + 1
        elif token in ('{', '[', '('): depth += 1
        elif token in ('}', ']', ')'): depth -= 1
    result.append(tokens[start:])
    return result


def parameters(kind, pairs):
    values = dict(pairs)
    if kind == 'FDRE':
        # Defaults checked against the installed Vivado 2024.2 UNISIM model.
        for name in ('INIT','IS_C_INVERTED','IS_D_INVERTED','IS_R_INVERTED'):
            values.setdefault(name,"1'b0")
    return tuple(sorted(values.items()))


class Pool:
    def __init__(self):
        self.ids = {}; self.names = []; self.parent = {}
        for bit in '01xz': self.add('$' + bit)

    def add(self, name):
        value = self.ids.get(name)
        if value is None:
            value = len(self.names); self.ids[name] = value; self.names.append(name)
        return value

    def root(self, value):
        trail = []
        while value in self.parent:
            trail.append(value); value = self.parent[value]
        for old in trail: self.parent[old] = value
        return value

    def union(self, a, b):
        a, b = self.root(a), self.root(b)
        if a == b: return
        if a < 4 and b < 4: raise ValueError('Different constants shorted')
        if b < a: a, b = b, a
        self.parent[b] = a


class Design:
    def __init__(self, path):
        raw, modules, self.inventory = parse(path)
        self.pool = Pool(); self.cells = {}; self.top_ports = {}; self.opaque = collections.Counter()
        library_types = {p.stem for p in Path('/Projects/Xilinx/Vivado/2024.2/data/verilog/src/unisims').glob('*.v')}
        by_module = collections.defaultdict(list)
        for (module, name), cell in raw.items(): by_module[module].append((ident(name), cell))
        del raw
        decls = {m:{} for m in modules}
        for m, data in modules.items():
            for tokens in data['ports']:
                n, bounds = declaration(tokens); decls[m][n] = bounds
        current = None
        with path.open() as stream:
            for line in stream:
                if line.startswith('`pragma protect begin_protected'): break
                if line.startswith('module '): current = TOKEN.findall(line)[1]
                elif line.strip() == 'endmodule': current = None
                elif 'wire ' in line and ATTR.sub('',line).strip().startswith('wire '):
                    tokens = TOKEN.findall(ATTR.sub('',line).strip())
                    if tokens[-1] != ';': raise ValueError('Multiline wire declaration')
                    n, bounds = declaration(tokens[:-1]); decls[current][n] = bounds

        def expand(module, prefix, tokens):
            tokens = tuple(tokens)
            if not tokens: return ()
            if tokens[0] == '{':
                if tokens[-1] != '}': raise ValueError('Malformed concat')
                inner = tokens[1:-1]
                if '{' in inner and re.fullmatch(r'\d+', ''.join(inner[:inner.index('{')])):
                    k = inner.index('{'); count = int(''.join(inner[:k]))
                    return expand(module, prefix, inner[k:]) * count
                return tuple(bit for part in split_concat(inner) for bit in expand(module, prefix, part))
            literal = re.fullmatch(r"(\d+)'[sS]?([bBoOdDhH])([0-9a-fA-F_xXzZ]+)", ''.join(tokens))
            if literal:
                width, radix, value = literal.groups(); width = int(width); value = value.replace('_','').lower()
                if set(value) <= {'x'}: return (2,) * width
                if set(value) <= {'z'}: return (3,) * width
                if 'x' in value or 'z' in value: raise ValueError('Mixed unknown literal')
                number = int(value, {'b':2,'o':8,'d':10,'h':16}[radix.lower()])
                return tuple((number >> bit) & 1 for bit in range(width-1,-1,-1))
            name = ident(tokens[0])
            if name not in decls[module]: raise ValueError('Undeclared signal: ' + module + '/' + str(tokens))
            bounds = decls[module][name]
            if len(tokens) > 1:
                if tokens[1] != '[' or tokens[-1] != ']': raise ValueError('Unsupported expression ' + str(tokens))
                selection = ''.join(tokens[2:-1])
                bounds = tuple(map(int, selection.split(':'))) if ':' in selection else (int(selection), int(selection))
            indices = [None] if bounds is None else range(bounds[0], bounds[1]+(1 if bounds[1]>=bounds[0] else -1), 1 if bounds[1]>=bounds[0] else -1)
            return tuple(self.pool.add(prefix + '\t' + name + '\t' + ('s' if i is None else str(i))) for i in indices)

        def flatten(module, prefix, parent_ports=None):
            data = modules[module]
            declared_ports = {ident(tokens[-1]):(tokens[0], expand(module,prefix,(tokens[-1],))) for tokens in data['ports']}
            if parent_ports is None:
                self.top_ports = declared_ports
            else:
                if set(parent_ports) != set(declared_ports): raise ValueError('Module port mismatch ' + prefix)
                for name, bits in parent_ports.items():
                    local = declared_ports[name][1]
                    if len(bits) != len(local): raise ValueError('Port width mismatch ' + prefix + name)
                    for a,b in zip(local,bits): self.pool.union(a,b)
            for tokens in data['assigns']:
                eq = tokens.index('='); left = expand(module,prefix,tokens[1:eq]); right = expand(module,prefix,tokens[eq+1:])
                if len(left) != len(right): raise ValueError('Assign width mismatch')
                for a,b in zip(left,right): self.pool.union(a,b)
            for name,(kind,params,ports) in by_module[module]:
                pins = {ident(p):expand(module,prefix,v) for p,v in ports}
                path_name = prefix + name
                if kind in modules:
                    if params: raise ValueError('Unexpected parameterized module instantiation')
                    flatten(kind,path_name+'/',pins)
                else:
                    if path_name in self.cells: raise ValueError('Duplicate flattened cell ' + path_name)
                    self.cells[path_name] = (kind, parameters(kind,((p,''.join(v)) for p,v in params)), pins)
                    if kind not in library_types:
                        self.opaque[kind] += 1
        flatten('minimap2_functional','')


def mapping(base, result):
    result_to_base = {i:i for i in range(4)}
    conflicts = []
    for name, rid in result.pool.ids.items():
        bid = base.pool.ids.get(name)
        if bid is None: continue
        rr, br = result.pool.root(rid), base.pool.root(bid)
        if rr in result_to_base and result_to_base[rr] != br:
            if len(conflicts)<20: conflicts.append([name, result_to_base[rr], br])
        else: result_to_base[rr] = br
    return result_to_base, conflicts


def pin_norm(design, pins, lookup=None):
    return tuple(sorted((p,tuple(design.pool.root(b) if lookup is None else lookup.get(design.pool.root(b),('unmapped',design.pool.root(b))) for b in bits)) for p,bits in pins.items()))


def duplicate_luts(design):
    """Merge equal LUT equations and equal initialized FDRE recurrences.

    Each merge depends only on already established signal equalities. For FFs,
    equal INIT plus equal C/CE/D/R and inversion parameters gives induction on
    clock events. No identical-name or suffix-based equivalence assumption.
    """
    witnesses=[]
    ff_controls={'FDRE':'R','FDSE':'S','FDCE':'CLR','FDPE':'PRE'}
    luts=[(n,c) for n,c in design.cells.items() if re.fullmatch('LUT[1-6]',c[0]) or c[0] in ff_controls]
    for iteration in range(20):
        seen={}; count=0
        for name,(kind,params,pins) in luts:
            out='Q' if kind in ff_controls else 'O'
            expected={'Q','C','CE','D',ff_controls[kind]} if kind in ff_controls else {'O'}|{'I'+str(i) for i in range(int(kind[-1]))}
            if set(pins)!=expected: raise ValueError('Unexpected pins for '+kind)
            key=(kind,params,pin_norm(design,{p:b for p,b in pins.items() if p!=out}))
            if key not in seen: seen[key]=name;continue
            other=seen[key];a=design.pool.root(pins[out][0]);b=design.pool.root(design.cells[other][2][out][0])
            if a==b: continue
            design.pool.union(a,b);count+=1;witnesses.append({'cell':name,'equivalent_to':other,'iteration':iteration,'type':kind})
        if not count: break
    return witnesses


def lut_function(design, kind, params, pins, lookup=None):
    if not re.fullmatch('LUT[1-6]',kind) or set(dict(params))!={'INIT'}:return None
    value=dict(params)['INIT']
    m=re.fullmatch(r"(\d+)'([hb])([0-9A-Fa-f]+)",value)
    if not m:return None
    init=int(m[3],16 if m[2]=='h' else 2)
    inputs=[]
    for i in range(int(kind[-1])):
        bit=design.pool.root(pins['I'+str(i)][0])
        if lookup is not None:bit=lookup.get(bit)
        if bit is None or bit in (2,3):return None
        inputs.append(bit)
    variables=sorted(set(inputs)-{0,1});positions={v:i for i,v in enumerate(variables)}
    table=[]
    for assignment in range(1<<len(variables)):
        index=sum((b if b in (0,1) else ((assignment>>positions[b])&1))<<i for i,b in enumerate(inputs))
        table.append((init>>index)&1)
    essential=[i for i in range(len(variables)) if any(table[a]!=table[a^(1<<i)] for a in range(len(table)))]
    reduced=0
    for a in range(1<<len(essential)):
        old=sum(((a>>j)&1)<<i for j,i in enumerate(essential));reduced|=table[old]<<a
    return tuple(variables[i] for i in essential),reduced


def verify(base, result, output):
    added = set(result.cells)-set(base.cells)
    removed = set(base.cells)-set(result.cells)
    buffers = []; replicas = []; dsp_checks = []; consumed = set()
    for name in sorted(added):
        kind,params,pins = result.cells[name]
        if kind != 'BUFGCE': continue
        if dict(params) != {'CE_TYPE':'"ASYNC"'} or pins['CE']!=(1,) or set(pins)!={'I','O','CE'} or len(pins['I'])!=1 or len(pins['O'])!=1:
            raise ValueError('Unsupported buffer: '+name)
        result.pool.union(pins['I'][0],pins['O'][0]); buffers.append(name); consumed.add(name)
    base_luts=duplicate_luts(base)
    result_luts=duplicate_luts(result)
    lookup, conflicts = mapping(base,result)
    signatures = collections.defaultdict(list)
    for name,(kind,params,pins) in base.cells.items():
        if kind == 'FDRE': signatures[(params,pin_norm(base,{p:b for p,b in pins.items() if p!='Q'}))].append(name)
    pending = {n for n in added-consumed if result.cells[n][0]=='FDRE' and '_psdsp' not in n}
    for iteration in range(20):
        progress = []
        for name in sorted(pending):
            _,params,pins = result.cells[name]
            sig = (params,pin_norm(result,{p:b for p,b in pins.items() if p!='Q'},lookup))
            candidates = signatures.get(sig,[])
            if not candidates: continue
            chosen = next((n for n in candidates if n in result.cells),None)
            if chosen is None: continue
            oq = result.cells[chosen][2]['Q']; nq = pins['Q']
            if len(oq)!=1 or len(nq)!=1: raise ValueError('Non-scalar FDRE Q')
            result.pool.union(oq[0],nq[0]); progress.append(name)
            replicas.append(dict(replica=name,original=chosen,rule='identical INIT/inversion parameters and C CE D R equations'))
        pending.difference_update(progress); consumed.update(progress)
        lookup, conflicts = mapping(base,result)
        if not progress: break
    # DSP AREG=1 -> AREG=0 with explicit FDREs. Verify all 30 A bits,
    # including repeated sign-extension bits, rather than only counting FFs.
    q_drivers = collections.defaultdict(list)
    for name in added-consumed:
        kind,params,pins = result.cells[name]
        if kind=='FDRE': q_drivers[result.pool.root(pins['Q'][0])].append(name)
    accepted_dsp = set()
    for name in sorted(set(base.cells)&set(result.cells)):
        ak,ap,ai = base.cells[name]; bk,bp,bi = result.cells[name]
        if ap == bp or ak!='DSP48E2' or bk!=ak: continue
        ap,bp = dict(ap),dict(bp)
        changes = {p:(ap.get(p),bp.get(p)) for p in ap.keys()|bp.keys() if ap.get(p)!=bp.get(p)}
        reasons = []; regs = set(); checks = []
        if changes != {'AREG':('1','0'),'ACASCREG':('1','0')}: reasons.append('unexpected parameter changes')
        if ap.get('A_INPUT')!='"DIRECT"' or ap.get('AMULTSEL')!='"A"': reasons.append('unsupported DSP input mode')
        for p,default in [('IS_CLK_INVERTED',"1'b0"),('IS_RSTA_INVERTED',"1'b0"),('IS_INMODE_INVERTED',"5'b00000")]:
            if ap.get(p,default)!=default: reasons.append('unsupported inversion '+p)
        if ai['CEA2']!=(1,) or ai['RSTA']!=(0,) or bi['CEA2']!=(0,): reasons.append('unsupported enable/reset')
        if any(ai['INMODE']): reasons.append('INMODE not constant zero')
        if len(ai['A'])!=30 or len(bi['A'])!=30: reasons.append('A width is not 30')
        for bit,(old,new) in enumerate(zip(ai['A'],bi['A'])):
            drivers = q_drivers.get(result.pool.root(new),[])
            ok = False; chosen = None
            for candidate in drivers:
                _,rp,ri = result.cells[candidate]
                if rp!=parameters('FDRE',{'INIT':"1'b0"}.items()): continue
                def matches(port,expected):
                    return tuple(lookup.get(result.pool.root(x)) for x in ri[port])==tuple(base.pool.root(x) for x in expected)
                if matches('D',(old,)) and matches('C',ai['CLK']) and ri['CE']==(1,) and ri['R']==(0,):
                    ok=True;chosen=candidate;regs.add(candidate);break
            checks.append(dict(a_bit=29-bit,verified=ok,external_register=chosen))
            if not ok: reasons.append('A bit %s unmatched'%(29-bit))
        item=dict(instance=name,parameter_changes=changes,verified=not reasons,reasons=reasons,bit_checks=checks,unique_registers=len(regs))
        dsp_checks.append(item)
        if not reasons: accepted_dsp.add(name); consumed.update(regs)
    counts = collections.Counter(); differences = [];lut_rewrites=[]
    with (output/'remaining_differences.jsonl').open('w') as stream:
        for name in sorted(set(base.cells)&set(result.cells)):
            ak,ap,ai = base.cells[name]; bk,bp,bi = result.cells[name]
            problems = []
            if (re.fullmatch('LUT[1-6]',ak) and re.fullmatch('LUT[1-6]',bk)
                    and (ak!=bk or ap!=bp or pin_norm(base,ai)!=pin_norm(result,bi,lookup))):
                af=lut_function(base,ak,ap,ai);bf=lut_function(result,bk,bp,bi,lookup)
                if af is not None and af==bf:
                    lut_rewrites.append(dict(instance=name,old_type=ak,new_type=bk,support_bits=len(af[0]),truth_table=hex(af[1])))
                    ak=bk='LUT_FUNCTION_VERIFIED';ap=bp=()
                    ai={'O':ai['O']};bi={'O':bi['O']}
            if ak!=bk: problems.append('type')
            if name in accepted_dsp:
                ad,bd = dict(ap),dict(bp)
                for p in ('AREG','ACASCREG'): ad.pop(p);bd.pop(p)
                ap,bp = tuple(sorted(ad.items())),tuple(sorted(bd.items()))
                ai={p:b for p,b in ai.items() if p not in ('A','CEA2')};bi={p:b for p,b in bi.items() if p not in ('A','CEA2')}
            if ap!=bp: problems.append('parameters')
            a_norm,b_norm = dict(pin_norm(base,ai)),dict(pin_norm(result,bi,lookup))
            changed = [p for p in a_norm.keys()|b_norm.keys() if a_norm.get(p)!=b_norm.get(p)]
            if changed: problems.append('connections')
            if problems:
                counts.update(problems); counts['different_cells']+=1
                record=dict(instance=name,type=ak,problems=problems,ports=changed,
                            old={p:[base.pool.names[base.pool.root(b)] for b in ai.get(p,())] for p in changed},
                            new={p:[result.pool.names[result.pool.root(b)] for b in bi.get(p,())] for p in changed})
                stream.write(json.dumps(record)+'\n')
                if len(differences)<15: differences.append(record)
            else: counts['matched_cells']+=1
    port_mismatches=[]
    if base.top_ports.keys()!=result.top_ports.keys(): port_mismatches.append('port names differ')
    for name,(direction,bits) in base.top_ports.items():
        rd,rb = result.top_ports[name]
        if direction!=rd or tuple(base.pool.root(b) for b in bits)!=tuple(lookup.get(result.pool.root(b)) for b in rb): port_mismatches.append(name)
    unresolved_added=sorted(added-consumed)
    report=dict(scope='Flattened visible functional logic; encrypted module bodies are opaque boundaries',
                assumptions=['binary functional behavior after common GSR release and initialization',
                             'zero physical delay and valid synchronous timing',
                             'opaque module behavior is identical between checkpoints; not proven by this checker'],
                base_flat_cells=len(base.cells),result_flat_cells=len(result.cells),opaque_references=dict(result.opaque),
                buffer_rewrites=buffers,replica_rewrites=replicas,dsp_rewrites=dsp_checks,
                duplicate_logic_witnesses={'base':base_luts,'result':result_luts},
                lut_truth_table_rewrites=lut_rewrites,
                counts=dict(counts),unresolved_added=unresolved_added,removed_cells=sorted(removed),
                net_mapping_conflicts=conflicts,top_port_mismatches=port_mismatches,remaining_examples=differences,
                visible_structural_certificate_passed=not(counts['different_cells'] or unresolved_added or removed or conflicts or port_mismatches),
                full_design_formal_equivalence_proven=False)
    (output/'rewrite_verification.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    source, output = map(lambda p:Path(p).resolve(),sys.argv[1:3])
    output.mkdir(parents=True,exist_ok=False)
    shutil.copy2(__file__,output/Path(__file__).name)
    shutil.copy2(Path(__file__).with_name('compare_minimap2_functional_netlists.py'),output/'compare_minimap2_functional_netlists.py')
    started=time.monotonic()
    manifest=dict(source=str(source),source_manifest_sha256=hashlib.sha256((source/'manifest.json').read_bytes()).hexdigest(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),checkpoints=json.loads((source/'manifest.json').read_text())['checkpoints'])
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    base=Design(source/'input/functional.v');print('INPUT_FLATTENED',len(base.cells),dict(base.opaque),flush=True)
    for label in ('amf3','native'):
        result=Design(source/label/'functional.v');print(label,'FLATTENED',len(result.cells),flush=True)
        dest=output/label;dest.mkdir()
        report=verify(base,result,dest)
        print(label,{k:report[k] for k in ('counts','visible_structural_certificate_passed')},'buffers',len(report['buffer_rewrites']),'replicas',len(report['replica_rewrites']),flush=True)
        del result
    manifest['elapsed_seconds']=time.monotonic()-started
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


if __name__=='__main__':main()
