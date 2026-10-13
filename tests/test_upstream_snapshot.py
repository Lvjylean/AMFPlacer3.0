"""An upstream baseline must not absorb edits from either working checkout."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_upstream_snapshot import snapshot_source


class SnapshotTests(unittest.TestCase):
    def test_committed_source_excludes_dirty_and_untracked_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);repo=root/'repo';repo.mkdir();(repo/'src').mkdir()
            (repo/'src/CMakeLists.txt').write_text('project(original)\n')
            (repo/'src/placer.cc').write_text('original\n')
            env=dict(os.environ,GIT_AUTHOR_NAME='Test',GIT_AUTHOR_EMAIL='test@example.invalid',GIT_COMMITTER_NAME='Test',GIT_COMMITTER_EMAIL='test@example.invalid')
            for args in (['init','-q'],['add','src'],['commit','-qm','original']):
                subprocess.run(['git','-C',str(repo),*args],check=True,env=env)
            (repo/'src/placer.cc').write_text('modified\n')
            (repo/'src/new.cc').write_text('untracked\n')
            out=root/'snapshot';record=snapshot_source(repo,'HEAD',out)
            self.assertEqual((out/'src/placer.cc').read_text(),'original\n')
            self.assertFalse((out/'src/new.cc').exists())
            self.assertEqual((repo/'src/placer.cc').read_text(),'modified\n')
            self.assertEqual(len(record['files']),2)


if __name__=='__main__':unittest.main()
