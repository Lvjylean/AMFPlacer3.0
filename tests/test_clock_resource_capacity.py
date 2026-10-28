import csv
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from clock_resource_capacity import (build_table, report_track_capacities,
                                     select_rule, validate_capacity_inputs,
                                     validate_capacity_binary, digest, prepare)


class ClockResourceCapacityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.raw = self.root / 'raw'
        self.raw.mkdir()
        self.out = self.root / 'model'
        self.device = self.root / 'device.zip'
        self.part = 'xcu250-figd2104-2L-e'
        self.rules = ROOT / 'configs/architecture/clock-resource-rules.json'
        (self.raw / 'metadata.tsv').write_text(
            f'part\t{self.part}\nvivado\t2024.2\navailability_source\tempty-device-design\n')
        (self.raw / 'clock_metadata.tsv').write_text(
            f'schema_version\t1\npart\t{self.part}\nfamily\tvirtexuplus\n'
            'vivado\t2024.2\navailability_source\tempty-device-design\nplacement_run_by_exporter\t0\n')
        (self.raw / 'clock_regions.tsv').write_text('clock_region\tslr\nX0Y0\t0\nX0Y1\t1\n')
        lines = []
        with (self.raw / 'sites.tsv').open('w') as f:
            writer = csv.writer(f, delimiter='\t')
            writer.writerow(['site', 'clock_region', 'slr', 'site_type'])
            for y in range(120):
                name, slr = f'SLICE_X0Y{y}', y // 60
                cr = f'X0Y{slr}'
                writer.writerow([name, cr, slr, 'SLICEL'])
                lines.append(f'site=> {name} tile=> CLE_X0Y{y} clockRegionName=> {cr} '
                             f'sitetype=> SLICEL slr=> {slr} prohibited=> 0')
        with zipfile.ZipFile(self.device, 'w') as z:
            z.writestr('exportSiteLocation', '\n'.join(lines) + '\n')

    def build(self):
        return build_table(self.raw, self.device, self.part, self.rules, self.out)

    def report(self, capacity=24, omit=False):
        text = '| | HROUTES | HDISTRS | VROUTES | VDISTRS |\n'
        for cr in ['X0Y0'] if omit else ['X0Y0', 'X0Y1']:
            text += '| ' + cr + ' | ' + ' | '.join(['0', str(capacity), '0.00'] * 4) + ' |\n'
        (self.raw / 'preplacement_clock_utilization.rpt').write_text(text)

    def test_rule_fallback_is_explicit_and_binds_input(self):
        table = self.build()
        self.assertEqual(table['resource_state'], 'nominal')
        self.assertEqual(len(table['regions']), 2)
        self.assertEqual(table['regions'][1]['slr'], 1)
        self.assertEqual(table['regions'][1]['half_column_limit'], 12)
        self.assertIn('no-preplacement-track-table', table['track_capacity_source'])
        self.assertIsNone(table['design_occupancy'])
        self.assertFalse(table['clock_routability_verified'])
        validate_capacity_inputs(self.out / 'clock_capacity.tsv', self.device, self.part)
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.build()

    def test_report_availability_is_verified(self):
        self.report()
        table = self.build()
        self.assertIn('vivado-report-availability', table['track_capacity_source'])
        self.assertEqual(table['regions'][0]['nominal_tracks']['vroute'], 24)

    def test_used_is_not_capacity(self):
        self.report()
        p = self.raw / 'preplacement_clock_utilization.rpt'
        p.write_text(p.read_text().replace('0 | 24 | 0.00', '32 | 24 | 133.33'))
        self.assertEqual(report_track_capacities(p)['X0Y0']['hroute'], 24)

    def test_report_rule_conflict_fails_before_writing(self):
        self.report(capacity=18)
        with self.assertRaisesRegex(ValueError, 'disagree'):
            self.build()
        self.assertFalse(self.out.exists())

    def test_partial_report_is_not_silently_filled(self):
        self.report(omit=True)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.build()

    def test_changed_report_column_order_is_rejected(self):
        self.report()
        p = self.raw / 'preplacement_clock_utilization.rpt'
        p.write_text(p.read_text().replace('HROUTES | HDISTRS', 'HDISTRS | HROUTES'))
        with self.assertRaisesRegex(ValueError, 'column order'):
            self.build()

    def test_old_or_replaced_binary_cannot_silently_ignore_table(self):
        build = self.root / 'build'
        build.mkdir()
        binary = build / 'AMFPlacer'
        binary.write_bytes(b'fixture executable identity')
        manifest = self.root / 'manifest.json'
        with self.assertRaisesRegex(ValueError, 'recorded AMF build'):
            validate_capacity_binary(binary)
        record = dict(state='completed', binaries={'AMFPlacer': digest(binary)})
        manifest.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'verified clock capacity support'):
            validate_capacity_binary(binary)
        record['capabilities'] = {'schema': 'amf-capabilities-v1', 'clock_resource_capacity_schema': 1}
        manifest.write_text(json.dumps(record))
        self.assertEqual(validate_capacity_binary(binary)['clock_resource_capacity_schema'], 1)
        binary.write_bytes(b'replaced executable')
        with self.assertRaisesRegex(ValueError, 'verified clock capacity support'):
            validate_capacity_binary(binary)

    def test_external_rules_and_dirty_source_snapshot_are_preserved(self):
        project = self.root / 'project'
        scripts = project / 'scripts'
        scripts.mkdir(parents=True)
        for name in ('amf3.py', 'export_clock_resources.tcl', 'export_fabric_device.tcl'):
            (scripts / name).write_text('fixture source bytes: ' + name)
        external_rules = self.root / 'external-rules.json'
        external_rules.write_bytes(self.rules.read_bytes())
        args = SimpleNamespace(part=self.part, rules=external_rules, device=self.device, raw_dir=self.raw)
        with patch('amf3.git', return_value='fixture'), patch('amf3.stamp', return_value='fixture'), contextlib.redirect_stdout(io.StringIO()):
            prepare(project, args)
        run = project / 'experiments/preflight/clock-capacity-fixture'
        self.assertEqual(json.loads((run / 'status.json').read_text())['state'], 'completed')
        manifest = json.loads((run / 'manifest.json').read_text())
        saved = run / manifest['source_snapshot'][str(external_rules.resolve())]
        self.assertEqual(saved.read_bytes(), external_rules.read_bytes())

    def test_unknown_architecture_has_no_24_12_fallback(self):
        rules = json.loads(self.rules.read_text())
        for part in ['xcsu50p-somepackage-1-e', 'xc7vx690t-ffg1761-2', 'xcvu095-ffva2104-2-e']:
            with self.subTest(part=part), self.assertRaisesRegex(ValueError, 'No unique'):
                select_rule(rules, part)
        with self.assertRaisesRegex(ValueError, 'family'):
            select_rule(rules, self.part, 'spartanusplus')

    def test_input_checkpoint_is_not_nominal_empty_device(self):
        p = self.raw / 'metadata.tsv'
        p.write_text(p.read_text().replace('empty-device-design', 'input-checkpoint'))
        with self.assertRaisesRegex(ValueError, 'empty-device'):
            self.build()

    def test_stale_clock_metadata_is_rejected(self):
        p = self.raw / 'clock_metadata.tsv'
        p.write_text(p.read_text().replace('2024.2', '2023.2'))
        with self.assertRaisesRegex(ValueError, 'version'):
            self.build()

    def test_region_slr_mismatch_is_rejected(self):
        p = self.raw / 'clock_regions.tsv'
        p.write_text(p.read_text().replace('X0Y1\t1', 'X0Y1\t0'))
        with self.assertRaisesRegex(ValueError, 'SLRs'):
            self.build()

    def test_missing_region_is_rejected(self):
        p = self.raw / 'clock_regions.tsv'
        p.write_text('clock_region\tslr\nX0Y0\t0\n')
        with self.assertRaisesRegex(ValueError, 'regions/SLRs'):
            self.build()

    def test_device_fingerprint_and_part_are_enforced(self):
        self.build()
        table = self.out / 'clock_capacity.tsv'
        with self.assertRaisesRegex(ValueError, 'part/state'):
            validate_capacity_inputs(table, self.device, 'xcvu095-ffva2104-2-e')
        with self.assertRaisesRegex(ValueError, 'requires'):
            validate_capacity_inputs(table, self.device, None)
        self.device.write_bytes(self.device.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'different device archive'):
            validate_capacity_inputs(table, self.device, self.part)

    def test_unsupported_half_column_geometry_is_rejected(self):
        # Keep input archive consistent with a hypothetical 59-row column;
        # the architecture assumption must still reject it, not halve by guess.
        p = self.raw / 'sites.tsv'
        p.write_text('\n'.join(l for l in p.read_text().splitlines() if not l.startswith('SLICE_X0Y59\t')) + '\n')
        with zipfile.ZipFile(self.device) as z:
            content = z.read('exportSiteLocation').decode()
        with zipfile.ZipFile(self.device, 'w') as z:
            z.writestr('exportSiteLocation', '\n'.join(l for l in content.splitlines()
                                                      if not l.startswith('site=> SLICE_X0Y59 ')) + '\n')
        with self.assertRaisesRegex(ValueError, 'half-column geometry'):
            self.build()


if __name__ == '__main__':
    unittest.main()
