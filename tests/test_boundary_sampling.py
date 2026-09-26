import csv
from pathlib import Path
import subprocess
import tempfile
import unittest

class SamplingCache(unittest.TestCase):
    def test_repeated_path_connection_reuses_query_and_keeps_each_path(self):
        script=Path(__file__).resolve().parents[1]/'scripts/diagnostics/export_boundary_timing_samples.tcl'
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            stub=r'''
set argc 0
set argv {}
set queries 0
proc get_timing_paths {args} {return {path0 path1}}
proc get_pins {args} {
    if {[lsearch -exact $args {DIRECTION == OUT}]>=0} {return {source[0]/O}}
    return {sink[0]/I0}
}
proc get_nets {args} {return net0}
proc get_clocks {args} {return {}}
proc get_cells {args} {return [lindex $args end]}
proc get_net_delays {args} {incr ::queries;return delay0}
proc version {args} {return test}
proc current_design {} {return design0}
proc get_property {key object} {
    switch $key {
        SLACK {return -1}
        DATAPATH_DELAY {return 11}
        STARTPOINT_PIN {return {source[0]/O}}
        ENDPOINT_PIN {return {sink[0]/I0}}
        REF_NAME {return LUT1}
        LOC {return SLICE_X0Y0}
        PART {return testpart}
        SLOW_MAX {return 1500}
        default {error "unhandled property $key"}
    }
}
'''
            check=root/'check.tcl'
            check.write_text(stub+'\nsource {'+str(script)+'}\nexport_boundary_timing_samples {'+str(root)+'}\nif {$queries != 1} {error "Repeated delay query"}\n')
            subprocess.run(['tclsh',str(check)],check=True,capture_output=True,text=True)
            with (root/'timing_connections.tsv').open() as f:records=list(csv.DictReader(f,delimiter='\t'))
            self.assertEqual([r['path'] for r in records],['0','1'])
            self.assertEqual([r['routed_delay_ns'] for r in records],['1.5','1.5'])
            self.assertEqual(records[0]['source_pin'],'source[0]/O')
