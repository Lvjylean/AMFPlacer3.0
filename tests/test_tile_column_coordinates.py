import copy
import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from fabric_x_coordinates import derive_x_map, mapped_x
from prepare_fabric_device import convert
from build_physical_boundaries import make_geometry
from test_physical_boundaries import fixture, RULES


class TileColumnsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rows = []
        # Nonuniform RPM spacing, a misleading _R type, and a singleton.
        lanes = [(0, 20, 'CLEM_R'), (0, 40, 'CLEL_R'),
                 (1, 60, 'CLEM'), (1, 80, 'CLEL_R'),
                 (2, 200, 'CLEM'), (3, 300, 'CLEM_R'), (3, 350, 'CLEL_R')]
        for site_x, (tx, rpm, typ) in enumerate(lanes):
            for y in range(2):
                name = f'SLICE_X{site_x}Y{y}'
                self.rows.append(dict(site=name, tile=f'{typ}_X{tx}Y{y}',
                    clock_region='X0Y0', site_type='SLICEL', tile_type=typ,
                    rpm_x=rpm, rpm_y=y*2, slr=0, prohibited=0, bels=name+'/A6LUT'))
        self.sites = copy.deepcopy(self.rows)
        self.tiles = []
        for i, (rpm, column, typ, tile_typ) in enumerate([
                (110, 12, 'HPIOB_M', 'HPIO_L'),
                (140, 13, 'CMACE4', 'CMAC'),
                (240, 23, 'CONFIG_SITE', 'CFG_CONFIG')]):
            tile = f'{tile_typ}_X1Y{i}'
            self.sites.append(dict(self.rows[0], site=f'IP{i}', site_type=typ,
                tile=tile, rpm_x=rpm, tile_type=tile_typ))
            self.tiles.append(dict(tile=tile, tile_type=tile_typ, column=column, row=i, slr=0))

    def write_table(self, name, rows):
        path = self.root/name
        with path.open('w') as f:
            w = csv.DictWriter(f, rows[0].keys(), delimiter='\t')
            w.writeheader(); w.writerows(rows)
        return path

    def derive(self):
        return derive_x_map(self.rows, self.write_table('structure-sites.tsv', self.sites),
                            self.write_table('structure-tiles.tsv', self.tiles))

    def test_data_driven_subslots_and_multiple_special_columns_in_one_gap(self):
        mapping = self.derive()
        self.assertEqual(mapping['type_offsets']['CLEM_R'], -.25)
        self.assertEqual([c['base_x'] for c in mapping['tile_columns']], [0, 1, 4, 6])
        self.assertEqual([c['x'] for c in mapping['special_columns']], [1.75, 3, 5])
        self.assertEqual([mapped_x(x, mapping) for x in (20,40,60,80,200,300,350)],
                         [-.25,.25,.75,1.25,3.75,5.75,6.25])
        self.assertAlmostEqual(mapped_x(120, mapping), 1.75 + 1.25/3)
        # A common RPM scale factor cannot produce both of these intervals.
        self.assertNotEqual(.5/(40-20), .5/(350-300))

    def test_full_converter_uses_new_model_by_default(self):
        source = self.write_table('fabric.tsv', self.rows)
        sites = self.write_table('structure-sites.tsv', self.sites)
        tiles = self.write_table('structure-tiles.tsv', self.tiles)
        dest = self.root/'device.zip'
        manifest = convert(source, dest, 'xcu250-test', structure_sites=sites, structure_tiles=tiles)
        self.assertEqual(manifest['coordinate_model'], 'tile-columns-subsites-v3')
        self.assertEqual(manifest['tile_column_count'], 4)
        self.assertEqual(manifest['special_column_count'], 3)
        mapping = json.loads(dest.with_suffix('.coordinates.json').read_text())
        self.assertNotIn('rpm_x_pitch', mapping)
        with zipfile.ZipFile(dest) as z:
            line = next(x for x in z.read('exportSiteLocation').decode().splitlines()
                        if x.startswith('site=> SLICE_X4Y1 '))
        self.assertIn('centerx=> 3.75000000 centery=> 1.00000000', line)

    def test_missing_structure_is_not_silently_replaced_by_rpm(self):
        source = self.write_table('fabric.tsv', self.rows)
        with self.assertRaisesRegex(ValueError, 'require full structure'):
            convert(source, self.root/'device.zip', 'xcu250-test')
        self.assertFalse((self.root/'device.zip').exists())

    def test_duplicate_sites_in_one_special_column_are_not_extra_width(self):
        self.sites.append(dict(self.sites[-1], site='CONFIG_DUP'))
        mapping = self.derive()
        self.assertEqual(len(mapping['special_columns']), 3)
        self.assertEqual(mapping['special_columns'][-1]['site_count'], 2)

    def test_inconsistent_full_structure_is_rejected(self):
        self.sites[0]['rpm_x'] += 1
        with self.assertRaisesRegex(ValueError, 'source mismatch'): self.derive()

    def test_inconsistent_type_side_is_rejected(self):
        self.rows[0]['tile_type'] = 'CLEL_R'
        with self.assertRaisesRegex(ValueError, 'inconsistent left/right'): self.derive()

    def test_unknown_singleton_side_is_rejected(self):
        for row in self.rows:
            if row['rpm_x'] == 200: row['tile_type'] = 'UNKNOWN'
        with self.assertRaisesRegex(ValueError, 'Cannot infer singleton'): self.derive()

    def test_boundary_builder_uses_same_nonuniform_x_map(self):
        fabric, sites, tiles, mapping = fixture()
        mapping.pop('rpm_x_origin'); mapping.pop('rpm_x_pitch')
        mapping.update(x_kind='tile-columns', rpm_x_anchors=[[0,.25],[5,2.75],[10,4.75]])
        for row in fabric: row['x'] = mapped_x(row['x'], mapping)
        report = make_geometry(fabric, sites, tiles, mapping, RULES, 'xcu250-test')
        self.assertEqual(report['boundaries'][1]['coordinate'], 2.75)
        self.assertEqual(report['boundaries'][0]['coordinate'], 1.5)
        self.assertEqual(sum(r['capacity']['LUT'] for r in report['regions']), 64)


if __name__ == '__main__': unittest.main()
