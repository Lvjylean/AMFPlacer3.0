import copy
import json
from pathlib import Path
import sys
import unittest
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_physical_boundaries import make_geometry, mapped_y, validate_model_inputs, digest

RULES=json.loads((Path(__file__).resolve().parents[1]/'configs/architectures/ultrascale-plus-boundaries.json').read_text())
def fixture(shift=0):
    fabric=[];sites=[];tiles=[]
    for y in range(4):
        slr=0 if y<2 else 7
        for x in (0,10):
            name=f'SLICE_X{x}Y{y}'
            cr=f'X0Y{y//2}'
            tile=f'T_{x}_{y}'
            fabric.append(dict(site=name,tile=tile,clock_region=cr,site_type='SLICEM' if x==0 else 'SLICEL',x=x+shift,y=y,slr=slr,prohibited=0))
            sites.append(dict(site=name,tile=tile,clock_region=cr,site_type=fabric[-1]['site_type'],rpm_x=str(x+shift),rpm_y=str(2*y),slr=str(slr)))
            tiles.append(dict(tile=tile,tile_type='CLEL_R',column=str(x),row=str(10-y),slr=str(slr)))
    for y in (0,2):
        tile=f'IO_{y}'
        sites.append(dict(site=f'IOB_X0Y{y}',tile=tile,clock_region=f'X1Y{y//2}',site_type='HPIOB_M',rpm_x=str(5+shift),rpm_y=str(2*y),slr='0' if y==0 else '7'))
        tiles.append(dict(tile=tile,tile_type='HPIO_L',column='29',row=str(10-y),slr='0' if y==0 else '7'))
    mapping=dict(part='xcu250-test',rpm_y_anchors=[[0,0],[6,3]],rpm_x_origin=0,rpm_x_pitch=1)
    return fabric,sites,tiles,mapping

def geometry(data):return make_geometry(*data,RULES,'xcu250-test')
class GeometryTest(unittest.TestCase):
    def test_model_and_provenance_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);model=d/'physical_structure.tsv';fabric=d/'fabric.zip';raw=d/'raw.tsv'
            model.write_text('model');fabric.write_text('fabric');raw.write_text('raw')
            report=dict(model_sha256=digest(model),sources={k:dict(path=str(p),sha256=digest(p)) for k,p in [('fabric',fabric),('raw',raw)]})
            (d/'boundaries.json').write_text(json.dumps(report))
            validate_model_inputs(model,fabric)
            raw.write_text('changed')
            with self.assertRaisesRegex(ValueError,'provenance'):validate_model_inputs(model,fabric)
            raw.write_text('raw');model.write_text('changed')
            with self.assertRaisesRegex(ValueError,'model hash'):validate_model_inputs(model,fabric)
            model.write_text('model');fabric.write_text('changed')
            with self.assertRaisesRegex(ValueError,'fabric'):validate_model_inputs(model,fabric)

    def test_structural_cuts_and_shared_capacities(self):
        r=geometry(fixture())
        self.assertEqual(r['slr_order'],[0,7])
        self.assertEqual([(b['kind'],b['coordinate']) for b in r['boundaries']],[('SLR',1.5),('IO',5)])
        self.assertEqual(len(r['regions']),4)
        self.assertEqual(r['regions'][0]['capacity']['MLUT'],16)
        self.assertEqual(r['regions'][1]['capacity']['MLUT'],0)
        self.assertEqual(sum(q['capacity']['LUT'] for q in r['regions']),64)
    def test_column_shift_is_data_driven(self):
        r=geometry(fixture(17))
        self.assertEqual(r['boundaries'][1]['coordinate'],22)
    def test_tile_row_direction_is_not_used_as_amf_y(self):
        r=geometry(fixture())
        self.assertEqual(r['boundaries'][0]['coordinate'],1.5)
        self.assertEqual(mapped_y(3,fixture()[3]),1.5)
    def test_missing_io_row_does_not_make_full_cut(self):
        data=fixture();data[1].pop()
        r=geometry(data)
        self.assertFalse(r['boundaries'][1]['active'])
        self.assertEqual(len(r['regions']),2)
    def test_io_constituents_are_not_double_charged(self):
        data=fixture()
        s=copy.deepcopy(data[1][-1]);s['site']='IOB_DUP';s['site_type']='HPIOB_S';data[1].append(s)
        r=geometry(data)
        self.assertEqual(sum(b['kind']=='IO' and b['active'] for b in r['boundaries']),1)
    def test_unknown_and_local_ip_remain_candidates(self):
        data=fixture()
        for typ in ('NEW_IP','PCIE40E4'):
            s=copy.deepcopy(data[1][0]);s['site']=typ;s['site_type']=typ;data[1].append(s)
        r=geometry(data)
        self.assertEqual(r['unknown_site_types'],{'NEW_IP':1})
        self.assertFalse(r['local_ip_candidates'][0]['active'])
    def test_prohibited_capacity(self):
        data=fixture();data[0][0]['prohibited']=1
        r=geometry(data);self.assertEqual(r['regions'][0]['capacity']['LUT'],8)
    def test_coordinate_mismatch_rejected(self):
        data=fixture();data[0][0]['x']=0.5
        with self.assertRaisesRegex(ValueError,'coordinate'):geometry(data)
    def test_nonmonotone_mapping_rejected(self):
        data=fixture();data[3]['rpm_y_anchors']=[[0,1],[6,0]]
        with self.assertRaisesRegex(ValueError,'monotone'):geometry(data)
    def test_interleaved_slr_rejected(self):
        data=fixture()
        data[0][0]['slr']=7;data[1][0]['slr']='7'
        with self.assertRaisesRegex(ValueError,'stacked'):geometry(data)

if __name__=='__main__':unittest.main()

