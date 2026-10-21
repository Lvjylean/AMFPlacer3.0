"""Read-only device/DCP catalog. Never loads a checkpoint into Vivado."""
import json, os, time, zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

SKIP = {'src','builds','build','data','inputs','work','reports','.git','viewer','logs',
        'viewer-exports','node_modules','__pycache__','before','overlay','binary','binary-04','binary-07'}


def read_json(path):
    try:
        if path.stat().st_size > 4*1024*1024: return {}
        value = json.loads(path.read_text())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError): return {}


def device_for_part(part):
    part = (part or '').lower()
    for prefix, device in [('xcu250','U250'),('xcvh1582','VHK158'),('xcvu095','VU095')]:
        if part.startswith(prefix): return device
    return ''


def checkpoint(path, host='local'):
    result = {'path':str(path),'host':host,'name':path.name,'part':'','verified':False}
    try:
        st = path.stat();result.update(bytes=st.st_size,modified=st.st_mtime)
        with zipfile.ZipFile(path) as z:
            info = z.getinfo('dcp.xml')
            if info.file_size > 1024*1024: raise ValueError('oversized dcp.xml')
            root = ET.fromstring(z.read(info))
        node = root.find('Part');top = root.find('Top')
        result.update(part=node.get('Name','') if node is not None else '',
                      top=top.get('Name','') if top is not None else '', verified=True,
                      verification='DCP dcp.xml',checked_at=time.time())
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, ET.ParseError) as error:
        result['error'] = str(error)
    return result


def classify(config, artifacts):
    declared = str(config.get('device','')).upper()
    config_part = config.get('physical device part','')
    dcp_parts = {a['part'].lower() for a in artifacts if a.get('verified') and a.get('part')}
    dcp_devices = {device_for_part(p) or p for p in dcp_parts}
    config_device = device_for_part(config_part) or declared
    if len(dcp_devices)>1 or (dcp_devices and config_device and config_device not in dcp_devices):
        return {'device':'UNCLASSIFIED','part':', '.join(sorted(dcp_parts)),
                'classification_source':'DCP/config conflict','classification_warning':'器件信息冲突，需核对'}
    device = next(iter(dcp_devices),config_device or 'UNCLASSIFIED')
    if config_part and declared and device_for_part(config_part) and declared!=device_for_part(config_part):
        return {'device':'UNCLASSIFIED','part':config_part,'classification_source':'config conflict',
                'classification_warning':'配置中的 device 与 part 不一致'}
    return {'device':device,'part':next(iter(sorted(dcp_parts)),config_part),
            'classification_source':'DCP dcp.xml + config' if dcp_parts else 'config.json',
            'classification_warning':'' if device!='UNCLASSIFIED' else '缺少器件信息，未按实验名推测'}


def collect(root, links=(), host="local"):
    started=time.monotonic();root=Path(root);records=[];warnings=[]
    by_run={}
    for link in links:by_run.setdefault(link['run'],[]).append(link['artifact'])
    for base,dirs,files in os.walk(root/'experiments'):
        dirs[:]=[d for d in dirs if d not in SKIP and not d.startswith('.viewer')]
        run=Path(base)
        if run.parent.name!='runs' or 'config.json' not in files:continue
        # Run outputs do not contain nested experiment roots.
        dirs[:]=[]
        config=read_json(run/'config.json')
        trace=None;frames=[]
        for sub in ['placement/visualization','placement']:
            paths=[p for p in (run/sub).glob('DumpAllCoordTrace-*.gz') if p.is_file()]
            if paths:trace=run/sub;frames=paths;break
        paths=[]
        for folder in [run/'reports',run/'joint',run]:
            paths.extend(p for p in folder.glob('*.dcp') if p.is_file())
        artifacts=[checkpoint(p, host=host) for p in sorted(set(paths))]
        artifacts.extend(by_run.get(str(run),[]))
        if not frames and not artifacts:continue
        artifacts.sort(key=lambda a:('routed' not in a['name'].lower(),a['name']))
        status=read_json(run/'status.json');preview=False
        if trace:
            preview=any((trace/n/'manifest.json').exists() for n in ['.viewer-preview-v1','.viewer-preview-hard-resources-v1'])
        modified=max([p.stat().st_mtime for p in frames]+[a.get('modified',0) for a in artifacts]+[0])
        records.append(dict(run=str(run),name=run.name,host=host,frames=len(frames),
            state=status.get('state','unknown'),directory=str(trace) if trace else None,
            preview=preview,modified=modified,artifacts=artifacts,**classify(config,artifacts)))
    records.sort(key=lambda r:r['modified'],reverse=True)
    return {'schema':'amf-device-catalog-v1','generated_at':time.time(),'scan_seconds':time.monotonic()-started,
            'scope':str(root/'experiments') + '; optional explicitly linked checkpoints',
            'runs':records,'warnings':warnings}


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--links');p.add_argument('--host',default='local');p.add_argument('--output',required=True);a=p.parse_args()
    links=json.loads(Path(a.links).read_text()) if a.links else []
    result=collect(Path(a.root),links,host=a.host);Path(a.output).write_text(json.dumps(result,ensure_ascii=False,indent=2))
    print(json.dumps({'runs':len(result['runs']),'dcps':sum(len(r['artifacts']) for r in result['runs']),
                      'seconds':result['scan_seconds']}))
