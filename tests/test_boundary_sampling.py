import csv
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

class PlacementAudit(unittest.TestCase):
    def test_driver_pin_clocks_and_external_ports_are_not_missing_cells(self):
        script=Path(__file__).resolve().parents[1]/'scripts/diagnostics/analyze_boundary_timing_samples.py'
        spec=importlib.util.spec_from_file_location('boundary_samples',script)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'placement').mkdir();(root/'reports/physical').mkdir(parents=True)
            model=root/'model.tsv'
            model.write_text('SITE\tS0\tSLICEL\t0\t0\t0\nSITE\tS1\tSLICEL\t0\t2\t1\nBOUNDARY\tb0\tSLR\tY\t1\t-1\t1\t1.5\t1\n')
            (root/'placement/routed_cell_sites.tsv').write_text('cell\tsite\nsource\tS0\nsink\tS1\n')
            clocks=root/'clocks';clocks.write_text('@PORT/ap_clk\nclock_buffer/O\nlegacy_clock_net\n')
            netlist=root/'netlist.zip'
            with zipfile.ZipFile(netlist,'w') as z:
                z.writestr('netlist',
                    'curCell=> source type=> LUT1\ncurCell=> sink type=> FDRE\n'
                    'pin=> sink/C refpin=> C dir=> IN net=> n568507 drivepin=> @PORT/ap_clk\n'
                    'pin=> sink/C2 refpin=> C dir=> IN net=> n42 drivepin=> clock_buffer/O\n'
                    'pin=> sink/C3 refpin=> C dir=> IN net=> legacy_clock_net drivepin=> old_buffer/O\n'
                    'pin=> sink/R refpin=> R dir=> IN net=> reset drivepin=> @PORT/ap_rst\n'
                    'pin=> sink/D refpin=> D dir=> IN net=> data drivepin=> source/O\n'
                    'pin=> sink/CE refpin=> CE dir=> IN net=> enable drivepin=> missing/O\n'
                    'pin=> sink/I0 refpin=> I0 dir=> IN net=> gnd drivepin=> constant/G\n'
                    'curCell=> constant type=> GND\n')
            report=module.audit_placements(root,model,netlist,clocks)
            self.assertEqual(report['excluded_input_edges'],{'clock_edges':3,'external_port_edges_without_fabric_site':1,'constant_edges':1})
            routed=report['stages']['routed']
            self.assertEqual(routed['driver_sink_edges'],1)
            self.assertEqual(routed['slr_crossings'],1)
            self.assertEqual(routed['missing_site_edges'],1)
            self.assertEqual(report['schema'],'backend-boundaries-v2')

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
