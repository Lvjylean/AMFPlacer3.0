import copy
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from run_dfx_multi import check_synthesis_resume
from run_dfx import sha


class SynthesisResume(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        source = self.path / 'top.v'
        source.write_text('module top(); endmodule\n')
        self.job = dict(part='part', top='top', clock_name='clk', clock_period_ns=10,
                        partitions=[], vivado='/vivado', vivado_threads=4,
                        rtl_sources=[str(source)], ip_scripts=[], xdc_files=[],
                        prepare_worker_shells=True, resume_synthesis_run=str(self.path))
        self.old = dict(schema='amf-dfx-multi-shell-v1', job=copy.deepcopy(self.job),
                        input_hashes={str(source): sha(source)})
        (self.path / 'manifest.json').write_text(json.dumps(self.old))
        (self.path / 'stage_times.tsv').write_text('synthesis\t10.5\t0\nwrite_synth_dcp\t1.2\t0\n')
        (self.path / 'base_synth.dcp').write_bytes(b'checkpoint fixture')

    def test_verified_parent_records_original_time_and_hash(self):
        value = check_synthesis_resume(self.job)
        self.assertFalse(value['synthesis_rerun'])
        self.assertEqual(value['inherited_stage_seconds']['synthesis'], 10.5)
        self.assertEqual(value['checkpoint_sha256'], sha(self.path / 'base_synth.dcp'))

    def test_clock_or_design_identity_change_rejected(self):
        for key, value in [('clock_period_ns', 9), ('top', 'other'), ('partitions', [dict(id='other')])]:
            job = copy.deepcopy(self.job)
            job[key] = value
            with self.assertRaises(ValueError):
                check_synthesis_resume(job)

    def test_modified_rtl_rejected(self):
        Path(self.job['rtl_sources'][0]).write_text('module other(); endmodule\n')
        with self.assertRaises(ValueError):
            check_synthesis_resume(self.job)

    def test_failed_checkpoint_stage_rejected(self):
        (self.path / 'stage_times.tsv').write_text('synthesis\t10.5\t0\nwrite_synth_dcp\t1.2\t1\n')
        with self.assertRaises(ValueError):
            check_synthesis_resume(self.job)

    def test_tcl_defines_main_without_executing_design_commands(self):
        script = (Path(__file__).resolve().parents[2] / 'scripts/dfx_multi_shell.tcl').read_text()
        definition = '\n'.join(script.split('if {[catch {main}')[0].splitlines()[2:])
        path = self.path / 'scope_check.tcl'
        path.write_text(definition + '\nputs [expr {[llength [info procs main]] == 1}]\n')
        self.assertEqual(subprocess.check_output(['tclsh', str(path)], text=True), '1\n')


if __name__ == '__main__':
    unittest.main()
