#!/usr/bin/env python3
"""Compile the feature fixture against frozen libraries without changing a build snapshot."""
import argparse,hashlib,json,os,shlex,subprocess,time
from pathlib import Path

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary-dir',type=Path,required=True)
    ap.add_argument('--fixture-config',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();root=Path(__file__).resolve().parents[2];b=args.binary_dir.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    source=root/'src/tests/check_feature_integration.cc'
    commands=subprocess.check_output(['ninja','-t','commands','checkFeatureIntegration'],cwd=b,text=True).splitlines()
    compile=shlex.split(next(x for x in commands if ' -c ' in x and 'check_feature_integration.cc' in x))
    obj=str(out/'fixture.o');exe=str(out/'checkFeatureIntegration')
    compile[compile.index('-o')+1]=obj;compile[compile.index('-c')+1]=str(source)
    if '-MF' in compile:compile[compile.index('-MF')+1]=str(out/'fixture.d')
    link=shlex.split(next(x for x in commands if ' -o checkFeatureIntegration ' in x))
    link=link[link.index('/usr/bin/c++'):]
    if '&&' in link:link=link[:link.index('&&')]
    link[link.index('-o')+1]=exe
    link=[obj if x.endswith('check_feature_integration.cc.o') else x for x in link]
    manifest={'production_build':str(b),'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
      'test_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
      'fixture_sha256':hashlib.sha256(args.fixture_config.read_bytes()).hexdigest(),
      'amf_binary_sha256':hashlib.sha256((b/'AMFPlacer').read_bytes()).hexdigest(),'commands':[],'state':'running'}
    for name,cmd in [('compile',compile),('link',link),('run',[exe,str(args.fixture_config),str(out/'result')])]:
        start=time.monotonic()
        with (out/(name+'.log')).open('w') as f:r=subprocess.run(cmd,cwd=b,stdout=f,stderr=subprocess.STDOUT,env=dict(os.environ,OMP_NUM_THREADS='4'),timeout=300)
        manifest['commands'].append({'name':name,'command':cmd,'exit_code':r.returncode,'seconds':time.monotonic()-start})
        manifest['state']='failed' if r.returncode else 'passed' if name=='run' else 'running'
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(name,r.returncode,flush=True)
        if r.returncode:return 1
    return 0
if __name__=='__main__':raise SystemExit(main())
