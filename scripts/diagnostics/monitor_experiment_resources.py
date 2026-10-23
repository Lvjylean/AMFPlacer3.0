#!/usr/bin/env python3
"""Read-only resource samples for one owned experiment runner and descendants."""
import argparse
import datetime as dt
import json
from pathlib import Path
import time

def process(pid):
    p=Path("/proc")/str(pid)
    try:
        stat=p.joinpath("stat").read_text().rsplit(")",1)[1].split()
        status={}
        for line in p.joinpath("status").read_text().splitlines():
            key,_,value=line.partition(":")
            if key in ("Name","State","VmRSS","VmHWM","VmSize","Threads"):
                status[key]=value.strip()
        return dict(pid=pid,ppid=int(stat[1]),start_ticks=int(stat[19]),
                    user_ticks=int(stat[11]),system_ticks=int(stat[12]),
                    command=p.joinpath("cmdline").read_bytes().replace(b"\0",b" ").decode(errors="replace"),
                    status=status,
                    children=[int(s) for s in p.joinpath("task",str(pid),"children").read_text().split()])
    except (FileNotFoundError,ProcessLookupError,PermissionError):
        return None

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--pid",type=int,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--interval",type=float,default=10)
    args=parser.parse_args()
    first=process(args.pid)
    if not first: raise SystemExit("Runner already exited")
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("x") as f:
        while True:
            parent=process(args.pid)
            if not parent or parent["start_ticks"]!=first["start_ticks"]:
                f.write(json.dumps(dict(time=dt.datetime.now().astimezone().isoformat(),state="runner-exited"))+"\n")
                break
            records=[parent];pending=list(parent["children"]);seen={args.pid}
            while pending:
                pid=pending.pop()
                if pid in seen: continue
                seen.add(pid);item=process(pid)
                if item:records.append(item);pending.extend(item["children"])
            memory={key:value.strip() for key,_,value in
                    (line.partition(":") for line in Path("/proc/meminfo").read_text().splitlines())
                    if key in ("MemTotal","MemAvailable","SwapFree")}
            oom=[line for line in Path("/proc/vmstat").read_text().splitlines() if line.startswith("oom_kill ")]
            f.write(json.dumps(dict(time=dt.datetime.now().astimezone().isoformat(),
                                   processes=records,memory=memory,oom_kill=oom))+"\n")
            f.flush();time.sleep(args.interval)

if __name__=="__main__":main()

