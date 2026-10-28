import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from run_dfx_multi import validate, settings, check_shell

class MultiContract(unittest.TestCase):
    def setUp(self):
        self.job=dict(IncremenralCompilation=True,clock_period_ns=10,require_distinct_slrs=True,part='p',clock_name='clk',
            partitions=[dict(id='a',active_partition='top/a',pblock='pa',pblock_ranges=['CLOCKREGION_X1Y1:CLOCKREGION_X1Y1'],expected_slr='SLR0'),
                        dict(id='b',active_partition='top/b',pblock='pb',pblock_ranges=['CLOCKREGION_X1Y5:CLOCKREGION_X1Y5'],expected_slr='SLR1')])
    def test_valid(self): self.assertIs(validate(self.job),self.job)
    def test_duplicate_and_nested_rejected(self):
        for field,value in [('id','a'),('active_partition','top/a'),('active_partition','top/a/sub'),('pblock','pa')]:
            job=copy.deepcopy(self.job);job['partitions'][1][field]=value
            with self.assertRaises(ValueError):validate(job)
    def test_bad_range_and_disabled(self):
        job=copy.deepcopy(self.job);job['partitions'][0]['pblock_ranges']=['[exec bad]']
        with self.assertRaises(ValueError):validate(job)
        self.job['IncremenralCompilation']=False
        with self.assertRaises(ValueError):validate(self.job)
    def test_nested_tcl_lists_and_metacharacters(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'inputs').mkdir();self.job['rtl_sources']=['/tmp/a $x [error bad].v']
            s=settings(self.job,p)
            script='source '+str(s)+'\nputs [llength $dfx(rps)]\nputs [lindex [lindex $dfx(rps) 1] 1]\nputs [lindex $dfx(rtl_sources) 0]\n'
            value=subprocess.check_output(['tclsh'],input=script,text=True)
            self.assertEqual(value,'2\ntop/b\n/tmp/a $x [error bad].v\n')
    def test_parent_identity_and_hash_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);self.job['shell_run']=tmp
            m=dict(schema='amf-dfx-multi-shell-v1',state='completed',job=copy.deepcopy(self.job),outputs={})
            (p/'manifest.json').write_text(json.dumps(m));self.assertEqual(check_shell(self.job)[0],p)
            m['job']['clock_period_ns']=9;(p/'manifest.json').write_text(json.dumps(m))
            with self.assertRaises(ValueError):check_shell(self.job)
    def test_no_global_reroute_in_assembler(self):
        p=Path(__file__).resolve().parents[2]/'scripts/dfx_multi_assemble.tcl'
        lines=[x.strip() for x in p.read_text().splitlines() if not x.lstrip().startswith('#')]
        self.assertFalse(any('place_design' in x or 'route_design' in x for x in lines))
    def test_shared_net_audit_is_partition_scoped(self):
        helper=Path(__file__).resolve().parents[2]/'scripts/dfx_multi_common.tcl'
        script='source {'+str(helper)+'}\n'+r'''
proc get_property {property object} {return $object}
set footprint [dict create INT_A 1]
set before [amf_multi::footprint_resources {INT_A/CTRL INT_A/VCC INT_B/OLD} $footprint]
set other_change [amf_multi::footprint_resources {INT_A/CTRL INT_A/VCC INT_B/NEW} $footprint]
set own_change [amf_multi::footprint_resources {INT_A/NEW INT_A/VCC INT_B/OLD} $footprint]
puts [expr {$before eq $other_change}]
puts [expr {$before eq $own_change}]
'''
        self.assertEqual(subprocess.check_output(['tclsh'],input=script,text=True),'1\n0\n')

if __name__=='__main__': unittest.main()
