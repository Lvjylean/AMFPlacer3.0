"""Fixed-graph tests of the real Tcl selector; no whole-design extraction mock."""
from pathlib import Path
import subprocess
import random
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def word(s):
    s=str(s)
    for a,b in [('\\','\\\\'),('"','\\"'),('$','\\$'),('[','\\['),(']','\\]'),('\n','\\n')]:s=s.replace(a,b)
    return '"'+s+'"'


def lst(items):return ' '.join(word(x) for x in items)


class ScopedInput(unittest.TestCase):
    def run_graph(self, types, edges, active, dsp_preg=1, export=False):
        rows={c:[] for c in types}; nets={}
        for i,(src,dst) in enumerate(edges):
            n='n'+str(i);s=src.rsplit('/',1)[0];d=dst.rsplit('/',1)[0]
            rows[s].append([src,src.rsplit('/',1)[1],'OUT',n,0])
            rows[d].append([dst,dst.rsplit('/',1)[1],'IN',n,0])
            nets[n]=[src,types[s],lst([dst])]
        # Clocks must not invoke net() or pull the global clock fanout into a cone.
        for c,t in types.items():
            if t in ('FDRE','DSP48E2','SRL16E'):
                rows[c].append([c+'/CLK','CLK','IN','global_clock',1])
        script='source '+word(ROOT/'scripts/dfx_input_scope.tcl')+'\nset dfx(dsp_critical) 1\n'
        script+='namespace eval amf_dfx {proc active {c} {return [string match {top/rm/*} $c]}\nproc timed {label script} {uplevel 1 $script}}\n'
        script+='set ::testTypes [dict create '+lst([x for c,t in types.items() for x in (c,t)])+']\n'
        script+='set ::testRows [dict create '+lst([x for c,rs in rows.items() for x in (c,lst([lst(r) for r in rs]))])+']\n'
        script+='set ::testNets [dict create '+lst([x for n,r in nets.items() for x in (n,lst(r))])+']\n'
        script+='rename amf_dfx_scope::type amf_dfx_scope::real_type\nproc amf_dfx_scope::type {c} {dict get $::testTypes $c}\n'
        script+='rename amf_dfx_scope::pins amf_dfx_scope::real_pins\nproc amf_dfx_scope::pins {c} {dict get $::testRows $c}\n'
        script+='rename amf_dfx_scope::net amf_dfx_scope::real_net\nproc amf_dfx_scope::net {n {withSinks 0}} {dict get $::testNets $n}\n'
        script+='proc get_cells {c} {return $c}\n'
        script+=f'proc get_property {{key c}} {{switch $key {{PREG {{return {dsp_preg}}} LOC {{return SITE}} BEL {{return BEL}} IS_CLK_INVERTED {{return 0}} IS_LOC_FIXED {{return 0}} IS_BEL_FIXED {{return 0}} default {{error "Unexpected property $key"}}}}}}\n'
        script+='set chosen [amf_dfx_scope::select '+word(lst(active))+']\n'
        script+='foreach c [lsort [dict keys $chosen]] {puts "CELL $c"}\nforeach p [lsort [dict keys $amf_dfx_scope::keep]] {puts "PIN $p"}\n'
        # Querying an unsupported unrelated primitive would fail include().
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'case.tcl';f.write_text(script)
            got=subprocess.run(['tclsh',str(f)],text=True,capture_output=True)
            self.assertEqual(got.returncode,0,got.stderr+'\n'+got.stdout)
            chosen={s[5:] for s in got.stdout.splitlines() if s.startswith('CELL ')}
            pins={s[4:] for s in got.stdout.splitlines() if s.startswith('PIN ')}
            return chosen,pins

    def test_register_cuts_exclude_unrelated_static_and_endpoint_other_side(self):
        types={'top/rm/a':'LUT2','launch':'FDRE','in_logic':'LUT2','side':'FDRE',
               'out_logic':'LUT2','capture':'FDRE','unrelated':'UNSUPPORTED',
               'before_launch':'LUT1','after_capture':'LUT1'}
        edges=[('launch/Q','in_logic/I0'),('side/Q','in_logic/I1'),('in_logic/O','top/rm/a/I0'),
               ('top/rm/a/O','out_logic/I0'),('side/Q2','out_logic/I1'),('out_logic/O','capture/D'),
               ('before_launch/O','launch/D'),('capture/Q','after_capture/I0')]
        got,pins=self.run_graph(types,edges,['top/rm/a'])
        self.assertEqual(got,{'top/rm/a','launch','in_logic','side','out_logic','capture'})
        self.assertNotIn('launch/D',pins);self.assertNotIn('capture/Q',pins)
        self.assertIn('side/Q2',pins)

    def test_leave_and_reenter_rm_comb_path_is_not_cut_at_module_port(self):
        types={'top/rm/a':'FDRE','top/rm/b':'FDRE','bridge':'LUT1','unrelated':'UNSUPPORTED'}
        edges=[('top/rm/a/Q','bridge/I0'),('bridge/O','top/rm/b/D')]
        got,pins=self.run_graph(types,edges,['top/rm/a','top/rm/b'])
        self.assertEqual(got,{'top/rm/a','top/rm/b','bridge'})
        self.assertTrue({'top/rm/a/Q','bridge/I0','bridge/O','top/rm/b/D'}<=pins)

    def test_constant_net_does_not_expand_or_create_static_cell(self):
        types={'top/rm/a':'FDRE','gnd':'GND','unrelated':'UNSUPPORTED'}
        got,pins=self.run_graph(types,[('gnd/G','top/rm/a/D')],['top/rm/a'])
        self.assertEqual(got,{'top/rm/a'});self.assertIn('top/rm/a/D',pins)

    def test_dsp_preg_zero_is_not_a_register_cut(self):
        types={'top/rm/a':'FDRE','dsp':'DSP48E2','launch':'FDRE'}
        edges=[('launch/Q','dsp/A[0]'),('dsp/P[0]','top/rm/a/D')]
        got,_=self.run_graph(types,edges,['top/rm/a'],dsp_preg=0)
        self.assertEqual(got,set(types))
        got,pins=self.run_graph(types,edges,['top/rm/a'],dsp_preg=1)
        self.assertEqual(got,{'top/rm/a','dsp'});self.assertNotIn('dsp/A[0]',pins)

    def test_dsp_unregistered_output_prevents_cut_even_with_preg(self):
        types={'top/rm/a':'FDRE','dsp':'DSP48E2','launch':'FDRE'}
        got,_=self.run_graph(types,[('launch/Q','dsp/A[0]'),('dsp/ACOUT[0]','top/rm/a/D')],['top/rm/a'])
        self.assertEqual(got,set(types))

    def test_random_graph_retains_every_register_path_through_active_cells(self):
        # Independent path enumeration on the original graph, then on the cut graph.
        # Includes reconvergence, extra fanout and static side inputs.
        for seed in range(20):
            rng=random.Random(seed)
            names=['launch0','launch1']+[('top/rm/' if i in (2,5) else 'static/')+str(i) for i in range(8)]+['capture0','capture1']
            types={n:('FDRE' if n.startswith(('launch','capture')) else 'LUT6') for n in names}
            edges=[]
            for i,a in enumerate(names[:-2]):
                for j in range(max(2,i+1),len(names)):
                    if rng.random()<0.22:edges.append((a+'/O'+str(j),names[j]+'/I'+str(i)))
            active=[n for n in names if n.startswith('top/rm/')]
            chosen,pins=self.run_graph(types,edges,active)
            def paths(allowed):
                adjacency={n:[] for n in names}
                for a,b in allowed:adjacency[a.rsplit('/',1)[0]].append(b.rsplit('/',1)[0])
                result=set()
                def visit(n,path):
                    if len(path)>1 and types[n]=='FDRE':
                        if any(x in active for x in path):result.add(tuple(path))
                        return
                    for v in adjacency[n]:visit(v,path+[v])
                for n in names:
                    if types[n]=='FDRE':visit(n,[n])
                return result
            cut=[(a,b) for a,b in edges if a in pins and b in pins]
            self.assertEqual(paths(edges),paths(cut),seed)

    def test_isolated_rm_requires_no_fixed_context(self):
        got,_=self.run_graph({'top/rm/a':'FDRE','other':'UNSUPPORTED'},[],['top/rm/a'])
        self.assertEqual(got,{'top/rm/a'})


if __name__=='__main__':unittest.main()
