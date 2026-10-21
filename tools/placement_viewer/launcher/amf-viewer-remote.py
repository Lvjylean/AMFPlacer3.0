"""Discover recorded placements and reuse loopback-only viewer services."""
import json, os, socket, subprocess, time, sys
from pathlib import Path
from urllib.request import urlopen
PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("AMF_VIEWER_PROJECT_ROOT", str(PACKAGE.parent.parent))).resolve()
VIEWER = PACKAGE / "placement_viewer.py"


def select_viewer(config):
    if str(config.get("device", "")).upper() == "VHK158" and not config.get("visualization device metadata file"):
        raise ValueError("VHK158 缺少全器件几何元数据，无法用旧 U250 查看器打开")
    return VIEWER

def trace_files(directory):
    return [p for p in directory.glob("DumpAllCoordTrace-*.gz") if p.is_file()]

def resolve_trace(run):
    # Prefer the new layout only once it contains published frames.
    for trace in (run/"placement"/"visualization", run/"placement"):
        if trace_files(trace):
            return trace
    raise ValueError("No trajectory in placement/visualization or placement")

def discover():
    import runpy
    directory = PACKAGE / "launcher"
    links_path = directory / "artifact_links.json"
    links = json.loads(links_path.read_text()) if links_path.exists() else []
    result = runpy.run_path(str(directory / "device_catalog.py"))["collect"](
        ROOT, links, host=os.environ.get("AMF_VIEWER_HOST_LABEL", "local"))
    return result["runs"]

def start(name):
    run=Path(name).resolve()
    if ROOT not in run.parents or not (run/"config.json").is_file(): raise ValueError("Invalid run")
    trace=resolve_trace(run)
    viewer=select_viewer(json.loads((run/"config.json").read_text()))
    if not viewer.is_file(): raise RuntimeError(f"查看器程序不存在：{viewer}")
    free=None
    for port in range(8766,8800):
        with socket.socket() as sock:
            sock.settimeout(.1)
            occupied=sock.connect_ex(("127.0.0.1",port))==0
        if not occupied:
            if free is None and port>=8768: free=port
            continue
        try:
            with urlopen(f"http://127.0.0.1:{port}/api/frames",timeout=2) as r: data=json.load(r)
            if data.get("directory")==str(trace): return dict(port=port,directory=str(trace),reused=True)
        except Exception: pass
    if free is None: raise RuntimeError("No free viewer port")
    logpath=run/"viewer-launcher.log"
    with logpath.open("ab") as log:
        p=subprocess.Popen([sys.executable,str(viewer),"--traces",str(trace),"--config",str(run/"config.json"),"--port",str(free)],stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
    for _ in range(40):
        if p.poll() is not None: raise RuntimeError(f"Viewer exited; see {logpath}")
        try:
            with urlopen(f"http://127.0.0.1:{free}/api/frames",timeout=2) as r: data=json.load(r)
            if data.get("directory")==str(trace): return dict(port=free,directory=str(trace),reused=False)
        except Exception: time.sleep(.5)
    raise RuntimeError(f"Viewer not ready; see {logpath}")
if __name__=="__main__":
    try:
        print(json.dumps(start(sys.argv[1]) if len(sys.argv)>1 else discover()))
    except Exception as error:
        print("AMF_VIEWER_ERROR: " + str(error), file=sys.stderr)
        sys.exit(1)
