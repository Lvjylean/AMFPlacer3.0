#!/usr/bin/env python3
"""Launch three finite, independent GP comparisons after a completed standard build."""
import json,subprocess,sys,os
from pathlib import Path
root=Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01')
repo=root/'repo'
build_id=sys.argv[1]
suffix='-'+sys.argv[2] if len(sys.argv)>2 else ''
binary=repo/'builds'/build_id/'build/AMFPlacer'
assert binary.is_file()
base=json.loads((repo/'configs/experiments/sll-quantity-baseline.json').read_text())
assert base['ProPrimitiveTiming']=='false'
launched=[]
for name,policy,enable,weight in [('disabled','preserve','false','0'),('release-control','release-single','false','0'),('quantity-100','release-single','true','100')]:
    name+=suffix
    config=dict(base)
    config.update({'SllQuantityDiagnostics':'true','SllQuantityEnable':enable,'SllQuantityWeight':weight,
                   'SllQuantityTau':'30','SllQuantityGamma':'0.25','SllQuantityCurvature':'0.002',
                   'SllQuantityMaxStep':'10','SllQuantityMaxFanout':'0','SllRegionPolicy':policy,
                   'SllReportDirectory':str(root/'cases'/name/'sll')})
    path=repo/'configs/experiments'/f'sll-{name}.json'
    assert not path.exists()
    path.write_text(json.dumps(config,indent=2)+'\n')
    log=open(root/f'{name}-launch.log','w')
    child=subprocess.Popen([sys.executable,str(root/'run_case.py'),name,str(binary),str(path),'--global-only'],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    launched.append({'name':name,'pid':child.pid,'binary':str(binary),'config':str(path)})
(root/f'comparison{suffix}-launch.json').write_text(json.dumps(launched,indent=2)+'\n')
print(json.dumps(launched,indent=2))
