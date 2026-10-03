import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


device = load('prepare_fabric_device')
netlist = load('convert_netlist_inventory')


class U250InputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def device_rows(self):
        rows = []
        for slr in range(2):
            for x in range(2):
                for y in range(2):
                    site = f'SLICE_X{x}Y{slr * 2 + y}'
                    rows.append(dict(site=site, tile=f'CLE_X{x}Y{slr * 2 + y}', clock_region=f'X0Y{slr}',
                                     site_type='SLICEL', tile_type='CLE', rpm_x=str(x * 16), rpm_y=str(slr * 4 + y),
                                     slr=str(slr), prohibited='0', bels=site + '/A6LUT'))
            site = f'URAM288_X0Y{slr}'
            rows.append(dict(site=site, tile=f'URAM_X2Y{slr * 2}', clock_region=f'X0Y{slr}',
                             site_type='URAM288', tile_type='URAM', rpm_x='32', rpm_y=str(slr * 4),
                             slr=str(slr), prohibited=str(slr), bels=site + '/URAM_288K_INST'))
        return rows

    def write_device(self, rows):
        p = self.root / 'sites.tsv'
        with p.open('w') as f:
            writer = csv.DictWriter(f, rows[0].keys(), delimiter='\t')
            writer.writeheader()
            writer.writerows(rows)
        return p

    def test_slr_and_unavailable_sites_survive_conversion(self):
        p = self.write_device(self.device_rows())
        result = device.convert(p, self.root / 'device.zip', 'test', x_model='rpm')
        self.assertEqual(result['slr_count'], 2)
        self.assertEqual(result['sites_by_slr'][0]['URAM288'], 1)
        self.assertEqual(result['unavailable_by_slr'][1]['URAM288'], 1)
        with zipfile.ZipFile(self.root / 'device.zip') as z:
            text = z.read('exportSiteLocation').decode()
        self.assertIn('slr=> 1 prohibited=> 1', text)
        self.assertIn('site=> SLICE_X0Y2', text)

    def test_ambiguous_slr_fails_before_output(self):
        rows = self.device_rows()
        rows[0]['slr'] = '1'
        p = self.write_device(rows)
        with self.assertRaisesRegex(ValueError, 'multiple SLRs'):
            device.convert(p, self.root / 'device.zip', 'test')
        self.assertFalse((self.root / 'device.zip').exists())

    def test_missing_resource_row_is_not_compressed_away(self):
        rows = self.device_rows()
        rows[1]['site'] = 'SLICE_X0Y99'
        rows[1]['bels'] = 'SLICE_X0Y99/A6LUT'
        p = self.write_device(rows)
        with self.assertRaisesRegex(ValueError, 'Non-contiguous'):
            device.convert(p, self.root / 'device.zip', 'test')

    def test_wrong_part_metadata_is_rejected(self):
        source = self.write_device(self.device_rows())
        metadata = self.root / 'metadata.tsv'
        metadata.write_text('part\txcu250\nscope\tfabric-sites\n')
        with self.assertRaisesRegex(ValueError, 'metadata'):
            device.convert(source, self.root / 'device.zip', 'wrong-part', metadata)

    def netlist_files(self):
        dcp = self.root / 'input.dcp'
        dcp.write_bytes(b'test fixture only')
        binding = self.root / 'binding.json'
        binding.write_text(json.dumps({'input_sha256': hashlib.sha256(dcp.read_bytes()).hexdigest()}))
        cells, nets = self.root / 'cells.tsv', self.root / 'nets.tsv'
        cells.write_text('cell_id\tcell_name\tprimitive\n'
                         'c0\tgnd\tGND\nc1\tmem\tURAM288\nc2\tlut\tLUT1\n')
        nets.write_text('net_id\ttype\tdrivers\tsinks\n'
                        'n0\tLOCAL_CLOCK\tPORT:ap_clk\tc1:CLK\n'
                        'n1\tGROUND\tc0:G\tc1:EN_A\n'
                        'n2\tSIGNAL\tc1:DOUT_A[0]\tc2:I0\n')
        return cells, nets, dcp, binding

    def test_uram_external_clock_and_constants_are_preserved(self):
        cells, nets, dcp, binding = self.netlist_files()
        output = self.root / 'netlist.zip'
        result = netlist.convert(cells, nets, output, dcp, binding)
        self.assertEqual(result['amf_cell_count'], 2)
        self.assertEqual(result['amf_pin_count'], 4)
        self.assertEqual(result['clocks'], ['@PORT/ap_clk'])
        with zipfile.ZipFile(output) as z:
            text = z.read('allCellPinNet').decode()
        self.assertNotIn('type=> GND', text)
        self.assertIn('type=> URAM288', text)
        self.assertIn('drivepin=> <const0>', text)
        self.assertIn('drivepin=> @PORT/ap_clk', text)
        self.assertIn('drivepin=> mem/DOUT_A[0]', text)

    def test_dcp_mismatch_is_rejected(self):
        cells, nets, dcp, binding = self.netlist_files()
        dcp.write_bytes(b'wrong design')
        with self.assertRaisesRegex(ValueError, 'binding'):
            netlist.convert(cells, nets, self.root / 'netlist.zip', dcp, binding)

    def test_missing_clock_connectivity_requires_supplement(self):
        cells, nets, dcp, binding = self.netlist_files()
        nets.write_text(nets.read_text().replace('PORT:ap_clk\tc1:CLK', 'PORT:ap_clk\t'))
        output = self.root / 'netlist.zip'
        with self.assertRaisesRegex(ValueError, 'no input pins'):
            netlist.convert(cells, nets, output, dcp, binding)
        self.assertFalse(output.exists())
        supplement = self.root / 'clock-pins.tsv'
        supplement.write_text('dcp_sha256\t' + netlist.digest(dcp) + '\n'
                              'driver_pin\tpin_name\n@PORT/ap_clk\tmem/CLK\n')
        result = netlist.convert(cells, nets, output, dcp, binding, supplement)
        self.assertEqual(result['supplemented_clock_pins'], 1)
        self.assertEqual(result['clock_input_pins'], {'@PORT/ap_clk': 1})

    def test_wrong_clock_supplement_binding_is_rejected(self):
        cells, nets, dcp, binding = self.netlist_files()
        supplement = self.root / 'clock-pins.tsv'
        supplement.write_text('dcp_sha256\twrong\ndriver_pin\tpin_name\n')
        with self.assertRaisesRegex(ValueError, 'Clock pins'):
            netlist.convert(cells, nets, self.root / 'netlist.zip', dcp, binding, supplement)

    def test_high_fanout_net_exceeds_default_csv_limit(self):
        cells, nets, dcp, binding = self.netlist_files()
        with cells.open('a') as f:
            for i in range(3, 18003):
                f.write(f'c{i}\tlut{i}\tLUT1\n')
        with nets.open('a') as f:
            f.write('n3\tSIGNAL\tPORT:reset\t' + ','.join(f'c{i}:I0' for i in range(3, 18003)) + '\n')
        result = netlist.convert(cells, nets, self.root / 'netlist.zip', dcp, binding)
        self.assertEqual(result['amf_pin_count'], 18004)

    def test_multiple_drivers_are_rejected(self):
        cells, nets, dcp, binding = self.netlist_files()
        nets.write_text(nets.read_text().replace('PORT:ap_clk', 'PORT:a,PORT:b'))
        with self.assertRaisesRegex(ValueError, 'one driver'):
            netlist.convert(cells, nets, self.root / 'netlist.zip', dcp, binding)


if __name__ == '__main__':
    unittest.main()
