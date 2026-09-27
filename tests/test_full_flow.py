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
from run_boundary_comparison import equivalent_configs, assess_qor
from summarize_full_flow import routing_complete, missing_clock_source_warning, numerical_guard_audit

class ComparisonRecommendation(unittest.TestCase):
    def fixture(self,cluster_wns=-.059,cluster_met=False):
        return {name:dict(summary=dict(implementation_verified=True,
            drc_counts=dict(errors=0,critical_warnings=0),timing_met=met,
            timing=dict(wns_ns=wns,tns_ns=tns))) for name,wns,tns,met in (
                ('control',-.137,-1.708,False),('delay',.003,0,True),
                ('cluster',cluster_wns,0 if cluster_met else -.072,cluster_met))}

    def test_relative_improvement_is_not_timing_closure(self):
        result=assess_qor(self.fixture())
        self.assertEqual(result['qor_assessment']['single_case_preferred_variant'],'delay')
        self.assertTrue(result['qor_assessment']['cluster_setup_nonregression_vs_control'])
        self.assertFalse(result['qor_assessment']['cluster_setup_nonregression_vs_delay'])
        self.assertIn('clustering-remains-experimental',result['recommendation'])

    def test_candidate_review_requires_timing_and_all_implementations_legal(self):
        inputs=self.fixture(.1,True)
        self.assertEqual(assess_qor(inputs)['recommendation'],'candidate-passes-single-case-QoR-review')
        inputs['cluster']['summary']['timing_met']=False  # e.g. a hold failure
        self.assertIn('clustering-remains-experimental',assess_qor(inputs)['recommendation'])
        inputs['cluster']['summary']['drc_counts']['critical_warnings']=1
        self.assertIn('implementation-validation-failed',assess_qor(inputs)['recommendation'])

class FullBackend(unittest.TestCase):
    def test_numerical_guard_events_are_not_unique_cell_counts(self):
        log='QP_GUARD axis=X repaired=2 max_diagonal_delta=0.01 iterations=5 relative_error=0.1 converged=0 rollback=1\nQP_GUARD axis=Y repaired=2 max_diagonal_delta=0.02 iterations=4 relative_error=0 converged=1 rollback=0\nTIMING_WEIGHT_GUARD cap=1000 enhanced_edges=20 saturated_edges=3 invalid_edges=0'
        result=numerical_guard_audit(log)
        self.assertEqual(result['solver_calls'],2)
        self.assertEqual(result['repaired_row_events'],4)
        self.assertEqual(result['rollback_calls'],1)
        self.assertEqual(result['unconverged_calls'],1)
        self.assertEqual(result['saturated_edge_events'],3)
        self.assertEqual(result['maximum_diagonal_delta'],.02)

    def test_comparison_refuses_different_numerical_protection(self):
        configs={name:dict(TimingMaxEnhancement='1000',QPStabilityGuard='true') for name in ('control','delay','cluster')}
        equivalent_configs(configs)
        configs['cluster']['TimingMaxEnhancement']='100'
        with self.assertRaisesRegex(ValueError,'settings differ'):equivalent_configs(configs)

    def test_both_clock_source_warning_codes_are_reported(self):
        self.assertTrue(missing_clock_source_warning('WARNING: [Route 35-197] HD.CLK_SRC missing'))
        self.assertTrue(missing_clock_source_warning('WARNING: [Timing 38-242]'))
        self.assertFalse(missing_clock_source_warning('ordinary routing warning'))

    def test_physical_report_destination_is_explicit(self):
        config={'physical boundary model file':'/tmp/model.tsv'}
        configure_outputs(config,Path('/tmp/experiment'))
        self.assertEqual(config['BoundaryReportDirectory'],'/tmp/experiment/reports/physical')

    def test_comparison_refuses_different_clock_or_input(self):
        configs={name:dict(ClockPeriod='10',PhysicalBoundaryMode='true',BoundaryAwareClustering='false') for name in ('control','delay','cluster')}
        equivalent_configs(configs)
        configs['cluster']['ClockPeriod']='9'
        with self.assertRaisesRegex(ValueError,'settings differ'):equivalent_configs(configs)

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
