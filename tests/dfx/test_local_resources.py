from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from run_dfx import check_prepare_resume, sha


class LocalResourceTables(unittest.TestCase):
    def run_case(self, body, exclusive=0, static=True):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            # Any whole-device site/cell query is an error in this test.
            script = ('source {' + str(ROOT/'scripts/dfx_common.tcl') + '}\n'
                      'set dfx(output) {' + directory + '}\n') + r'''
set dfx(active_partition) top/rm
set dfx(pblock) rp
proc get_pblocks {name} {if {$name ne "rp"} {error "wrong pblock"};return rp}
proc get_sites {args} {
    if {$args ne {-of_objects rp}} {error "whole-device site query"}
    return {SLICE_X1Y1 SLICE_X1Y2 SLICE_X1Y3 SLICE_X1Y4 CLOCK_X1Y1}
}
proc get_cells {args} {
    set expected {-quiet -of_objects {SLICE_X1Y1 SLICE_X1Y2 SLICE_X1Y3 SLICE_X1Y4 CLOCK_X1Y1} -filter {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL}}
    if {$args ne $expected} {error "whole-design occupancy query"}
    return $::local_cells
}
proc get_property {key object} {
    switch $key {
        PROHIBIT {
            if {$object eq "SLICE_X99Y99"} {error "outside-site property query"}
            return [expr {$object eq "SLICE_X1Y2"}]
        }
        EXCLUDE_PLACEMENT {return $::exclusive}
        REF_NAME {if {$object eq "const"} {return VCC};return LUT6}
        LOC {
            if {$object eq "outside"} {return SLICE_X99Y99}
            return SLICE_X1Y3
        }
        default {error "unexpected property $key"}
    }
}
set modeled {SLICE_X1Y1 1 SLICE_X1Y2 1 SLICE_X1Y3 1 SLICE_X1Y4 1 SLICE_X99Y99 1}
'''
            script += f'set exclusive {exclusive}\n'
            script += 'set local_cells {' + ('fixed ' if static else '') + 'top/rm/c const}\n'
            script += body
            (p/'test.tcl').write_text(script)
            result = subprocess.run(['tclsh', str(p/'test.tcl')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout, {x.name:x.read_text() for x in p.iterdir() if x.suffix in ('.txt', '.tsv')}

    def test_local_candidates_match_full_scan_semantics(self):
        _, files = self.run_case('amf_dfx::resource_tables $dfx(output) $modeled\n')
        allowed = set(files['allowed_sites.txt'].splitlines())
        blocked = set(files['blocked_sites.txt'].splitlines())
        # Old whole-device oracle also includes an occupied outside site.
        old_blocked = {'SLICE_X1Y2', 'SLICE_X1Y3', 'SLICE_X99Y99'}
        self.assertEqual(allowed-blocked, allowed-old_blocked)
        self.assertEqual(allowed-blocked, {'SLICE_X1Y1', 'SLICE_X1Y4'})
        self.assertNotIn('SLICE_X99Y99', blocked)
        self.assertIn('static_cells\t1', files['resource_scope.tsv'])

    def test_exclusive_empty_region_preserves_local_prohibit(self):
        _, files = self.run_case('amf_dfx::resource_tables $dfx(output) $modeled\n', exclusive=1, static=False)
        self.assertEqual(files['blocked_sites.txt'].strip(), 'SLICE_X1Y2')
        self.assertIn('static_cells\t0', files['resource_scope.tsv'])

    def test_exclusive_static_occupancy_fails(self):
        out, _ = self.run_case(r'''
if {![catch {amf_dfx::resource_tables $dfx(output) $modeled} msg]} {error "occupancy accepted"}
puts $msg
''', exclusive=1)
        self.assertIn('Exclusive Pblock contains 1 static primitives', out)

    def test_unexpected_outside_occupancy_fails(self):
        out, _ = self.run_case(r'''
set local_cells outside
if {![catch {amf_dfx::resource_tables $dfx(output) $modeled} msg]} {error "outside query accepted"}
puts $msg
''')
        self.assertIn('outside site', out)

    def test_no_modeled_resources_fails(self):
        out, _ = self.run_case(r'''
if {![catch {amf_dfx::resource_tables $dfx(output) {}} msg]} {error "empty domain accepted"}
puts $msg
''', static=False)
        self.assertIn('no modeled fabric sites', out)


class LinkedRecovery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.job = dict(part='part', active_partition='top/rm', pblock='rp',
                        clock_name='clk', clock_period_ns=10, vivado='/tool/vivado',
                        static_placement_audit='skip', prepare_from_run=str(self.root))
        for k in ('static_checkpoint', 'reference_checkpoint', 'rm_checkpoint', 'amf_config'):
            p=self.root/(k+'.input');p.write_text(k);self.job[k]=str(p)
        self.old = dict(schema='amf-dfx-run-v1', state='failed', job=dict(self.job),
                        input_hashes={k:sha(self.job[k]) for k in ('static_checkpoint','reference_checkpoint','rm_checkpoint','amf_config')})
        self.save_parent()
        (self.root/'stage_times.tsv').write_text('link_rm\t10\t0\nactive_opt\t20\t0\nwrite_linked_dcp\t5\t0\n')
        (self.root/'linked.dcp').write_bytes(b'test checkpoint')
        (self.root/'shell_boundary.tcldata').write_text('port {IN tile}')
    def save_parent(self):
        (self.root/'manifest.json').write_text(json.dumps(self.old))
        (self.root/'status.json').write_text(json.dumps(dict(state=self.old['state'])))
    def tearDown(self): self.temp.cleanup()
    def test_reuses_completed_checkpoint_and_records_inherited_cost(self):
        got=check_prepare_resume(self.job)
        self.assertFalse(got['link_and_opt_rerun'])
        self.assertEqual(got['inherited_stage_seconds']['active_opt'],20)
        self.assertNotIn('static_snapshot.tcldata',got['files'])
    def test_live_parent_rejected(self):
        self.old['state']='running';self.save_parent()
        with self.assertRaisesRegex(ValueError,'stopped'):check_prepare_resume(self.job)
    def test_changed_rm_rejected(self):
        Path(self.job['rm_checkpoint']).write_text('different netlist')
        with self.assertRaisesRegex(ValueError,'hash mismatch'):check_prepare_resume(self.job)
    def test_changed_pblock_rejected(self):
        self.job['pblock']='elsewhere'
        with self.assertRaisesRegex(ValueError,'mismatch: pblock'):check_prepare_resume(self.job)
    def test_incomplete_checkpoint_rejected(self):
        (self.root/'stage_times.tsv').write_text('write_linked_dcp\t5\t1\n')
        with self.assertRaisesRegex(ValueError,'not completed'):check_prepare_resume(self.job)
    def test_full_audit_requires_original_snapshot(self):
        self.job['static_placement_audit']='full'
        with self.assertRaisesRegex(ValueError,'static_snapshot'):check_prepare_resume(self.job)


if __name__ == '__main__': unittest.main()

