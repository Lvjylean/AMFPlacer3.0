#!/usr/bin/env python3
"""Finite collector for two already-running native audit exports."""
import datetime
import json
from pathlib import Path
import subprocess
import sys
import time

root=Path(sys.argv[1]); script=Path(__file__).with_name('compare_native_logic_audit.py')
stages={'input':root/'native-v2/input','routed':root/'native-final/routed'}
processes={};done=set();started=time.monotonic()
state={}


def save():
    (root/'native-comparison-status.json').write_text(json.dumps({'updated':datetime.datetime.now().astimezone().isoformat(),'stages':state},indent=2))


try:
    while len(done)<2:
        if time.monotonic()-started>10800:raise TimeoutError('Native audit exports/comparison exceeded 3 hours')
        for stage,directory in stages.items():
            if stage in done:continue
            if stage in processes:
                code=processes[stage].poll()
                if code is not None:
                    if code:raise RuntimeError(f'{stage} normalization failed ({code}); inspect normalize-{stage}.log')
                    done.add(stage);state[stage]='normalized';save()
            elif (directory/'complete.tsv').exists():
                log=(root/'logs'/('normalize-'+stage+'.log')).open('w')
                processes[stage]=subprocess.Popen(['nice','-n','10',sys.executable,str(script),'normalize',str(directory)],stdout=log,stderr=subprocess.STDOUT)
                state[stage]='normalizing';save()
            else:
                state[stage]='waiting_for_export'
                logpath=root/'logs'/('native-v3-'+stage+'.log')
                if logpath.exists() and 'Exiting Vivado' in logpath.read_text()[-2000:]:
                    raise RuntimeError(f'{stage} export exited without completion')
        save();time.sleep(3)
    log=(root/'logs/native-comparison.log').open('w')
    subprocess.run([sys.executable,str(script),'compare',str(stages['input']),str(stages['routed']),str(root/'native-comparison.json')],stdout=log,stderr=subprocess.STDOUT,check=True)
    state['comparison']='complete';save()
except Exception as error:
    state['error']=str(error);save();raise
