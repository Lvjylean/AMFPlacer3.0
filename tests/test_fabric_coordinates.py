import csv
import importlib.util
import json
import re
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import prepare_fabric_device as candidate

class CoordinateScaleTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.rows=[]
        for slr in range(4):
            for family,n,x,rpmx in [('SLICE',60,0,48),('SLICE',60,1,64),('DSP48E2',24,0,96),('RAMB18',24,0,128),('RAMB36',12,0,128),('URAM288',16,0,160)]:
                for i in range(n):
                    site=f'{family}_X{x}Y{slr*n+i}'
                    ty=slr*60+(i if family=='SLICE' else i//4*15 if family=='URAM288' else i*5 if family=='RAMB36' else i//2*5)
                    typ={'SLICE':'SLICEL','DSP48E2':'DSP48E2','RAMB18':('RAMB181' if i%2 else 'RAMBFIFO18'),'RAMB36':'RAMBFIFO36','URAM288':'URAM288'}[family]
                    self.rows.append(dict(site=site,tile=f'TILE_X{0 if family=="SLICE" else rpmx//16}Y{ty}',clock_region=f'X0Y{slr}',site_type=typ,tile_type='test',rpm_x=rpmx,rpm_y=2*(slr*60+i) if family=='SLICE' else ty*2+(i%4 if family=='URAM288' else i%2),slr=slr,prohibited=int(family=='URAM288' and i==0),bels=site+'/BEL'))
        self.meta=self.root/'metadata.tsv'
        self.meta.write_text('part\txcu250-figd2104-2L-e\nscope\tfabric-sites\n')
    def run_conversion(self):
        source=self.root/'sites.tsv'
        with source.open('w') as f:
            w=csv.DictWriter(f,self.rows[0].keys(),delimiter='\t');w.writeheader();w.writerows(self.rows)
        return candidate.convert(source,self.root/'device.zip','xcu250-figd2104-2L-e',self.meta,x_model='rpm')
    def test_common_scale_and_geometry_survive(self):
        m=self.run_conversion()
        self.assertEqual(m['x_pitch'],16)
        self.assertEqual(m['resource_geometry']['URAM288'],dict(tile_span_rows=15,sites_per_tile=4))
        self.assertEqual(m['site_count'],len(self.rows))
        self.assertEqual(m['slr_count'],4)
        with zipfile.ZipFile(self.root/'device.zip') as z:
            text=z.read('exportSiteLocation').decode()
        self.assertIn('centerx=> 1.00000000 centery=> 0.00000000',text)
        self.assertIn('centery=> 0.00000000',text)
        self.assertIn('centery=> 2.50000000',text)
        self.assertIn('centery=> 3.75000000',text)
        self.assertIn('prohibited=> 1',text)
        before=(self.root/'device.zip').read_bytes()
        with self.assertRaisesRegex(ValueError,'already exists'): self.run_conversion()
        self.assertEqual(before,(self.root/'device.zip').read_bytes())
    def test_incompatible_tile_anchor_is_rejected(self):
        next(r for r in self.rows if r['site']=='DSP48E2_X0Y1')['tile']='DSP_X6Y99'
        with self.assertRaisesRegex(ValueError,'Irregular resource tile coverage'): self.run_conversion()
        self.assertFalse((self.root/'device.zip').exists())
    def test_inconsistent_pair_pitch_is_rejected(self):
        next(r for r in self.rows if r['site']=='SLICE_X1Y1')['rpm_x']=80
        with self.assertRaisesRegex(ValueError,'Inconsistent RPM X'): self.run_conversion()

    def test_missing_tile_does_not_compress_coordinates(self):
        self.rows=[r for r in self.rows if not (r['site'].startswith('DSP48E2') and r['tile'].endswith('Y5'))]
        with self.assertRaises(ValueError): self.run_conversion()
        self.assertFalse((self.root/'device.zip').exists())
    def test_tile_gap_is_preserved_not_clock_region_flattened(self):
        for row in self.rows:
            ty=int(re.search(r'Y(\d+)$',row['tile'])[1])
            if ty >= 60:
                row['tile']=re.sub(r'Y\d+$', 'Y'+str(ty+7),row['tile'])
        self.run_conversion()
        with zipfile.ZipFile(self.root/'device.zip') as z:
            lines=z.read('exportSiteLocation').decode().splitlines()
        line=next(l for l in lines if l.startswith('site=> SLICE_X0Y60 '))
        self.assertIn('centery=> 67.00000000',line)

    def test_legacy_entry_point_is_retired(self):
        import prepare_legacy_scale_device
        with self.assertRaisesRegex(ValueError,'Retired'): prepare_legacy_scale_device.convert()

if __name__=='__main__':unittest.main()
