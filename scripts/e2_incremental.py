"""SLL resource leases and plan transport for frozen-shell incremental placement.

Static and partition-interface nets stay unchanged. Only wholly active RM nets
are planned. Parallel RMs receive disjoint physical channel/entrance components
inside their actual DFX routing footprints, after subtracting routed shell use.
"""
import csv, hashlib, json, os, shutil, subprocess, time
from pathlib import Path

HELPERS = ('sll_routes.tcl','sll_parallel.tcl','sll_parallel_worker.tcl',
           'sll_parallel.py','dfx_sll.tcl')
def enabled(cfg):
    return cfg.get('SllPlanningEnable','false') == 'true'
def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def channels(path):
    lines = Path(path).read_text().splitlines()
    return list(csv.DictReader((s for s in lines if not s.startswith('#')),delimiter='\t'))
def allocate(rows, used, footprints):
    """Lease connected resource components; reverse directions share one lease."""
    paths={}; eligible={}
    for r in rows:
        paths.setdefault(r['resource'],set()).update(r['segment'].split(','))
    parent={k:k for k in paths}
    def find(k):
        while parent[k]!=k:
            parent[k]=parent[parent[k]];k=parent[k]
        return k
    node_owner={}
    for resource,nodes in sorted(paths.items()):
        for n in nodes:
            if n in node_owner:
                a,b=find(resource),find(node_owner[n])
                if a!=b:parent[max(a,b)]=min(a,b)
            else:node_owner[n]=resource
    groups={}
    for r in paths:groups.setdefault(find(r),set()).add(r)
    loads={k:0 for k in footprints};owned={k:set() for k in footprints}
    blocked=set()
    for _,resources in sorted(groups.items()):
        nodes=set().union(*(paths[r] for r in resources))
        if nodes & used:
            blocked.update(resources);continue
        tiles={n.split('/')[0] for n in nodes}
        candidates=[k for k,fp in footprints.items() if tiles <= fp]
        if not candidates:
            blocked.update(resources);continue
        owner=min(candidates,key=lambda k:(loads[k],k))
        owned[owner].update(resources);loads[owner]+=len(resources)
    all_resources=set(paths)
    reserved={k:all_resources-v for k,v in owned.items()}
    return reserved,{'physical_resources':len(all_resources),'unavailable':len(blocked),
                     'leased':{k:len(v) for k,v in owned.items()},
                     'policy':'exclusive-connected-resource-components; balanced overlapping footprints'}
def prepare(job,cfg,out,scripts):
    if not enabled(cfg):return None
    from run_dfx import tcl_word
    from sll_planning_inputs import validate
    validate(cfg,job['amf_binary'])
    out=Path(out);out.mkdir()
    checkpoint=Path(job['common_static_shell_run'])/'common_static_locked.dcp'
    settings=out/'settings.tcl'
    parts=[[p['id'],p['active_partition'],p['pblock']] for p in job['partitions']]
    settings.write_text(''.join('set dfx('+k+') '+tcl_word(v)+'\n' for k,v in {
        'output':str(out),'static_checkpoint':str(checkpoint),
        'sll_channel_file':cfg['SllChannelFile'],'rps':' '.join(tcl_word(' '.join(tcl_word(v) for v in row)) for row in parts),
        'vivado_threads':job['vivado_threads']}.items()))
    begin=time.monotonic()
    command=[job['vivado'],'-mode','batch','-notrace','-source',str(Path(scripts)/'dfx_sll_inventory.tcl'),'-tclargs',str(settings)]
    with (out/'inventory.log').open('w') as log:
        p=subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT,timeout=2400)
    if p.returncode:raise RuntimeError('SLL frozen-shell inventory failed: '+str(out/'inventory.log'))
    used=set((out/'used_nodes.txt').read_text().splitlines())
    footprints={p['id']:set((out/(p['id']+'-tiles.txt')).read_text().splitlines()) for p in job['partitions']}
    reservations,summary=allocate(channels(cfg['SllChannelFile']),used,footprints)
    paths={}
    for k,resources in reservations.items():
        path=out/(k+'-reserved.txt');path.write_text('\n'.join(sorted(resources))+'\n')
        paths[k]={'path':str(path),'sha256':digest(path)}
    result={'schema':1,'checkpoint':str(checkpoint),'checkpoint_sha256':digest(checkpoint),
            'channel_file':cfg['SllChannelFile'],'channel_sha256':digest(cfg['SllChannelFile']),
            'reservations':paths,'summary':summary,'command':command,'exit_code':p.returncode,
            'seconds':time.monotonic()-begin}
    (out/'contract.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
def bind(job,cfg):
    if not enabled(cfg):return cfg
    entry=job.get('e2_sll_reservation')
    if not entry or digest(entry['path'])!=entry['sha256']:
        raise ValueError('Incremental E2 requires a verified frozen-shell SLL lease')
    cfg['SllIncrementalScope']='internal-active-nets'
    cfg['SllReservedResourceFile']=entry['path']
    return cfg
def publish(cfg,out):
    if not enabled(cfg):return
    out=Path(out);plan=out/'placement/sll-plan.tcl'
    if not plan.is_file():raise ValueError('E2 enabled but AMF SLL plan is missing')
    shutil.copy2(plan,out/'sll-plan.tcl')
    (out/'sll-plan.json').write_text(json.dumps({'schema':1,'sha256':digest(plan),
        'scope':'internal-active-nets','workers':int(cfg.get('SllConstraintWorkers','4')),
        'reservation_sha256':digest(cfg['SllReservedResourceFile'])},indent=2)+'\n')
def copy_plan(source,dest):
    source,dest=Path(source),Path(dest)
    metadata=source/'sll-plan.json'
    if not metadata.exists():return
    record=json.loads(metadata.read_text())
    if digest(source/'sll-plan.tcl')!=record['sha256']:raise ValueError('SLL worker plan changed')
    for name in ('sll-plan.tcl','sll-plan.json'):shutil.copy2(source/name,dest/name)
