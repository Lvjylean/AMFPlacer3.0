import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/diagnostics'))
from audit_amf2_original_connectivity import compare


class OriginalConnectivity(unittest.TestCase):
    def check(self, wrong_driver=False):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            (root/'binding.json').write_text('{"input_sha256":"fixture"}')
            (root/'cells.tsv').write_text('cell_id\tcell_name\tprimitive\n0\tdsp\tDSP48E2\n1\tff\tFDRE\n2\tgnd\tGND\n')
            (root/'nets.tsv').write_text('net_id\tdrivers\tsinks\tinouts\n'
                'n0\t0:P[0]\t1:D\t\nn1\t0:PCOUT[0]\t\t\nn2\t2:G\t1:R\t\n')
            driver='dsp/DSP_OUTPUT_INST/P[1]' if wrong_driver else 'dsp/DSP_OUTPUT_INST/P[0]'
            with zipfile.ZipFile(root/'netlist.zip','w') as z:
                z.writestr('netlist','curCell=> dsp type=> DSP48E2\n'
                    'pin=> dsp/P[0] refpin=> P[0] dir=> OUT net=> n drivepin=> dsp/DSP_OUTPUT_INST/P[0]\n'
                    'pin=> dsp/PCOUT[0] refpin=> PCOUT[0] dir=> OUT net=>  drivepin=> \n'
                    'curCell=> ff type=> FDRE\n'
                    f'pin=> ff/D refpin=> D dir=> IN net=> n drivepin=> {driver}\n'
                    'pin=> ff/R refpin=> R dir=> IN net=> c drivepin=> gnd/G\n')
            return compare(root,root/'netlist.zip')

    def test_internal_alias_and_dangling_output_do_not_change_leaf_connectivity(self):
        result=self.check()
        self.assertEqual(result['state'],'matched')
        self.assertEqual(result['internal_output_aliases'],1)
        self.assertEqual(result['counts']['extra_constant_output_pins'],1)
        self.assertEqual(result['counts']['dangling_output_net_representation_only'],1)

    def test_real_driver_difference_still_fails(self):
        result=self.check(wrong_driver=True)
        self.assertEqual(result['state'],'mismatch')
        self.assertEqual(result['counts']['mismatches'],1)

    def test_bidirectional_iobuf_keeps_its_output_driver(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            (root/'binding.json').write_text('{"input_sha256":"fixture"}')
            (root/'cells.tsv').write_text('cell_id\tcell_name\tprimitive\n0\tio\tIOBUFE3\n')
            (root/'nets.tsv').write_text('net_id\tdrivers\tsinks\tinouts\nn0\t\t\t0:IO\n')
            with zipfile.ZipFile(root/'netlist.zip','w') as z:
                z.writestr('netlist','curCell=> io type=> IOBUFE3\n'
                    'pin=> io/IO refpin=> IO dir=> INOUT net=> pad drivepin=> io/OBUFT_INST/O\n')
            self.assertEqual(compare(root,root/'netlist.zip')['state'],'matched')
