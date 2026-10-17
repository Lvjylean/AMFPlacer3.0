import csv
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_cache_contract import partition_coordinates, validate_coordinate_map, verify_cache, model_identity, validate_full_capture, verify_export_scripts
from run_dfx import sha


class NativeCacheContractTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.mapping = dict(schema='amf-coordinate-map-v2', x_kind='tile-columns',
                            tile_columns=[dict(tile_x=2, base_x=3.0)], type_offsets={'CLEL_L': -.25},
                            tile_y_origin=4, tile_y_pitch=2)
        self.mapfile = self.root/'map.json';self.mapfile.write_text(json.dumps(self.mapping))
        self.device = self.root/'device.zip'
        with zipfile.ZipFile(self.device, 'w') as z:
            z.writestr('device', 'site=> SLICE_X0Y0 tile=> CLEL_L_X2Y10 clockRegionName=> X0Y0 sitetype=> SLICEL tiletype=> CLEL_L centerx=> 2.75000000 centery=> 3.00000000 BELs=> []\n')
        self.ports = self.root/'ports.tsv';self.ports.write_text('module\tport\tdirection\tdriver\tclock\nb\tout[0]\tOUT\tb/lut/O\t0\nb\tclk\tIN\t@DFX_CLOCK/buf/O\t1\n')
        self.nodes = self.root/'nodes.tsv'

    def tearDown(self):
        self.temp.cleanup()

    def test_recovery_requires_export_dependency_identity_but_allows_import_fix(self):
        names = ('dfx_native_rm_export.tcl', 'dfx_native_export.tcl', 'dfx_common.tcl',
                 'dfx_input_scope.tcl', 'dfx_srl_metadata.tcl')
        old = {n: 'digest-'+n for n in names}
        current = dict(old, **{'dfx_native_rm_import.tcl': 'new-importer'})
        self.assertEqual(verify_export_scripts(old, current), old)
        for n in names:
            bad = dict(current);bad[n] = 'changed'
            with self.assertRaises(ValueError):verify_export_scripts(old, bad)
            del bad[n]
            with self.assertRaises(ValueError):verify_export_scripts(old, bad)

    def test_full_capture_rejects_bad_request_before_expensive_work(self):
        cfg = {'vivado extracted device information file': str(self.device)}
        job = dict(native_cache_capture=True, native_static_identity='full-v0', native_coordinate_map=str(self.mapfile))
        capability = {'native_static_cache_schema': 2}
        validate_full_capture(job, capability, cfg)
        for bad_job, bad_cap in ((job, {}), (dict(job, native_static_identity=''), capability),
                                (dict(job, reuse_prepared_input_run='old-run'), capability),
                                (dict(job, native_coordinate_map=str(self.root/'missing.json')), capability)):
            with self.assertRaises(ValueError):validate_full_capture(bad_job, bad_cap, cfg)
        validate_full_capture({}, {}, {})

    def test_frozen_device_mapping_matches_actual_metric(self):
        _, proof = validate_coordinate_map(self.mapfile, self.device)
        self.assertEqual(proof['tile_count'], 1)
        self.mapping['tile_columns'][0]['base_x'] = 2
        self.mapfile.write_text(json.dumps(self.mapping))
        with self.assertRaises(ValueError):validate_coordinate_map(self.mapfile, self.device)

    def test_preserves_all_assigned_nodes_and_empty_dedicated_port(self):
        self.nodes.write_text('module\tport\tdirection\tnode\nb\tout[0]\tOUT\tINT_X2Y10/A\nb\tout[0]\tOUT\tINT_X2Y10/B\nb\tclk\tIN\t-\n')
        out = self.root/'endpoints.tsv'
        proof = partition_coordinates(self.nodes, self.ports, self.mapping, out)
        self.assertEqual(proof, {'ports': 2, 'located_ports': 1, 'clock_ports': 1, 'clock_nodes': 0})
        with out.open() as f:rows = list(csv.DictReader(f, delimiter='\t'))
        self.assertEqual(rows[0]['node'], 'INT_X2Y10/A INT_X2Y10/B')
        self.assertEqual((float(rows[0]['x']), float(rows[0]['y'])), (3., 3.))

    def test_distributed_clock_nodes_are_preserved_without_data_endpoint(self):
        text = 'module\tport\tdirection\tnode\nb\tout[0]\tOUT\tINT_X2Y10/A\nb\tclk\tIN\tRCLK_X2Y10/CLK0\nb\tclk\tIN\tRCLK_X2Y12/CLK1\n'
        self.nodes.write_text(text)
        out = self.root/'endpoints.tsv'
        proof = partition_coordinates(self.nodes, self.ports, self.mapping, out)
        self.assertEqual(proof['clock_nodes'], 2)
        self.assertEqual(proof['located_ports'], 1)
        self.assertEqual(self.nodes.read_text(), text)
        self.assertNotIn('\tclk\t', out.read_text())

    def test_distinct_locations_are_not_silently_collapsed(self):
        self.nodes.write_text('module\tport\tdirection\tnode\nb\tout[0]\tOUT\tINT_X2Y10/A\nb\tout[0]\tOUT\tINT_X2Y12/B\nb\tclk\tIN\t-\n')
        with self.assertRaisesRegex(ValueError, 'distinct coordinates'):
            partition_coordinates(self.nodes, self.ports, self.mapping, self.root/'bad.tsv')

    def test_changed_interface_and_incomplete_coverage_rejected(self):
        for text in ('module\tport\tdirection\tnode\nb\tout[0]\tIN\tINT_X2Y10/A\n',
                     'module\tport\tdirection\tnode\nb\tout[0]\tOUT\tINT_X2Y10/A\n'):
            self.nodes.write_text(text)
            with self.assertRaises(ValueError):partition_coordinates(self.nodes, self.ports, self.mapping, self.root/'bad.tsv')

    def test_cache_bound_to_shell_and_resource_models(self):
        cache = self.root/'cache.bin';cache.write_bytes(b'synthetic immutable database')
        shell = self.root/'shell.dcp';shell.write_bytes(b'synthetic shell identity, not a DCP')
        cfg = {'device':'U250', 'vivado extracted device information file':str(self.device), 'ProPrimitiveTiming':'false'}
        c = dict(schema='amf-native-static-contract-v2', state='sealed', cache_sha256=sha(cache),
                 static_shell_sha256=sha(shell), device='U250', clock_period_ns=20, model_identity=model_identity(cfg))
        contract = self.root/'contract.json';contract.write_text(json.dumps(c))
        native = dict(path=str(cache), sha256=sha(cache), contract=str(contract), contract_sha256=sha(contract))
        job = dict(static_checkpoint=str(shell), clock_period_ns=20)
        self.assertEqual(verify_cache(native, job, cfg)['state'], 'sealed')
        with self.assertRaises(ValueError):verify_cache(native, dict(job, clock_period_ns=10), cfg)
        with self.assertRaises(ValueError):verify_cache(native, job, dict(cfg, ProPrimitiveTiming='true'))
        with self.assertRaisesRegex(ValueError, 'resource-certified'):
            verify_cache(native, dict(job, native_dfx_edge_policy='clock-region-slice-dsp-v1'), cfg)
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            verify_cache(native, dict(job, native_dfx_edge_policy='typo'), cfg)
        c['resource_policy'] = {'version':'clock-region-slice-dsp-v1','cache_format':3}
        contract.write_text(json.dumps(c)); native['contract_sha256'] = sha(contract)
        self.assertEqual(verify_cache(native, dict(job, native_dfx_edge_policy='clock-region-slice-dsp-v1'), cfg)['resource_policy']['cache_format'], 3)
        shell.write_bytes(b'other static routes')
        with self.assertRaises(ValueError):verify_cache(native, job, cfg)


if __name__ == '__main__':unittest.main()
