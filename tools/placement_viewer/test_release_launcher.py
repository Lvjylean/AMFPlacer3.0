"""Portable release entry points must retain historical-directory compatibility."""
import json
import os
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

LAUNCHER = Path(__file__).parent / 'launcher'


class ReleaseLauncherTests(unittest.TestCase):
    def test_settings_are_explicit_and_not_personal_defaults(self):
        load = runpy.run_path(str(LAUNCHER / 'viewer_settings.py'))['load_settings']
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'settings.json'
            with patch.dict(os.environ, {'AMF_VIEWER_SETTINGS': str(path)}):
                with self.assertRaises(ValueError):
                    load()
                value = dict(ssh_host='test-host', remote_project_root='/project with spaces',
                             remote_tools_dir='/release/tools/placement_viewer', picker_port=18820)
                path.write_text(json.dumps(value))
                self.assertEqual(load()['picker_port'], 18820)
                for change in ({'ssh_host': '-oProxyCommand=bad'}, {'picker_port': 80},
                               {'remote_tools_dir': 'relative'}):
                    path.write_text(json.dumps(dict(value, **change)))
                    with self.assertRaises(ValueError):
                        load()

    def test_remote_discovers_both_trajectory_locations(self):
        with tempfile.TemporaryDirectory() as d:
            with patch.dict(os.environ, {'AMF_VIEWER_PROJECT_ROOT': d}):
                module = runpy.run_path(str(LAUNCHER / 'amf-viewer-remote.py'))
            self.assertEqual(module['ROOT'], Path(d).resolve())
            run = Path(d) / 'experiments/runs/test'
            legacy = run / 'placement'
            modern = legacy / 'visualization'
            modern.mkdir(parents=True)
            (legacy / 'DumpAllCoordTrace-0.gz').write_bytes(b'fixture')
            self.assertEqual(module['resolve_trace'](run), legacy)
            (modern / 'DumpAllCoordTrace-1.gz').write_bytes(b'fixture')
            self.assertEqual(module['resolve_trace'](run), modern)
            viewer = module['select_viewer']({'device': 'U250'})
            self.assertTrue(viewer.is_file())
            self.assertEqual(viewer, module['select_viewer']({
                'device': 'VHK158', 'visualization device metadata file': '/geometry.json'}))
            with self.assertRaises(ValueError):
                module['select_viewer']({'device': 'VHK158'})
            with self.assertRaises(ValueError):
                module['start']('/outside/project/run')


if __name__ == '__main__':
    unittest.main()
