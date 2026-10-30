import gzip
import json
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from urllib.request import urlopen
from urllib.error import HTTPError

from build_layout_bundle import membership, sll_overlay, write_frame, write_json, placed_points, pblocks
from local_layout import Bundle, handler, PageLeases


class LayersTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.channels=self.root/'channels.tsv'
        self.channels.write_text('resource\tfrom\tto\tcolumn\tin_x\tin_y\tout_x\tout_y\n'
            'T/UBUMP0\t0\t1\t71\t10\t180\t10\t240\n'
            'T/UBUMP0\t1\t0\t71\t10\t240\t10\t180\n'
            'T/UBUMP1\t0\t1\t71\t10\t180\t10\t240\n')
        self.plan=self.root/'plan.tcl'
        self.plan.write_text('set e2_sll_plan [list \\\n'
          ' [dict create net "top/rp/n\\[0\\]" driver "top/rp/u/O" source_slr 1 tree [list '
          '[dict create resource "T/UBUMP0" from 1 to 0 segment [list "a"] children [list]]]] \\\n]\n')
        self.regions=[dict(id='a',prefix='top/rp')]

    def test_direction_and_single_capacity(self):
        o=sll_overlay(self.plan,self.channels,self.regions)
        c=o['crossings'][0];t=o['tiles'][0]
        self.assertEqual(c['entry'],[10,240]);self.assertEqual(c['exit'],[10,180])
        self.assertEqual(c['net'],'top/rp/n[0]');self.assertEqual(c['region'],'a')
        self.assertEqual((t['used'],t['capacity']),(1,2))

    def test_empty_is_not_missing(self):
        self.plan.write_text('set e2_sll_plan [list]\n')
        self.assertEqual(sll_overlay(self.plan,self.channels,[])['state'],'recorded')
        self.assertEqual(sll_overlay(None,self.channels,[])['state'],'missing')

    def test_truncated_export_not_reported_as_zero(self):
        self.plan.write_text('')
        with self.assertRaises(ValueError):sll_overlay(self.plan,self.channels,[])
        self.plan.write_text('set e2_sll_plan [list \\\n')
        with self.assertRaises(ValueError):sll_overlay(self.plan,self.channels,[])

    def test_unsupported_plan_never_executes(self):
        self.plan.write_text('exec touch NEVER\n')
        with self.assertRaises(ValueError):sll_overlay(self.plan,self.channels,[])
        self.assertFalse((self.root/'NEVER').exists())

    def test_membership_is_hierarchy_not_geometry(self):
        self.assertEqual(membership('top/rp2/a',self.regions),'static')
        self.assertEqual(membership('top/rp/a',self.regions),'a')
        p=self.root/'placed.tsv';p.write_text('top/rp2/a\tSLICE_X1Y1/A6LUT\n')
        a=placed_points(p,{'SLICE_X1Y1':[10,150]},{'top/rp2/a':'LUT'},self.regions,True)
        self.assertEqual(a[0][4:],['static','context'])

    def test_disjoint_pblocks_not_union_bbox(self):
        (self.root/'regions.tsv').write_text('id\tpblock\tpartition\tderived_ranges\na\tpb\ttop/rp\tCR\n')
        (self.root/'resource_regions.tsv').write_text('region\tclock_regions\na\tCLOCKREGION_X0Y0:CLOCKREGION_X0Y0 CLOCKREGION_X2Y0:CLOCKREGION_X2Y0\n')
        result=pblocks(self.root,{'cr_x':[0,10,20,30],'cr_y':[0,60]})
        self.assertEqual(result[0]['rects'],[[0,10,0,60],[20,30,0,60]])

    def make_bundle(self):
        points=[[10,100,'top/rp/u','LUT','a','active']]
        f=write_frame(self.root,'f0',points,sll_overlay(self.plan,self.channels,self.regions))
        g=write_frame(self.root,'f1',points,dict(state='recorded',crossings=[],tiles=[]))
        h=dict(g,name='f2');h.pop('layers')
        write_json(self.root/'manifest.json',dict(schema='amf-local-layout-v1',run='/run',device='U250',
            title='test',mode='full',metadata={},placement_stage='test',frames=[f,g,h]))
        return Bundle(self.root)

    def test_frame_lifecycle_and_no_final_fallback(self):
        bundle=self.make_bundle()
        a,z=bundle.layers('f0');self.assertTrue(z)
        self.assertEqual(len(json.loads(gzip.decompress(a))['crossings']),1)
        b,z=bundle.layers('f1');self.assertEqual(json.loads(gzip.decompress(b))['crossings'],[])
        c,z=bundle.layers('f2');self.assertFalse(z);self.assertEqual(json.loads(c)['state'],'missing')
        with self.assertRaises(ValueError):bundle.layers('unknown')

    def test_invalid_asset_cannot_escape_bundle(self):
        bundle=self.make_bundle()
        with self.assertRaises(ValueError):bundle.file('../outside.json')
        with self.assertRaises(ValueError):bundle.file('/etc/passwd')

    def test_duplicate_cells_rejected(self):
        with self.assertRaises(ValueError):
            write_frame(self.root,'bad',[[0,0,'a','LUT','a','active']]*2,dict(crossings=[]))

    def test_http_frame_identity_and_layers(self):
        bundle=self.make_bundle();pool=PageLeases()
        server=ThreadingHTTPServer(('127.0.0.1',0),handler(bundle,pool))
        t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
        try:
            base=f'http://127.0.0.1:{server.server_port}'
            with urlopen(base+'/api/frames') as r:self.assertEqual(json.load(r)['frames'],['f0','f1','f2'])
            with urlopen(base+'/api/layers?name=f1') as r:
                self.assertEqual(r.headers['Content-Encoding'],'gzip')
                self.assertEqual(json.loads(gzip.decompress(r.read()))['crossings'],[])
            with self.assertRaises(HTTPError):urlopen(base+'/api/layers?name=unknown')
        finally:server.shutdown();server.server_close();t.join()


if __name__=='__main__':unittest.main()
