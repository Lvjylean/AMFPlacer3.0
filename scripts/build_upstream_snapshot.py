"""Build an unchanged upstream Git source snapshot without touching its checkout."""
import datetime as dt
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot_source(repository, revision, destination):
    repository, destination = Path(repository).resolve(), Path(destination).resolve()
    commit = subprocess.check_output(['git', '-C', str(repository), 'rev-parse', '--verify', revision+'^{commit}'], text=True).strip()
    names = subprocess.check_output(['git', '-C', str(repository), 'ls-tree', '-r', '--name-only', commit], text=True).splitlines()
    required = ['src'] + [n for n in ('LICENSE', 'NOTICE', 'README.MD', 'doc/NotoSans-Regular.ttf') if n in names]
    if 'src/CMakeLists.txt' not in names:
        raise ValueError('Upstream revision does not contain src/CMakeLists.txt')
    destination.mkdir(parents=True, exist_ok=False)
    archive = destination/'upstream-source.tar'
    subprocess.run(['git', '-C', str(repository), 'archive', '--format=tar', '--output='+str(archive), commit, '--', *required], check=True)
    with tarfile.open(archive) as stream:
        for member in stream.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or not (member.isfile() or member.isdir()):
                raise ValueError('Unsafe or unsupported upstream archive entry: '+member.name)
        stream.extractall(destination)
    hashes = {str(p.relative_to(destination)): sha(p) for p in sorted((destination/'src').rglob('*')) if p.is_file()}
    return dict(repository=str(repository), commit=commit, revision=revision,
                archive_sha256=sha(archive), files=hashes,
                source_tree=subprocess.check_output(['git','-C',str(repository),'rev-parse',commit+':src'],text=True).strip())


def build_upstream(project, repository, revision, jobs):
    project=Path(project)
    stamp=dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    directory=project/'builds'/('upstream-amf2-'+stamp)
    snapshot=snapshot_source(repository, revision, directory)
    record=dict(schema='pristine-upstream-build-v1', build_id=directory.name,
                state='building', source_commit=snapshot['commit'], upstream=snapshot,
                source_snapshot=str(directory/'src'), source_modified=False,
                runtime_profiling=False, jobs=jobs, stages=[],
                created=dt.datetime.now().astimezone().isoformat(), sets_current=False)
    def save():
        temporary=directory/'manifest.json.tmp'
        temporary.write_text(json.dumps(record,indent=2)+'\n')
        temporary.replace(directory/'manifest.json')
    (directory/'source_hashes.json').write_text(json.dumps(snapshot['files'],indent=2)+'\n')
    print(directory,flush=True)
    save()
    try:
        record['tool_versions']={n:subprocess.check_output(c,text=True).strip() for n,c in
            [('cmake',['cmake','--version']),('cxx',['c++','--version']),('ninja',['ninja','--version'])]}
        commands=[('configure',['cmake','-S',str(directory/'src'),'-B',str(directory/'build'),'-G','Ninja']),
                  ('compile',['cmake','--build',str(directory/'build'),'--parallel',str(jobs),'--target','AMFPlacer','partitionHyperGraph'])]
        for name,command in commands:
            started=time.monotonic()
            with (directory/(name+'.log')).open('w') as output:
                result=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT)
            record['stages'].append(dict(name=name,command=command,exit_code=result.returncode,elapsed_seconds=time.monotonic()-started))
            save()
            if result.returncode:
                raise RuntimeError('Upstream '+name+' failed: '+str(directory/(name+'.log')))
        changed=[name for name,value in snapshot['files'].items() if sha(directory/name)!=value]
        if changed:
            raise RuntimeError('Build modified upstream source files: '+repr(changed[:10]))
        record['binaries']={n:sha(directory/'build'/n) for n in ('AMFPlacer','partitionHyperGraph')}
        record['dependencies']={}
        for path in (directory/'build/PaToH').glob('*'):
            if path.is_file():record['dependencies'][str(path.relative_to(directory))]=sha(path)
        eigen=directory/'build/_deps/eigen-src'
        if eigen.is_dir():
            record['eigen_commit']=subprocess.check_output(['git','-C',str(eigen),'rev-parse','HEAD'],text=True).strip()
        record.update(state='completed',finished=dt.datetime.now().astimezone().isoformat())
        save()
        print(json.dumps(dict(state='completed',directory=str(directory),binary=str(directory/'build/AMFPlacer'))),flush=True)
    except Exception as error:
        record.update(state='failed',error=str(error),finished=dt.datetime.now().astimezone().isoformat())
        save()
        raise
