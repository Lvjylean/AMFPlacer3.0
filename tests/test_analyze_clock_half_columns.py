"""Contract tests using a small, independently constructed device graph.

Two adjacent SLICE columns share a two-leaf lower domain and a three-leaf
upper domain. Neither cardinality is a production architecture constant.
"""
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from analyze_clock_half_columns import analyze
from clock_resource_capacity import build_table, validate_capacity_inputs


class ClockHalfColumnAnalysisTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.raw = self.root / 'topology'
        self.fabric = self.root / 'fabric'
        self.raw.mkdir()
        self.fabric.mkdir()
        self.device = self.root / 'device.zip'
        self.out = self.root / 'analysis'
        self.part = 'xcu250-figd2104-2L-e'
        (self.raw / 'metadata.tsv').write_text(
            'schema\tamf-clock-half-column-topology-v1\n'
            f'part\t{self.part}\nvivado\t2024.2\nstatus\tcompleted\n'
            'placement_executed\t0\nrouting_executed\t0\n'
            'geometry_assumptions\t60 SLICE rows; two 30-row halves\n')
        (self.fabric / 'metadata.tsv').write_text(
            f'part\t{self.part}\nvivado\t2024.2\n')
        (self.fabric / 'clock_regions.tsv').write_text('clock_region\tslr\nX0Y0\t0\n')
        fabric_rows, device_lines = [], []
        for x, kind in ((0, 'SLICEL'), (1, 'SLICEM')):
            for y in range(60):
                site = f'SLICE_X{x}Y{y}'
                fabric_rows.append(dict(site=site, clock_region='X0Y0', slr=0, site_type=kind))
                device_lines.append(f'site=> {site} tile=> CLE_X{x}Y{y} '
                                    f'clockRegionName=> X0Y0 sitetype=> {kind} slr=> 0')
        self.write_table(self.fabric / 'sites.tsv', fabric_rows)
        self.device_text = '\n'.join(device_lines) + '\n'
        self.write_device(self.device_text)
        self.halves, self.pins = [], []
        for x, kind in ((0, 'SLICEL'), (1, 'SLICEM')):
            for low, high, leaves in ((0, 29, ['leaf_A0', 'leaf_A1']),
                                      (30, 59, ['leaf_B0', 'leaf_B1', 'leaf_B2'])):
                checked = []
                for y in (low, high):
                    site = f'SLICE_X{x}Y{y}'
                    suffixes = ('CLK1', 'CLK2') if x == 0 else ('CLK1', 'CLK2', 'LCLK')
                    for suffix in suffixes:
                        pin = site + '/' + suffix
                        checked.append(pin)
                        self.pins.append(dict(site=site, site_type=kind, clock_region='X0Y0',
                                              pin=pin, start_nodes=f'input_{x}_{y}_{suffix}',
                                              leaf_count=len(leaves), leaf_nodes=','.join(leaves)))
                self.halves.append(dict(clock_region='X0Y0', slice_x=x, y_min=low, y_max=high,
                                        site_types=kind, leaf_count=len(leaves),
                                        leaf_nodes=','.join(leaves), checked_pins=','.join(checked)))
        self.sources = [dict(leaf_node=leaf, leaf_site='BUFCE_' + leaf,
                             input_pin='BUFCE_' + leaf + '/CLK_IN', input_node='in_' + leaf,
                             hdistr_count=4, hdistr_nodes='h0,h1,h2,h3',
                             other_upstream_nodes='VCC', output_pins='BUFCE_' + leaf + '/CLK_LEAF')
                        for leaf in ('leaf_A0', 'leaf_A1', 'leaf_B0', 'leaf_B1', 'leaf_B2')]
        self.write_topology()

    @staticmethod
    def write_table(path, records):
        with path.open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(records[0]), delimiter='\t')
            writer.writeheader()
            writer.writerows(records)

    def write_topology(self):
        self.write_table(self.raw / 'half_columns.tsv', self.halves)
        self.write_table(self.raw / 'pin_leaf_reachability.tsv', self.pins)
        self.write_table(self.raw / 'leaf_sources.tsv', self.sources)

    def write_device(self, text):
        with zipfile.ZipFile(self.device, 'w') as archive:
            archive.writestr('exportSiteLocation', text)

    def run_analysis(self, output=None):
        return analyze(self.raw, self.fabric, self.device, output or self.out)

    def assert_invalid(self, message):
        self.write_topology()
        with self.assertRaisesRegex(ValueError, message):
            self.run_analysis()
        self.assertFalse(self.out.exists(), 'Rejected input must not produce a usable capacity artifact')

    def prepare_uniform_capacity_fixture(self, shared_tile_column):
        # The CR format accepts one capacity per region. Give both halves two
        # independent leaves while keeping the upper/lower resource sets disjoint.
        for half in self.halves:
            if half['y_min'] == 30:
                half['leaf_nodes'], half['leaf_count'] = 'leaf_B0,leaf_B1', 2
        for pin in self.pins:
            if 'leaf_B2' in pin['leaf_nodes'].split(','):
                pin['leaf_nodes'], pin['leaf_count'] = 'leaf_B0,leaf_B1', 2
        self.sources = [source for source in self.sources if source['leaf_node'] != 'leaf_B2']
        self.write_topology()
        if shared_tile_column:
            self.write_device(self.device_text.replace('tile=> CLE_X1Y', 'tile=> CLE_X0Y'))
        metadata = self.fabric / 'metadata.tsv'
        metadata.write_text(metadata.read_text() + 'availability_source\tempty-device-design\n')
        (self.fabric / 'clock_metadata.tsv').write_text(
            f'schema_version\t1\npart\t{self.part}\nfamily\tvirtexuplus\n'
            'vivado\t2024.2\navailability_source\tempty-device-design\n'
            'placement_run_by_exporter\t0\n')

    def test_shared_adjacent_columns_count_once_and_upper_lower_stay_independent(self):
        summary = self.run_analysis()
        self.assertEqual(summary['clock_regions'], 1)
        self.assertEqual(summary['slice_half_columns'], 4)
        self.assertEqual(summary['shared_leaf_domains'], 2)
        self.assertEqual(summary['unique_leaf_nodes'], 5)
        self.assertEqual(summary['nominal_leaf_capacities'], [2, 3])
        self.assertEqual(summary['hdistr_sources_per_leaf'], [4])
        self.assertEqual(summary['member_columns_per_domain'], [2])
        self.assertEqual(summary['resource_state'], 'nominal')
        self.assertFalse(summary['clock_routability_verified'])
        self.assertIn('endpoint checks', summary['scope'])
        data = json.loads((self.out / 'half_column_domains.json').read_text())
        actual = {}
        for domain in data['domains']:
            members = domain['slice_half_columns']
            extent = (members[0]['y_min'], members[0]['y_max'])
            actual[extent] = (domain['nominal_leaf_capacity'], {m['slice_x'] for m in members})
        self.assertEqual(actual, {(0, 29): (2, {0, 1}), (30, 59): (3, {0, 1})})
        with (self.out / 'half_column_capacities.tsv').open() as f:
            table = list(csv.DictReader(f, delimiter='\t'))
        self.assertEqual({(r['y_min'], r['y_max'], r['nominal_leaf_capacity']) for r in table},
                         {('0', '29', '2'), ('30', '59', '3')})
        self.assertTrue(all(set(r['member_slice_x'].split(',')) == {'0', '1'} for r in table))

    def test_missing_half_column_cannot_pass_geometric_coverage(self):
        self.halves = [r for r in self.halves if not (r['slice_x'] == 1 and r['y_min'] == 0)]
        self.assert_invalid('cover every fabric SLICE')

    def test_duplicate_half_column_is_rejected(self):
        self.halves.append(self.halves[0].copy())
        self.assert_invalid('Overlapping or duplicate')

    def test_slicem_lclk_is_mandatory_even_if_clk1_clk2_are_present(self):
        self.halves[2]['checked_pins'] = ','.join(
            pin for pin in self.halves[2]['checked_pins'].split(',') if not pin.endswith('/LCLK'))
        self.assert_invalid('endpoint clock-pin checks')

    def test_claimed_pin_check_requires_its_raw_evidence(self):
        self.pins = [r for r in self.pins if r['pin'] != 'SLICE_X1Y0/LCLK']
        self.assert_invalid('Endpoint pin evidence disagrees')

    def test_equal_leaf_count_with_different_reachable_nodes_is_rejected(self):
        self.pins[0]['leaf_nodes'] = 'leaf_A0,leaf_B0'
        self.assert_invalid('Endpoint pin evidence disagrees')

    def test_partially_overlapping_leaf_domains_are_not_counted_as_independent(self):
        self.halves[2]['leaf_nodes'] = 'leaf_A0,leaf_B0'
        # Keep the pin evidence consistent: the failure must concern resource
        # sharing, not a mismatched summary/evidence pair.
        changed = set(self.halves[2]['checked_pins'].split(','))
        for pin in self.pins:
            if pin['pin'] in changed:
                pin['leaf_nodes'] = 'leaf_A0,leaf_B0'
        self.assert_invalid('Partially overlapping leaf sets')

    def test_one_leaf_domain_cannot_silently_drop_a_second_vertical_extent(self):
        self.halves[1]['leaf_nodes'] = 'leaf_A0,leaf_A1'
        self.halves[1]['leaf_count'] = 2
        changed = set(self.halves[1]['checked_pins'].split(','))
        for pin in self.pins:
            if pin['pin'] in changed:
                pin['leaf_nodes'], pin['leaf_count'] = 'leaf_A0,leaf_A1', 2
        self.assert_invalid('inconsistent vertical extent')

    def test_wrong_part_or_vivado_version_is_rejected(self):
        metadata = self.raw / 'metadata.tsv'
        original = metadata.read_text()
        for old, new in ((self.part, 'xcvu095-ffva2104-2-e'), ('2024.2', '2023.2')):
            with self.subTest(mismatch=new):
                metadata.write_text(original.replace(old, new))
                self.assert_invalid('part or Vivado version differs')

    def test_archive_site_identity_must_match_the_queried_fabric(self):
        self.write_device(self.device_text.replace(
            'SLICE_X1Y0 tile=> CLE_X1Y0 clockRegionName=> X0Y0 sitetype=> SLICEM',
            'SLICE_X1Y0 tile=> CLE_X1Y0 clockRegionName=> X0Y0 sitetype=> SLICEL'))
        self.assert_invalid('Device site disagrees')

    def test_device_and_raw_source_hashes_bind_exact_input_bytes(self):
        first = self.run_analysis()
        original_hash = hashlib.sha256(self.device.read_bytes()).hexdigest()
        self.assertEqual(first['device_sha256'], original_hash)
        evidence = self.raw / 'pin_leaf_reachability.tsv'
        self.assertEqual(first['sources'][str(evidence.resolve())],
                         hashlib.sha256(evidence.read_bytes()).hexdigest())
        # ZIP comments change archive identity without changing any site. The
        # next artifact must bind the actual new bytes, not a geometry-only ID.
        with zipfile.ZipFile(self.device, 'a') as archive:
            archive.comment = b'independent archive identity'
        second = self.run_analysis(self.root / 'analysis_second_archive')
        self.assertNotEqual(first['device_sha256'], second['device_sha256'])
        self.assertEqual(second['device_sha256'], hashlib.sha256(self.device.read_bytes()).hexdigest())
        self.assertEqual(first['nominal_leaf_capacities'], second['nominal_leaf_capacities'])

    def test_placed_or_failed_query_is_not_accepted_as_completed_nominal_data(self):
        metadata = self.raw / 'metadata.tsv'
        original = metadata.read_text()
        for old, new in (('placement_executed\t0', 'placement_executed\t1'),
                         ('status\tcompleted', 'status\tfailed')):
            with self.subTest(state=new):
                metadata.write_text(original.replace(old, new))
                self.assert_invalid('completed, unplaced')

    def test_existing_analysis_is_not_overwritten(self):
        self.run_analysis()
        before = (self.out / 'summary.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.run_analysis()
        self.assertEqual((self.out / 'summary.json').read_bytes(), before)

    def test_amf_partition_uses_archive_tile_x_and_not_slice_x(self):
        separate = self.run_analysis()
        self.assertFalse(separate['amf_slice_half_column_partition_matches'])
        # Raw fabric inventory intentionally has no tile field: only the actual
        # archive used by AMF can establish these shared tile columns.
        self.write_device(self.device_text.replace('tile=> CLE_X1Y', 'tile=> CLE_X0Y'))
        shared = self.run_analysis(self.root / 'analysis_shared_tile')
        self.assertTrue(shared['amf_slice_half_column_partition_matches'])
        self.assertEqual(shared['shared_leaf_domains'], 2)

    def test_capacity_table_takes_measured_leaf_cardinality_instead_of_legacy_twelve(self):
        self.prepare_uniform_capacity_fixture(shared_tile_column=True)
        table = build_table(self.fabric, self.device, self.part,
                            ROOT / 'configs/architecture/clock-resource-rules.json',
                            self.out, half_column_raw=self.raw)
        self.assertEqual([region['half_column_limit'] for region in table['regions']], [2])
        self.assertEqual(table['half_column_capacity_source'],
                         'vivado-device-node-connectivity-endpoint-sampled')
        self.assertFalse(table['clock_routability_verified'])
        self.assertFalse(table['half_column_topology']['clock_routability_verified'])
        topology = json.loads(Path(table['half_column_topology']['summary_path']).read_text())
        self.assertTrue(topology['amf_slice_half_column_partition_matches'])
        self.assertEqual(topology['nominal_leaf_capacities'], [2])
        capacity_file = self.out / 'clock_capacity.tsv'
        region_lines = [line.split('\t') for line in capacity_file.read_text().splitlines()
                        if line.startswith('CR\t')]
        self.assertEqual(region_lines, [['CR', 'X0Y0', '0', '24', '24', '24', '24', '2', '24']])
        validate_capacity_inputs(capacity_file, self.device, self.part)

    def test_capacity_table_rejects_incompatible_amf_and_device_sharing_groups(self):
        self.prepare_uniform_capacity_fixture(shared_tile_column=False)
        with self.assertRaisesRegex(ValueError, 'leaf sharing differs from the AMF'):
            build_table(self.fabric, self.device, self.part,
                        ROOT / 'configs/architecture/clock-resource-rules.json',
                        self.out, half_column_raw=self.raw)
        # Diagnostic topology may be retained, but no usable placer table or
        # enabling overlay may escape a failed grouping validation.
        for name in ('clock_capacity.tsv', 'clock_capacity.json', 'config_overlay.json'):
            self.assertFalse((self.out / name).exists())

    def test_offset_site_rows_in_one_column_cannot_claim_amf_partition_equivalence(self):
        # Keep tile Y at 0..59 but move the second column's site Y to 60..119.
        # Both columns still have 60 contiguous rows and complete endpoint
        # evidence. AMF's CR-global origin would give column 1 half indices 2/3.
        def shift_site(value):
            return re.sub(r'SLICE_X1Y([0-9]+)',
                          lambda match: 'SLICE_X1Y' + str(int(match[1]) + 60), value)

        fabric_file = self.fabric / 'sites.tsv'
        fabric_file.write_text(shift_site(fabric_file.read_text()))
        self.write_device(shift_site(self.device_text))
        second_column_leaves = {}
        for half in self.halves:
            if half['slice_x'] == 1:
                half['y_min'] += 60
                half['y_max'] += 60
                half['checked_pins'] = shift_site(half['checked_pins'])
                old_leaves = half['leaf_nodes'].split(',')
                for leaf in old_leaves:
                    second_column_leaves[leaf] = 'column_1_' + leaf
                half['leaf_nodes'] = ','.join(second_column_leaves[leaf] for leaf in old_leaves)
        for pin in self.pins:
            if pin['site'].startswith('SLICE_X1Y'):
                pin['site'] = shift_site(pin['site'])
                pin['pin'] = shift_site(pin['pin'])
                pin['leaf_nodes'] = ','.join(second_column_leaves[leaf]
                                            for leaf in pin['leaf_nodes'].split(','))
        # Independent leaf sets avoid an earlier cross-height sharing error:
        # rejection must be due to the incompatible AMF Y geometry itself.
        for source in list(self.sources):
            leaf = source['leaf_node']
            self.sources.append({key: value.replace(leaf, second_column_leaves[leaf])
                                 if isinstance(value, str) else value
                                 for key, value in source.items()})
        self.assert_invalid('same contiguous 60-row range per CR')

    def test_half_ranges_must_align_with_the_cr_global_site_y_origin(self):
        self.halves[0]['y_min'], self.halves[0]['y_max'] = 1, 30
        self.assert_invalid('AMF CR-relative Y partition')


if __name__ == '__main__':
    unittest.main()
