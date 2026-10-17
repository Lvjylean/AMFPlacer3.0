"""Safety tests for process isolation, failure barriers and immutable inputs."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from parallel_runtime import parallel_barrier, command, verify_hashes
from run_dfx import sha


class Safety(unittest.TestCase):
    def test_parallel_whole_branches_no_interstage_barrier(self):
        a_past_synthesis=threading.Event()
        b_past_synthesis=threading.Event()
        def a():
            a_past_synthesis.set()
            # A must proceed to its next stage while B is still synthesizing.
            self.assertFalse(b_past_synthesis.is_set())
            return 'a placed'
        def b():
            self.assertTrue(a_past_synthesis.wait(3))
            time.sleep(.05)
            b_past_synthesis.set()
            return 'b placed'
        self.assertEqual(parallel_barrier({'a':a,'b':b}),{'a':'a placed','b':'b placed'})

    def test_failure_waits_for_successful_sibling_and_blocks_next_stage(self):
        preserved=threading.Event()
        downstream=[]
        def fail(): raise RuntimeError('deliberate branch failure')
        def success():
            time.sleep(.05);preserved.set();return 'immutable artifact'
        with self.assertRaisesRegex(RuntimeError,'barrier failed'):
            parallel_barrier({'a':fail,'b':success})
            downstream.append('route or bitstream')
        self.assertTrue(preserved.is_set())
        self.assertEqual(downstream,[])

    def test_private_workspaces_and_refuse_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            code='from pathlib import Path; import os,time; Path("owner").write_text(str(os.getpid())); time.sleep(.1)'
            results=parallel_barrier({k:lambda k=k:command([sys.executable,'-c',code],root/k) for k in ('a','b')})
            self.assertNotEqual((root/'a/owner').read_text(),(root/'b/owner').read_text())
            self.assertGreater(min(r['ended_epoch'] for r in results.values())-max(r['started_epoch'] for r in results.values()),0)
            with self.assertRaises(FileExistsError):command([sys.executable,'-c','raise Exception()'],root/'a')
            self.assertEqual(json.loads((root/'a/process.json').read_text())['exit_code'],0)

    def test_changed_input_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'routed.dcp';p.write_bytes(b'immutable checkpoint')
            expected={str(p):sha(p)};verify_hashes(expected)
            p.write_bytes(b'changed checkpoint')
            with self.assertRaisesRegex(ValueError,'Immutable input changed'):verify_hashes(expected)


if __name__=='__main__':unittest.main()
