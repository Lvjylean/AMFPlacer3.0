import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from run_full_backend import prepare
from run_full_flow import configure_outputs, completed_amf_placement
from summarize_full_flow import routing_complete

class FullBackend(unittest.TestCase):
    def test_route_completion_requires_full_coverage_and_zero_errors(self):
        good = {"routable nets":100, "fully routed nets":100, "nets with routing errors":0}
        self.assertTrue(routing_complete(good))
        self.assertFalse(routing_complete(dict(good, **{"fully routed nets":99})))
        self.assertFalse(routing_complete(dict(good, **{"nets with routing errors":1})))
        self.assertFalse(routing_complete({"nets with routing errors":0}))

    def test_dump_path_is_relative_for_legacy_json_parser(self):
        config = {}
        configure_outputs(config, Path("/tmp/experiment"))
        self.assertFalse(Path(config["DumpCLBPacking"]).is_absolute())
        expanded = Path(config["dumpDirectory"]) / config["DumpCLBPacking"]
        self.assertEqual(str(expanded), "/tmp/experiment/placement/DumpCLBPacking")

    def test_backend_failure_can_reuse_completed_amf_but_not_partial_packing(self):
        with tempfile.TemporaryDirectory() as work:
            r = Path(work)
            (r/'placement').mkdir()
            (r/'manifest.json').write_text(json.dumps({'stages':[{'name':'amf','exit_code':0},{'name':'vivado','exit_code':2}]}))
            (r/'status.json').write_text('{"state":"failed"}')
            self.assertFalse(completed_amf_placement(r))
            (r/'placement/DumpCLBPacking-first-0.tcl').write_text('place_design\nroute_design\n')
            self.assertTrue(completed_amf_placement(r))
            (r/'manifest.json').write_text(json.dumps({'stages':[{'name':'amf','exit_code':-6}]}))
            self.assertFalse(completed_amf_placement(r))

    def fixture(self, root, body):
        for d in ('placement','inputs','reports','scripts'):(root/d).mkdir()
        (root/'scripts/full_backend.tcl').write_text('# backend\n')
        with zipfile.ZipFile(root/'netlist.zip','w') as z:
            z.writestr('allCellPinNet','curCell=> a[0] type=> LUT6\ncurCell=> b type=> SRLC32E\n')
        (root/'config.json').write_text(json.dumps({'vivado extracted design information file':str(root/'netlist.zip')}))
        (root/'manifest.json').write_text('{}')
        (root/'placement/resources.json').write_text('{}')
        (root/'placement/DumpCLBPacking-first-0.tcl').write_text('set result [catch {place_cell {'+body+'}}]\n$errorNum\nplace_design\nroute_design\n')
    def test_import_split_preserves_assignments_and_reports_coverage(self):
        with tempfile.TemporaryDirectory() as work:
            r=Path(work);self.fixture(r,'a[0] SLICE_X0Y0/A6LUT\nb SLICE_X0Y1/H6LUT\n')
            manifest={};prepare(r,r,r,manifest)
            self.assertEqual((r/'placement/requested.tsv').read_text(),'a[0]\tSLICE_X0Y0/A6LUT\nb\tSLICE_X0Y1/H6LUT\n')
            self.assertNotIn('route_design',(r/'placement/import_placement.tcl').read_text())
            coverage=json.loads((r/'reports/amf_coverage.json').read_text())
            self.assertEqual(coverage['assigned_cells'],2)
            self.assertEqual(coverage['missing_by_type'],{})
            self.assertFalse(manifest['placement_source']['assignments_changed'])
    def test_rejected_last_batch_logs_indexed_names_as_data(self):
        for spelling in ('a[0]', r'a\[0]'):
            with self.subTest(spelling=spelling), tempfile.TemporaryDirectory() as work:
                r=Path(work);self.fixture(r,'a[0] SLICE_X0Y0/A6LUT\nb SLICE_X0Y1/H6LUT\n')
                (r/'placement/DumpCLBPacking-first-0.tcl').write_text(
                    'set fo [open {'+str(r/'placement/initialPlacementError')+'} w]\n'
                    'set errorNum 0\n'
                    'set result [catch {place_cell {b SLICE_X0Y1/H6LUT}}]\n'
                    'set result [catch {place_cell {a[0] SLICE_X0Y0/A6LUT}}]\n'
                    'if {$result} {incr errorNum; puts $fo "'+spelling+' SLICE_X0Y0/A6LUT"}\n'
                    '$errorNum\nclose $fo\nplace_design\nroute_design\n')
                prepare(r,r,r,{})
                check=r/'check.tcl'
                check.write_text('proc place_cell {args} {error "deliberately rejected"}\nsource {'+str(r/'placement/import_placement.tcl')+'}\n')
                subprocess.run(['tclsh',str(check)],check=True,capture_output=True,text=True)
                self.assertEqual((r/'placement/initialPlacementError').read_text().strip(),'a[0] SLICE_X0Y0/A6LUT')

    def test_missing_cells_fail_before_vivado_and_keep_coverage_report(self):
        with tempfile.TemporaryDirectory() as work:
            r=Path(work);self.fixture(r,'a[0] SLICE_X0Y0/A6LUT\n')
            with self.assertRaisesRegex(ValueError,'Incomplete AMF BEL export'):
                prepare(r,r,r,{})
            self.assertEqual(json.loads((r/'reports/amf_coverage.json').read_text())['missing_by_type'],{'SRLC32E':1})

    def test_duplicate_output_fails_before_vivado(self):
        with tempfile.TemporaryDirectory() as work:
            r=Path(work);self.fixture(r,'a[0] SLICE_X0Y0/A6LUT\na[0] SLICE_X0Y1/A6LUT\n')
            with self.assertRaises(subprocess.CalledProcessError):prepare(r,r,r,{})

    def test_old_srl_exit_export_is_corrected_and_recorded(self):
        with tempfile.TemporaryDirectory() as work:
            r=Path(work);self.fixture(r,'a[0] SLICE_X0Y0/H6LUT\nb SLICE_X1Y0/H6LUT\n')
            with zipfile.ZipFile(r/'netlist.zip','w') as z:
                z.writestr('allCellPinNet','curCell=> a[0] type=> SRLC32E\ncurCell=> b type=> SRL16E\n'
                           'pin=> b/D refpin=> D dir=> IN net=> n drivepin=> a[0]/Q31\n')
            manifest={};prepare(r,r,r,manifest)
            self.assertIn('a[0]\tSLICE_X0Y0/A6LUT',(r/'placement/requested.tsv').read_text())
            self.assertIn('SLICE_X0Y0/A6LUT',(r/'placement/import_placement.tcl').read_text())
            self.assertIn('SLICE_X0Y0/H6LUT',(r/'placement/DumpCLBPacking-first-0.tcl').read_text())
            self.assertTrue(manifest['placement_source']['assignments_changed'])
            self.assertFalse(manifest['placement_source']['site_assignments_changed'])
            self.assertEqual(json.loads((r/'reports/srl_cascades.json').read_text())['checked'],1)
