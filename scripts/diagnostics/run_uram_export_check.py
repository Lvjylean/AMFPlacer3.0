"""Compile the export regression against an immutable validated build's production libraries."""
import argparse,hashlib,json,shlex,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser()
for key in ('build','fixture','output'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[2];source=root/'src/tests/check_uram_cascade_export.cc'
commands=subprocess.check_output(['ninja','-t','commands','checkFFControlPolarity'],cwd=a.build,text=True).splitlines()
compile_command=next(c for c in commands if ' -c ' in c and 'check_ff_control_polarity.cc' in c)
argv=shlex.split(compile_command);argv[argv.index('-c')+1]=str(source)
old_object=argv[argv.index('-o')+1];obj=a.output.resolve()/'check.o';argv[argv.index('-o')+1]=str(obj)
if '-MF' in argv:argv[argv.index('-MF')+1]=str(a.output.resolve()/'check.d')
link=next(c for c in commands if ' -o checkFFControlPolarity ' in c)
exe=a.output.resolve()/'checkURAMExport'
link=link.replace(old_object,shlex.quote(str(obj))).replace(' -o checkFFControlPolarity ',' -o '+shlex.quote(str(exe))+' ')
t=time.time();subprocess.run(argv,cwd=a.build,check=True);subprocess.run(['bash','-c',link],cwd=a.build,check=True)
subprocess.run([str(exe),str(a.fixture.resolve()),str(a.output.resolve())],check=True)
(a.output/'manifest.json').write_text(json.dumps({'production_build':str(a.build.resolve()),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'compile':argv,'link':link,'wall_seconds':time.time()-t},indent=2)+'\n')
