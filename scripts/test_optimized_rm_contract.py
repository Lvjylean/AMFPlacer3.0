import json,tempfile,unittest
from pathlib import Path
from run_dfx import sha,save
from optimized_rm_contract import verify_optimized

class Contract(unittest.TestCase):
    def test_reject_incomplete_changed_or_wrong_clock(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);source=p/'synth.dcp';source.write_bytes(b'synth')
            output=p/'opt.dcp';output.write_bytes(b'optimized')
            job=dict(part='test-part',clock_period_ns=20,vivado_threads=4,vivado='/test/vivado')
            script=Path(__file__).parent/'dfx_rm_opt.tcl'
            data=dict(job,state='completed',input=str(source),input_sha256=sha(source),output=str(output),output_sha256=sha(output),
                      process=dict(exit_code=0,command=[job['vivado'],'-source',str(script)]))
            path=p/'manifest.json';save(path,data)
            (p/'stage_times.tsv').write_text('open_rm_synthesis\t1\t0\nrm_ooc_opt\t1\t0\nwrite_optimized_rm\t1\t0\n')
            (p/'validation.tsv').write_text('interface_exact\t1\nunresolved_blackboxes\t0\nopt_executed\t1\nport_bits\t4\nclock_period_ns\t20\n')
            for n in ('ports_before.tcldata','ports_after.tcldata'):(p/n).write_text('a IN b OUT')
            verify_optimized(path,job)
            with self.assertRaisesRegex(ValueError,'clock_period_ns'):verify_optimized(path,dict(job,clock_period_ns=10))
            data['process']['exit_code']=2;save(path,data)
            with self.assertRaisesRegex(ValueError,'did not complete'):verify_optimized(path,job)
            data['process']['exit_code']=0;save(path,data)
            (p/'ports_after.tcldata').write_text('a IN')
            with self.assertRaisesRegex(ValueError,'changed interface'):verify_optimized(path,job)
            (p/'ports_after.tcldata').write_text('a IN b OUT');output.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'artifact changed'):verify_optimized(path,job)

if __name__=='__main__':unittest.main()
