"""Bounded, progressive WebP cache; never writes source traces or experiment files."""
from collections import OrderedDict, deque
import fcntl
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

from PIL import Image, ImageDraw

COLORS = {'LUT':'#55baf2','FF':'#60dc91','DSP':'#ff9d47','BRAM':'#d68bff',
          'URAM':'#ff6490','CARRY':'#f4d35e','MUX':'#b3a5ed',
          'LUTRAM/SRL':'#44ddd0','OTHER':'#b9c3cf'}
VERSION = 'compact-webp-640x900-v1'
W, H = 640, 900


def progressive_order(names):
    """Cover the whole timeline first, then fill every gap; never omit a frame."""
    if not names:
        return []
    order=[names[0]]
    if len(names)>1:order.append(names[-1])
    intervals=deque([(0,len(names)-1)])
    while intervals:
        lo,hi=intervals.popleft()
        if hi-lo<2:continue
        mid=(lo+hi)//2
        order.append(names[mid])
        intervals.extend([(lo,mid),(mid,hi)])
    return order


class Budget:
    """Cooperative limits apply only to the rendering thread, not Vivado."""
    def __init__(self, cpu_fraction=.25, read_mib_s=2):
        if not 0 < cpu_fraction <= 1 or read_mib_s <= 0:
            raise ValueError("Invalid render budget")
        self.cpu_fraction = cpu_fraction
        self.read_rate = read_mib_s * 1024**2
        self.start = time.monotonic()
        self.cpu = time.thread_time()

    def pace(self, compressed_bytes=0):
        target = max((time.thread_time()-self.cpu)/self.cpu_fraction,
                     compressed_bytes/self.read_rate)
        delay = target - (time.monotonic()-self.start)
        if delay > 0:
            time.sleep(delay)


def render(source, types, meta, budget=None):
    """Same 4x-X geometry as the existing preview/group overlay; stream all cells."""
    x0,x1,y0,y1 = meta['bounds']
    scale = min((W-95)/(4*(x1-x0)), (H-40)/(y1-y0))
    left = (W-(x1-x0)*4*scale)/2
    X = lambda x: round(left+(x-x0)*4*scale)
    Y = lambda y: round(H-20-(y-y0)*scale)
    im = Image.new('RGB', (W,H), '#111923')
    draw = ImageDraw.Draw(im)
    prev = None
    batch = []
    counts = {'source_points':0, 'drawn_points':0, 'hidden_unknown_virtual':0}

    def flush():
        if not batch:
            return
        draw.point(batch, fill=COLORS[prev])
        if prev in ('DSP','BRAM','URAM'):
            for x,y in batch:
                draw.rectangle((x,y,x+1,y+1), fill=COLORS[prev])

    with source.open('rb') as raw, gzip.open(raw, 'rt') as stream:
        for line in stream:
            v = line.split(maxsplit=2)
            if not v:
                continue
            if len(v)<2:
                raise ValueError('Incomplete coordinate row')
            x,y = float(v[0]),float(v[1])
            if not (math.isfinite(x) and math.isfinite(y)):
                raise ValueError('Non-finite coordinate')
            counts['source_points'] += 1
            kind = types.get(v[2].strip()) if len(v)>2 else None
            if kind in COLORS:
                if kind != prev or len(batch)>=2048:
                    flush(); batch=[]; prev=kind
                batch.append((X(x),Y(y)))
                counts['drawn_points'] += 1
            else:
                counts['hidden_unknown_virtual'] += 1
            if budget and counts['source_points'] % 2048 == 0:
                budget.pace(raw.tell())
        flush()
    for x in meta['cr_x']:
        draw.line((X(x),Y(y0),X(x),Y(y1)),fill='#697c90')
    for y in meta['cr_y']:
        draw.line((X(x0),Y(y),X(x1),Y(y)),fill='#697c90')
    for y in meta['slr']:
        draw.line((X(x0),Y(y),X(x1),Y(y)),fill='#ffd166',width=2)
    ys=[y0,*meta['slr'],y1]
    for i in range(len(ys)-1):
        draw.text((X(x1)+5,Y((ys[i]+ys[i+1])/2)),f'SLR {i}',fill='#ffd166')
    buf=io.BytesIO()
    im.save(buf, format='WEBP', quality=75, method=2)
    if budget:
        budget.pace(source.stat().st_size)
    return buf.getvalue(), counts


class CompactPreview:
    def __init__(self, sources, types, meta, root, config_path,
                 limit_bytes=32*1024**2, start_worker=True,
                 cpu_fraction=.25, read_mib_s=2, progressive=False):
        self.sources = OrderedDict((p.name,p) for p in sources)
        Budget(cpu_fraction,read_mib_s)  # Validate before starting the worker.
        self.cpu_fraction, self.read_mib_s = cpu_fraction, read_mib_s
        self.render_order = progressive_order(list(self.sources)) if progressive else list(self.sources)
        self.progressive = progressive
        self.types, self.meta, self.root = types, meta, Path(root)
        self.limit = int(limit_bytes)
        if self.limit < 1024**2:
            raise ValueError('Preview limit must be at least 1 MiB')
        self.root.mkdir(parents=True,exist_ok=True)
        self.identity = hashlib.sha256((VERSION+str(self.sources)+
            hashlib.sha256(Path(config_path).read_bytes()).hexdigest()+
            json.dumps({k:meta[k] for k in ('bounds','cr_x','cr_y','slr')},sort_keys=True)).encode()).hexdigest()
        self.lock=threading.RLock()
        self.pending=OrderedDict()
        self.entries={}
        self.cursor=0
        self.stop=threading.Event()
        self.worker_state={'state':'starting'}
        self.last_swap=None
        manifest=self.root/'index.json'
        if manifest.exists():
            previous=json.loads(manifest.read_text())
            if previous.get('identity') != self.identity:
                raise ValueError('Different cache identity; use a new cache directory')
            self.entries=previous.get('entries',{})
        # Only files created by this cache namespace may be evicted.
        allowed={self.target(n).name for n in self.sources}
        unexpected=[p.name for p in self.root.iterdir() if p.name not in allowed|{'index.json','worker.json','metadata.json.gz'}]
        if unexpected:
            raise ValueError('Unexpected files in managed cache: '+repr(unexpected[:5]))
        for name in list(self.entries):
            if name not in self.sources or not self.valid(name):
                self.entries.pop(name,None)
        # Recover a crash after atomic image publication but before index update.
        for name in self.sources:
            p=self.target(name)
            if p.exists() and name not in self.entries:
                p.unlink()
        self.evict(0)
        self.persist()
        if start_worker:
            self.thread=threading.Thread(target=self.work,daemon=True,name='compact-preview')
            self.thread.start()

    def target(self, name):
        if name not in self.sources:
            raise ValueError('Unknown frame')
        return self.root/(hashlib.sha256(name.encode()).hexdigest()+'.webp')

    def signature(self,name):
        s=self.sources[name].stat()
        return [s.st_size,s.st_mtime_ns]

    def valid(self,name):
        e=self.entries.get(name)
        return bool(e and e['source']==self.signature(name) and self.target(name).is_file())

    def used(self):
        return sum(p.stat().st_blocks*512 for p in self.root.iterdir() if p.is_file())

    def evict(self, incoming, keep=None):
        # Reserve 512 KiB for bounded index/status and atomic metadata writes.
        capacity=self.limit-512*1024
        for name in sorted(self.entries,key=lambda n:self.entries[n].get('access',0)):
            if self.used()+incoming<=capacity:
                break
            if name==keep:
                continue
            self.target(name).unlink(missing_ok=True)
            del self.entries[name]
        if self.used()+incoming>capacity:
            raise ValueError('Preview cannot fit within configured cache quota')

    def persist(self):
        p=self.root/'index.json';tmp=p.with_suffix('.tmp')
        tmp.write_text(json.dumps(dict(identity=self.identity,version=VERSION,limit_bytes=self.limit,
                                      entries=self.entries),separators=(',',':')))
        tmp.replace(p)

    def status(self):
        with self.lock:
            return dict(self.worker_state,ready=len(self.entries),total=len(self.sources),
                        bytes=self.used(),limit_bytes=self.limit,pending=len(self.pending),
                        size=[W,H],cpu_target=self.cpu_fraction,read_mib_s=self.read_mib_s,
                        progressive=self.progressive)

    def manifest(self):
        with self.lock:
            return dict(index=dict(identity=self.identity,version=VERSION,
                                   entries={n:dict(e) for n,e in self.entries.items()}),
                        state=self.status())

    def request(self,name,priority=True):
        with self.lock:
            if name not in self.sources:
                raise ValueError('Unknown frame')
            if self.valid(name):
                self.entries[name]['access']=time.time()
                return self.target(name).read_bytes()
            if len(self.pending)<16 or name in self.pending:
                self.pending[name]=None
                if priority:
                    self.pending.move_to_end(name,last=False)
            return None

    def publish(self,name,body,counts,signature):
        with self.lock:
            if signature!=self.signature(name):
                raise ValueError('Source frame changed while rendering')
            self.evict(((len(body)+4095)//4096)*4096,keep=name)
            p=self.target(name);tmp=p.with_suffix('.tmp')
            tmp.write_bytes(body);tmp.replace(p)
            self.entries[name]=dict(source=signature,bytes=len(body),access=time.time(),**counts)
            self.persist()

    def pressure(self):
        info={k:int(v.split()[0]) for k,v in
              (s.split(':',1) for s in Path('/proc/meminfo').read_text().splitlines())}
        if info['MemAvailable']<16*1024**2:
            return 'available_memory_below_16_GiB'
        if shutil.disk_usage(self.root).free<8*1024**3:
            return 'disk_free_below_8_GiB'
        vm=dict(s.split() for s in Path('/proc/vmstat').read_text().splitlines())
        swap=int(vm['pswpin'])+int(vm['pswpout'])
        previous=self.last_swap;self.last_swap=swap
        if previous is not None and swap>previous:
            return 'active_swapping'
        return None

    def note(self,**values):
        with self.lock:
            self.worker_state.update(values)
            p=self.root/'worker.json';tmp=p.with_suffix('.tmp')
            tmp.write_text(json.dumps(self.status(),indent=2));tmp.replace(p)

    def work(self):
        try:
            tid=threading.get_native_id()
            os.setpriority(os.PRIO_PROCESS,tid,19)
            cpus=os.sched_getaffinity(tid)
            cpu=23 if 23 in cpus else max(cpus)
            os.sched_setaffinity(tid,{cpu})
            subprocess.run(['ionice','-c','3','-p',str(tid)],check=True,capture_output=True)
            self.note(tid=tid,cpu=cpu,nice=os.getpriority(os.PRIO_PROCESS,tid),io_class='idle',state='idle')
            # Shared lock ensures baseline and SLL renderers never run concurrently.
            with (self.root.parent/'render.lock').open('a') as common:
                while not self.stop.is_set():
                    reason=self.pressure()
                    if reason:
                        self.note(state='paused',reason=reason);self.stop.wait(5);continue
                    try:
                        fcntl.flock(common,fcntl.LOCK_EX|fcntl.LOCK_NB)
                    except BlockingIOError:
                        self.stop.wait(1);continue
                    name=None
                    try:
                        with self.lock:
                            while self.pending and name is None:
                                candidate,_=self.pending.popitem(last=False)
                                if not self.valid(candidate):name=candidate
                            while name is None and self.cursor<len(self.sources):
                                candidate=self.render_order[self.cursor];self.cursor+=1
                                if not self.valid(candidate):name=candidate
                        if name is not None:
                            signature=self.signature(name)
                            self.note(state='rendering',frame=name,reason=None)
                            begin=time.monotonic()
                            body,counts=render(self.sources[name],self.types,self.meta,Budget(self.cpu_fraction,self.read_mib_s))
                            self.publish(name,body,counts,signature)
                            self.note(state='idle',last_seconds=time.monotonic()-begin)
                        else:
                            self.note(state='idle',reason='all_frames_scanned')
                    finally:
                        fcntl.flock(common,fcntl.LOCK_UN)
                    # Let the other group take its turn; request threads stay responsive.
                    self.stop.wait(1 if name else 3)
        except Exception as error:
            self.note(state='failed',error=str(error))
