"""Non-blocking descriptive reports after verified bitstream completion.

The main manifest is immutable here. Report failure never becomes a successful
report, nor changes the already recorded implementation acceptance or wall time.
"""
import argparse,datetime,json,subprocess,sys,time
from pathlib import Path
import shutil
from run_dfx import save,sha
from parallel_runtime import command,require_memory,verify_hashes

SCRIPTS=Path(__file__).resolve().parent

def completed_parent(path):
    p=Path(path).resolve();m=json.loads(p.read_text())
    if m.get('state')!='completed' or not m.get('ended_epoch') or m.get('wall_seconds') is None:
        raise ValueError('Background reports require a recorded completed main flow')
    return p,m

def dispatch_after_completion(source,output,parent_manifest):
    parent,m=completed_parent(parent_manifest)
    out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    record=dict(state='dispatching',parent_manifest=str(parent),parent_manifest_sha256=sha(parent),
                main_completed_epoch=m['ended_epoch'],main_wall_seconds=m['wall_seconds'],
                excluded_from_main_flow=True,started_epoch=time.time())
    save(out/'dispatch.json',record)
    try:
        args=[sys.executable,str(SCRIPTS/'run_auxiliary_reports.py'),'--source-joint',str(Path(source).resolve()),
              '--output',str(out/'run'),'--parent-manifest',str(parent),'--parent-sha256',record['parent_manifest_sha256']]
        with (out/'controller.log').open('w') as log:
            child=subprocess.Popen(args,cwd=out,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        record.update(state='dispatched',pid=child.pid,process_group=child.pid,command=args)
    except Exception as error:
        record.update(state='dispatch_failed',error=repr(error))
    save(out/'dispatch.json',record)
    return record

def run(source,output,parent_manifest,parent_sha256):
    from run_parallel_acceptance import source_contract
    import run_dfx_multi as flow
    started=time.monotonic();out=Path(output).resolve();out.mkdir(parents=True,exist_ok=False)
    state=dict(state='running',started_epoch=time.time(),excluded_from_main_flow=True,read_only_dcp=True)
    save(out/'manifest.json',state)
    try:
        parent,m=completed_parent(parent_manifest)
        verify_hashes({str(parent):parent_sha256})
        source,job,routed,reference,hashes=source_contract(source)
        hashes[str(parent)]=parent_sha256
        state.update(parent_manifest=str(parent),main_completed_epoch=m['ended_epoch'],main_wall_seconds=m['wall_seconds'],
                     source_joint=str(source),input_hashes=hashes,resources=require_memory(16))
        frozen=out/'inputs';frozen.mkdir()
        for name in ('dfx_auxiliary_reports.tcl','dfx_common.tcl'):shutil.copy2(SCRIPTS/name,frozen/name)
        state['script_hashes']={p.name:sha(p) for p in frozen.iterdir()}
        settings=flow.settings(job,out,dict(routed_checkpoint=str(routed)))
        state['process']=command([job['vivado'],'-mode','batch','-source',str(frozen/'dfx_auxiliary_reports.tcl'),
                                  '-tclargs',str(settings)],out/'exec',timeout=7200)
        names=['utilization.rpt','hierarchical_utilization.rpt','critical_paths.rpt']+[p['id']+'_boundary_paths.rpt' for p in job['partitions']]
        for name in names:
            if not (out/name).is_file() or not (out/name).stat().st_size:raise ValueError('Missing background report: '+name)
        verify_hashes(hashes)
        state.update(state='completed',input_hashes_unchanged=True,outputs={n:sha(out/n) for n in names})
    except Exception as error:
        state.update(state='failed',error=repr(error));raise
    finally:
        state.update(ended_epoch=time.time(),wall_seconds=time.monotonic()-started)
        save(out/'manifest.json',state)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-joint',required=True);parser.add_argument('--output',required=True)
    parser.add_argument('--parent-manifest',required=True);parser.add_argument('--parent-sha256',required=True)
    args=parser.parse_args();run(args.source_joint,args.output,args.parent_manifest,args.parent_sha256)
