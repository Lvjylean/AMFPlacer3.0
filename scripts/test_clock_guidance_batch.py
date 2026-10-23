"""Complete-vector checking must reject corruption without dropping empty LOCs."""
import json,subprocess,sys,tempfile,unittest
from pathlib import Path
S=Path(__file__).resolve().parent
from dfx_compare_clock_route import graph

class RouteTest(unittest.TestCase):
    def test_branch_order(self):
        self.assertEqual(graph('{A B {C D} {E F}}'),graph('{A B {E F} {C D}}'))
    def test_same_nodes_different_edges(self):
        a,b=graph('{A B {C D} {E F}}'),graph('{A B {C F} {E D}}')
        self.assertEqual(a[0],b[0]);self.assertNotEqual(a[1],b[1])
    def test_roots_and_multiplicity(self):
        self.assertNotEqual(graph('{A B} {C D}'),graph('{C A} {B D}'))
        self.assertNotEqual(graph('{A {B} {B}}'),graph('{A {B}}'))
    def test_reject_malformed(self):
        for text in ('{A B', 'A B}', '', '{A "B"}', '{A B\\ C}'):
            with self.assertRaises(ValueError):graph(text)
    def test_cli_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);a=p/'before';b=p/'after';r=p/'result.json'
            a.write_text('{A B {C D} {E F}}');b.write_text('{A B {C F} {E D}}')
            ret=subprocess.run([sys.executable,str(S/'dfx_compare_clock_route.py'),str(a),str(b),str(r)],capture_output=True,text=True)
            self.assertNotEqual(ret.returncode,0)
            self.assertFalse(json.loads(r.read_text())['passed'])

class BatchTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def vector(self,name,rows):
        p=self.root/name
        for i,suffix in enumerate(('names','locs','bels')):
            Path(str(p)+'.'+suffix).write_text('\n'.join(r[i] for r in rows)+'\n')
        Path(str(p)+'.count').write_text(str(len(rows))+'\n')
        return p
    def compare(self,a,b):
        return subprocess.run([sys.executable,str(S/'dfx_compare_placement.py'),str(a),str(b),str(self.root/'check')],capture_output=True,text=True)
    def test_reordered_and_empty_values(self):
        rows=[('cell[1]','SLICE_X0Y0','AFF'),('unplaced','',''),('尾部','','')]
        self.assertEqual(self.compare(self.vector('a',rows),self.vector('b',rows[::-1])).returncode,0)
        self.assertEqual(json.loads((self.root/'check_placement_summary.json').read_text())['before'],3)
    def test_changed_bel(self):
        a=self.vector('a',[('x','S','A')]);b=self.vector('b',[('x','S','B')])
        self.assertNotEqual(self.compare(a,b).returncode,0)
    def test_changed_inventory(self):
        a=self.vector('a',[('x','S','A')]);b=self.vector('b',[('y','S','A')])
        self.assertNotEqual(self.compare(a,b).returncode,0)
    def test_duplicate(self):
        rows=[('x','S','A'),('x','S','A')]
        self.assertNotEqual(self.compare(self.vector('a',rows),self.vector('b',rows)).returncode,0)
    def test_truncation(self):
        a=self.vector('a',[('x','S','A')]);b=self.vector('b',[('x','S','A')])
        Path(str(b)+'.locs').write_text('')
        self.assertNotEqual(self.compare(a,b).returncode,0)
    def test_tcl_complete_vector_and_fixed_clock_checks(self):
        script=r'''
set dfx(output) [lindex $argv 0]
source [lindex $argv 1]
proc get_cells {args} {return {a b c}}
proc get_nets {args} {
    if {[lsearch -exact $args -top_net_of_hierarchical_group]<0} {error "Clock hierarchy not canonicalized"}
    return {clock1 clock2}
}
proc get_property {property objects} {
    if {$objects eq {a b c}} {
        switch $property {NAME {return {a b c}} LOC {return {SITE {} {}}} BEL {return {AFF {} {}}}}
    }
    switch $property {
        IS_ROUTE_FIXED {set flags {};foreach x $objects {lappend flags [expr {$x eq "clock1"}]};return $flags}
        NAME {return $objects}
        FIXED_ROUTE {return {NODE1 NODE2}}
    }
    error "Unexpected mock query"
}
set before [dfx_clock_guidance::placement]
set after [dfx_clock_guidance::placement]
puts [exec python3 $dfx_clock_guidance::comparator [lindex $before 0] [lindex $after 0] $dfx(output)/tcl_check]
set fixed [dfx_clock_guidance::fixed_clocks]
if {[dict size $fixed]!=1} {error "Nonfixed clock included"}
dfx_clock_guidance::exact $fixed $fixed fixed
if {![catch {dfx_clock_guidance::exact $fixed {clock1 {NODE1 NODE3}} fixed}]} {error "Changed fixed route accepted"}
'''
        p=self.root/'mock.tcl';p.write_text(script)
        ret=subprocess.run(['tclsh',str(p),str(self.root),str(S/'dfx_clock_guidance.tcl')],capture_output=True,text=True)
        self.assertEqual(ret.returncode,0,ret.stdout+ret.stderr)
        self.assertEqual(json.loads((self.root/'tcl_check_placement_summary.json').read_text())['before'],3)

if __name__=='__main__':unittest.main()
