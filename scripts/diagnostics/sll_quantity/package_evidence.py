#!/usr/bin/env python3
"""Stream an explicit report/config whitelist to stdout. Never read a DCP."""
import io,json,sys,tarfile
from pathlib import Path

root=Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01')
files={}
for name in ('manifest.json','comparison-final-launch.json'):
    files[name]=root/name
for name in ('results.json','model-tests-cmake.json','model-tests-sanitized.json',
             'release-source-verification.json','source-protection-check.json',
             'original-independent-count.json','pilot-disabled-reproduction.json',
             'pilot-run-index-corrections.json','build-transactional.log'):
    files['validation/'+name]=root/'validation'/name
for case in ('original','disabled-final','release-control-final','quantity-100-final'):
    directory=root/'cases'/case
    worker=json.loads((directory/'worker.json').read_text())
    assert worker['state']=='completed' and worker['exit_code']==0 and len(worker['runs'])==1
    files[f'cases/{case}/worker.json']=directory/'worker.json'
    for name in ('parameters.tsv','mapping.tsv','stages.tsv','qp.tsv'):
        if case!='original':files[f'cases/{case}/sll/{name}']=directory/'sll'/name
    run=Path(worker['runs'][0])
    files[f'cases/{case}/run-manifest.json']=run/'manifest.json'
    files[f'cases/{case}/global-placement.json']=run/'reports/global_placement.json'
    files[f'cases/{case}/effective-config.json']=run/'config.json'
    config=Path(worker['command'][worker['command'].index('--config')+1])
    files[f'cases/{case}/user-config.json']=config
with tarfile.open(fileobj=sys.stdout.buffer,mode='w|gz') as archive:
    for name,path in files.items():
        assert path.is_file() and path.suffix in ('.json','.tsv','.log') and '..' not in Path(name).parts
        archive.add(path,arcname=name,recursive=False)
