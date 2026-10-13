#!/usr/bin/env python3
"""Record load during one already-running profile and finalize its reports."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
from summarize_runtime_profile import summarize


def now(): return datetime.now().astimezone().isoformat()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('run',type=Path)
    args=p.parse_args();run=args.run.resolve()
    for name in ('summarize_runtime_profile.py','finish_runtime_profile.py'):
        shutil.copy2(Path(__file__).with_name(name),run/'inputs'/name)
    hashes={name:hashlib.sha256((run/'inputs'/name).read_bytes()).hexdigest()
            for name in ('summarize_runtime_profile.py','finish_runtime_profile.py')}
    manifest=json.loads((run/'manifest.json').read_text())
    def processes():
        result=[]
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit():continue
            try:
                if proc.stat().st_uid!=os.getuid():continue
                command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
                if manifest['binary'] not in command or str(run/'config.json') not in command:continue
                s=(proc/'stat').read_text();fields=s[s.rfind(')')+2:].split()
                status=(proc/'status').read_text()
                result.append(dict(pid=int(proc.name),state=fields[0],
                    cpu_s=(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'),
                    rss_kib=int(fields[21])*os.sysconf('SC_PAGE_SIZE')/1024,
                    threads=int(next(x.split(':')[1] for x in status.splitlines() if x.startswith('Threads:')))))
            except (OSError,ValueError,StopIteration):continue
        return result
    outcome=dict(state='collecting',started=now(),collector_sha256=hashes)
    target=run/'reports/profiling_supervisor.json'
    target.write_text(json.dumps(outcome,indent=2))
    try:
        with (run/'reports/profile_resource_samples.jsonl').open('a') as stream:
            while True:
                status=json.loads((run/'status.json').read_text())
                mem={x.split(':')[0]:x.split(':')[1].strip() for x in Path('/proc/meminfo').read_text().splitlines()}
                sample=dict(time=now(),load_average=Path('/proc/loadavg').read_text().strip(),
                            mem_available=mem['MemAvailable'],processes=processes(),state=status['state'])
                stream.write(json.dumps(sample)+'\n');stream.flush()
                if status['state']!='running':break
                time.sleep(15)
        if status['state']!='completed':raise RuntimeError('AMF did not complete: '+str(status))
        result=summarize(run)
        outcome.update(state='completed',finished=now(),
                       profile_sha256=result['profile_sha256'],report=str(run/'reports/profile_summary.md'))
    except Exception as error:
        outcome.update(state='failed',finished=now(),error=str(error));raise
    finally:
        target.write_text(json.dumps(outcome,indent=2))


if __name__=='__main__': main()
