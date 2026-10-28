import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from amf3 import validate_run_id
from report_bundle import extract_bundle, write_bundle


class ProjectContracts(unittest.TestCase):
    def test_dcp_inputs_and_symlinks_never_download(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work) / 'run'
            (root / 'reports').mkdir(parents=True)
            (root / 'logs').mkdir()
            (root / 'inputs').mkdir()
            (root / 'reports/timing.rpt').write_text('timing')
            (root / 'reports/final.dcp').write_bytes(b'checkpoint')
            (root / 'reports/FINAL.DCP').write_bytes(b'checkpoint')
            (root / 'reports/disguised.rpt').symlink_to(root / 'reports/final.dcp')
            (root / 'inputs/private.json').write_text('{}')
            (root / 'logs/run.log').write_text('done')
            (root / 'status.json').write_text('{}')
            stream = io.BytesIO()
            write_bundle(root, stream)
            destination = Path(work) / 'local'
            self.assertEqual(extract_bundle(stream.getvalue(), destination), 3)
            self.assertEqual({str(p.relative_to(destination)) for p in destination.rglob('*') if p.is_file()},
                             {'reports/timing.rpt', 'logs/run.log', 'status.json'})

    def test_archive_rejects_path_traversal_and_dcp_before_writes(self):
        for name in ['../escape.json', '/absolute.json', 'reports/final.dcp']:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as work:
                stream = io.BytesIO()
                with tarfile.open(fileobj=stream, mode='w:gz') as archive:
                    member = tarfile.TarInfo(name)
                    member.size = 1
                    archive.addfile(member, io.BytesIO(b'x'))
                with self.assertRaises(ValueError):
                    extract_bundle(stream.getvalue(), Path(work) / 'output')
                self.assertFalse((Path(work) / 'output').exists())

    def test_run_ids_cannot_escape_experiment_directory(self):
        self.assertEqual(validate_run_id('faceDetect-20260925-144131'), 'faceDetect-20260925-144131')
        for value in ['../other', '/tmp/run', 'x/../../y', 'x y', '']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_run_id(value)


if __name__ == '__main__':
    unittest.main()
