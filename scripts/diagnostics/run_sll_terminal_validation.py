#!/usr/bin/env python3
"""Record and run one serial Vivado batch validation; DCPs remain on server."""
import argparse, datetime, hashlib, json, os, pathlib, shutil, subprocess, time

def sha(p):
    h=hashlib.sha256()
    with pathlib.Path(p).open('rb') as f:
        for b in iter(lambda:f.read(8*1024*1024),b''): h.update(b)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--script',required=True);p.add_argument('--out',required=True)
    p.add_argument('--input',action='append',default=[]);p.add_argument('args',nargs='*');a=p.parse_args()
    out=pathlib.Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
    script=pathlib.Path(a.script).resolve();repo=pathlib.Path(__file__).resolve().parents[2]
    def write(n,v): (out/n).write_text(json.dumps(v,indent=2)+'\n')
    manifest={'created':datetime.datetime.now().astimezone().isoformat(),'source_repository':str(repo),
      'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
      'source_status':subprocess.check_output(['git','status','--short'],cwd=repo,text=True),
      'inputs':{str(x):sha(x) for x in [script,*a.input]},'dcp_storage':'server-only'}
    (out/'source.diff').write_bytes(subprocess.check_output(['git','diff'],cwd=repo))
    (out/'inputs').mkdir()
    shutil.copy2(script,out/'inputs'/script.name)
    for i,x in enumerate(a.input):
        q=pathlib.Path(x)
        if q.suffix.lower() not in {'.dcp','.zip'} and q.stat().st_size<8*1024*1024:
            shutil.copy2(q,out/'inputs'/f'{i:02d}-{q.name}')
    cmd=['/Projects/Xilinx26/2026.1/Vivado/bin/vivado','-mode','batch','-notrace','-nojournal',
         '-log',str(out/'vivado_internal.log'),'-source',str(script),'-tclargs',str(out/'reports'),*a.args]
    manifest['command']=cmd;write('manifest.json',manifest);write('status.json',{'state':'running'})
    env=dict(os.environ);env.setdefault('LM_LICENSE_FILE','/Projects/jinyang/licenses/vivado_2026.lic')
    start=time.monotonic()
    with (out/'vivado.log').open('w') as f:
        rc=subprocess.run(cmd,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
    result={'state':'completed' if rc==0 else 'failed','exit_code':rc,'elapsed_seconds':time.monotonic()-start}
    write('status.json',result)
    print(json.dumps(result),flush=True)
    raise SystemExit(rc)

if __name__=='__main__': main()
