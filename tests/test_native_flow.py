from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from run_face_detect_flow import ROUTE_TCL
from run_native_face_detect import native_script, command_seconds


class NativeControl(unittest.TestCase):
    def test_native_wrapper_runs_the_reference_commands_and_writes_timing(self):
        script = native_script(ROUTE_TCL, 'place_design -unplace\nplace_design\nroute_design\n')
        self.assertNotIn('source $placementTcl', script)
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / 'native.tcl').write_text(script)
            harness = root / 'mock_vivado.tcl'
            harness.write_text(r'''
proc set_param {args} {}
proc open_checkpoint {args} {puts "COMMAND open_checkpoint $args"}
proc place_design {args} {puts "COMMAND place_design $args"}
proc route_design {args} {puts "COMMAND route_design $args"}
proc write_checkpoint {args} {puts "COMMAND write_checkpoint $args"}
foreach name {report_timing_summary report_drc report_route_status report_utilization report_bus_skew close_design} {
    proc $name {args} {}
}
set script [lindex $argv 0]
set argv [list input.dcp unused [lindex $argv 1] ""]
source $script
''')
            output = subprocess.check_output(['tclsh', str(harness), str(root / 'native.tcl'), str(root)], text=True)
            commands = [line.strip() for line in output.splitlines() if line.startswith('COMMAND')]
            self.assertEqual(commands[1:4], ['COMMAND place_design -unplace', 'COMMAND place_design', 'COMMAND route_design'])
            times = (root / 'stage_times.tsv').read_text().splitlines()
            self.assertEqual(times[0].split('\t'), ['stage', 'seconds', 'tcl_status'])
            self.assertEqual([row.split('\t')[0] for row in times[1:]],
                             ['open_checkpoint', 'unplace', 'place_design', 'route_design', 'write_checkpoint'])
            self.assertTrue(all(row.split('\t')[2] == '0' for row in times[1:]))

    def test_changed_reference_directives_are_rejected(self):
        with self.assertRaises(ValueError):
            native_script(ROUTE_TCL, 'place_design -unplace\nplace_design -directive Explore\nroute_design\n')

    def test_log_times_use_elapsed_instead_of_cpu(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / 'log'
            path.write_text('place_design: Time (s): cpu = 00:02:58 ; elapsed = 00:01:26 . Memory\n'
                            'route_design: Time (s): cpu = 00:04:14 ; elapsed = 00:01:42 . Memory\n')
            self.assertEqual(command_seconds(path), {'place_design': 86.0, 'route_design': 102.0})


if __name__ == '__main__':
    unittest.main()
