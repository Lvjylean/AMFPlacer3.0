import hashlib,json,tempfile,unittest
from pathlib import Path
from device_geometry import identity,load_explicit
from multi_device_viewer import catalog_handler

class DeviceGeometryTests(unittest.TestCase):
    def test_wrong_label_rejected(self):
        with self.assertRaises(ValueError):identity({'device':'VHK158','physical device part':'xcu250-figd2104-2L-e'})
    def test_sparse_regions_not_padded_and_wrong_archive_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); source=p/'device.zip';source.write_bytes(b'fixture geometry archive')
            g={'schema':1,'device':'VHK158','part':'xcvh1582-fixture','bounds':[0,20,0,20],
               'clock_regions':[{'name':'X0Y0','bounds':[0,10,0,10]},{'name':'X2Y1','bounds':[12,20,10,20]}],
               'slr_regions':[{'name':'SLR0','bounds':[0,20,0,20]}],
               'device_input_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
            gp=p/'geometry.json';gp.write_text(json.dumps(g)); cfg={'device':'VHK158','physical device part':'xcvh1582-fixture','visualization device metadata file':str(gp),'vivado extracted device information file':str(source)}
            got=load_explicit(cfg,p/'config.json');self.assertEqual(len(got['clock_regions']),2)
            source.write_bytes(b'different device');
            with self.assertRaises(ValueError):load_explicit(cfg,p/'config.json')
    def test_catalog_cannot_relabel_run(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d); (p/'config.json').write_text(json.dumps({'device':'U250','physical device part':'xcu250-fixture'}))
            catalog={'schema':1,'runs':[{'id':'v','device':'VHK158','config':'config.json','traces':'trace','label':'x','scope':'fixture'}]}
            cp=p/'catalog.json';cp.write_text(json.dumps(catalog))
            with self.assertRaises(ValueError):catalog_handler(cp)

if __name__=='__main__':unittest.main()
