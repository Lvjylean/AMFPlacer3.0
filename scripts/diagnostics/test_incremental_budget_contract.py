#!/usr/bin/env python3
"""Synthetic evidence regression; never launches AMF or Vivado."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('budget', SCRIPT/'evaluate_incremental_budget.py')
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.full, self.inc = self.root/'full', self.root/'inc'
        self.full.mkdir(); self.inc.mkdir()
        self.identity = dict(v1_design_input_sha256='a'*64, placement_constraints_sha256='b'*64, amf_binary_sha256='c'*64, amf_settings_sha256='d'*64)
        self.job = dict(part='xcu250', clock_name='dfx_clk', clock_period_ns=10)
        self.fm = dict(state='completed', job=self.job, comparison_identity=self.identity,
                       acceptance=dict(implementation_verified=True, timing_met=True))
        self.im = dict(self.fm, parallel_amf_placement_timing=dict(scope='placement_only_excluding_export_and_vivado', seconds=70))
        self.evidence(self.full, self.fm, 'whole_design', 100, 110, 1000, 1000)
        for ident in ('a', 'b'):
            p = self.inc/'workers'/ident; p.mkdir(parents=True)
            m = dict(state='completed', acceptance=dict(implementation_verified=True), binary_sha256='c'*64)
            self.evidence(p, m, 'dynamic_only_fixed_static_context', 70, 74, 2000, 700)
        self.save(self.inc/'manifest.json', self.im)

    @staticmethod
    def save(p, v):
        p.write_text(json.dumps(v))

    def evidence(self, p, m, scope, placement, process, start, route):
        m.update(amf_placement_scope=scope, stages=[dict(name='amf',exit_code=0,started_epoch=start,ended_epoch=start+process,seconds=process)])
        self.save(p/'manifest.json',m)
        (p/'amf_profile.tsv').write_text('thread_index\tcategory\tinclusive_wall_s\tself_wall_s\n0\tplacement_orchestration\t'+str(placement+2)+'\t0\n0\texport_vivado\t2\t2\n')
        (p/'stage_intervals.tsv').write_text(f'vivado_place\t0\t9999000\t0\nvivado_route\t{int(start*1000)}\t{int((start+route)*1000)}\t0\n')

    def test_exact_seventy_passes_and_vivado_place_is_excluded(self):
        v=budget.evaluate(self.full,self.inc)
        self.assertTrue(v['performance_passed'])
        self.assertEqual(v['full_placement_seconds'],100)
        self.assertEqual(v['full_vivado_compatibility_placement_seconds'],9999)
        self.assertAlmostEqual(v['parallel_routing']['ratio'],.7)

    def test_over_seventy_fails(self):
        self.im['parallel_amf_placement_timing']['seconds']=70.001
        self.save(self.inc/'manifest.json',self.im)
        self.assertFalse(budget.evaluate(self.full,self.inc)['performance_passed'])

    def test_native_reference_rejected(self):
        self.fm.pop('amf_placement_scope'); self.fm.pop('stages')
        self.save(self.full/'manifest.json',self.fm)
        with self.assertRaisesRegex(ValueError,'scope'):
            budget.evaluate(self.full,self.inc)

    def test_identity_required(self):
        self.im.pop('comparison_identity')
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'identity'):
            budget.evaluate(self.full,self.inc)

    def test_different_design_rejected(self):
        self.im['comparison_identity']=dict(self.identity,v1_design_input_sha256='d'*64)
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'identity'):
            budget.evaluate(self.full,self.inc)

    def test_null_reference_identity_rejected(self):
        self.fm['comparison_identity']=None
        self.save(self.full/'manifest.json',self.fm)
        with self.assertRaisesRegex(ValueError,'identity'):
            budget.evaluate(self.full,self.inc)

    def test_null_incremental_identity_rejected(self):
        self.im['comparison_identity']=None
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'identity'):
            budget.evaluate(self.full,self.inc)

    def test_correctness_required(self):
        self.im['acceptance']=dict(implementation_verified=False)
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'correctness'):
            budget.evaluate(self.full,self.inc)

    def test_process_time_is_not_placement_time(self):
        self.im.pop('parallel_amf_placement_timing')
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'actual parallel placement-phase timing'):
            budget.evaluate(self.full,self.inc)

    def test_invalid_parallel_span_rejected(self):
        self.im['parallel_amf_placement_timing']['seconds']=69
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'shorter than a worker'):
            budget.evaluate(self.full,self.inc)

    def test_incomplete_implementation_rejected(self):
        self.im['state']='failed';self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'incomplete'):
            budget.evaluate(self.full,self.inc)

    def test_worker_binary_mismatch_rejected(self):
        p=self.inc/'workers/a/manifest.json';m=json.loads(p.read_text());m['binary_sha256']='f'*64;self.save(p,m)
        with self.assertRaisesRegex(ValueError,'Worker binary'):budget.evaluate(self.full,self.inc)

    def test_algorithm_settings_mismatch_rejected(self):
        self.im['comparison_identity']=dict(self.identity,amf_settings_sha256='e'*64)
        self.save(self.inc/'manifest.json',self.im)
        with self.assertRaisesRegex(ValueError,'identity'):budget.evaluate(self.full,self.inc)

    def test_invalid_times_rejected(self):
        for x in (float('nan'),float('inf'),-1):
            with self.assertRaises(ValueError):budget.check(x,100)


if __name__=='__main__':
    unittest.main()
