import gzip
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image
from compact_preview import CompactPreview, Budget, render


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.config=self.root/'config.json';self.config.write_text('{}')
        self.meta=dict(bounds=[0,100,0,300],cr_x=[0,50,100],cr_y=[0,100,200,300],slr=[100,200])
        self.sources=[]
        for i in range(4):
            p=self.root/f'DumpAllCoordTrace-{i}.gz'
            with gzip.open(p,'wt') as f:f.write('10 20 cell_a\n30 40 cell_b\n50 60 virtual\n')
            self.sources.append(p)
        self.cache=CompactPreview(self.sources,{'cell_a':'LUT','cell_b':'BRAM'},self.meta,
                                  self.root/'cache',self.config,limit_bytes=1024**2,start_worker=False)

    def tearDown(self):
        self.temp.cleanup()

    def test_raster_counts_and_source_unchanged(self):
        before=self.sources[0].read_bytes()
        body,counts=render(self.sources[0],self.cache.types,self.meta)
        self.assertEqual(counts,dict(source_points=3,drawn_points=2,hidden_unknown_virtual=1))
        with Image.open(io.BytesIO(body)) as im:self.assertEqual(im.size,(640,900))
        self.assertEqual(self.sources[0].read_bytes(),before)

    def test_bounded_lru_eviction_only_in_cache(self):
        for p in self.sources:
            self.cache.publish(p.name,b'x'*(180*1024),{},self.cache.signature(p.name))
        self.assertLessEqual(self.cache.used(),self.cache.limit)
        self.assertLess(len(self.cache.entries),len(self.sources))
        self.assertEqual(len(list(self.root.glob('*.gz'))),4)
        self.assertIn(self.sources[-1].name,self.cache.entries)

    def test_source_change_invalidates_cached_frame(self):
        name=self.sources[0].name
        self.cache.publish(name,b'image',{},self.cache.signature(name))
        self.assertEqual(self.cache.request(name),b'image')
        with gzip.open(self.sources[0],'wt') as f:f.write('99 99 changed\n')
        self.assertIsNone(self.cache.request(name))
        self.assertIn(name,self.cache.pending)

    def test_change_during_render_not_published(self):
        name=self.sources[0].name;signature=self.cache.signature(name)
        self.sources[0].write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'changed while'):
            self.cache.publish(name,b'image',{},signature)
        self.assertFalse(self.cache.target(name).exists())

    def test_restart_recovers_index_and_rejects_other_identity(self):
        name=self.sources[0].name
        self.cache.publish(name,b'image',{},self.cache.signature(name))
        again=CompactPreview(self.sources,self.cache.types,self.meta,self.root/'cache',self.config,
                             limit_bytes=1024**2,start_worker=False)
        self.assertEqual(again.request(name),b'image')
        self.config.write_text('{"changed":true}')
        with self.assertRaisesRegex(ValueError,'identity'):
            CompactPreview(self.sources,self.cache.types,self.meta,self.root/'cache',self.config,start_worker=False)

    def test_unknown_frame_rejected(self):
        with self.assertRaisesRegex(ValueError,'Unknown'):
            self.cache.request('../other')

    def test_metadata_counts_toward_quota_and_is_not_evicted(self):
        snapshot=self.root/'cache/metadata.json.gz'
        snapshot.write_bytes(b'x'*(256*1024))
        for p in self.sources:
            self.cache.publish(p.name,b'x'*(180*1024),{},self.cache.signature(p.name))
        self.assertEqual(snapshot.stat().st_size,256*1024)
        self.assertLessEqual(self.cache.used(),self.cache.limit)
        self.assertEqual(len(self.cache.entries),1)

    def test_metadata_reuse_rejects_changed_inputs(self):
        from metadata_snapshot import signature, load
        design=self.root/'net.zip';design.write_bytes(b'net')
        device=self.root/'device.zip';device.write_bytes(b'device')
        (self.root/'boundaries.json').write_text('{}')
        self.config.write_text(json.dumps({'vivado extracted design information file':str(design),
            'vivado extracted device information file':str(device),
            'physical boundary model file':str(self.root/'model.json')}))
        p=self.root/'metadata.json.gz'
        with gzip.open(p,'wt') as f:
            json.dump(dict(signature=signature(self.config),types=self.cache.types,metadata=self.meta),f)
        self.assertEqual(load(p,self.config),(self.cache.types,self.meta))
        design.write_bytes(b'changed input')
        with self.assertRaisesRegex(ValueError,'stale'):
            load(p,self.config)

    def test_nonfinite_source_rejected(self):
        with gzip.open(self.sources[0],'wt') as f:f.write('nan 2 cell_a\n')
        with self.assertRaisesRegex(ValueError,'Non-finite'):
            render(self.sources[0],self.cache.types,self.meta)

    def test_cpu_and_read_budget_sleep(self):
        with patch('compact_preview.time.monotonic',side_effect=[0,1]),patch('compact_preview.time.thread_time',side_effect=[0,.5]),patch('compact_preview.time.sleep') as sleep:
            Budget(.25,2).pace(6*1024**2)
            sleep.assert_called_once_with(2.0)


if __name__=='__main__':unittest.main()
