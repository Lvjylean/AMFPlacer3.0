#!/usr/bin/env python3
"""Prepare one reusable cache. Playback never writes this cache."""
import argparse,gzip,json,struct,hashlib,os,time,math
from pathlib import Path
from placement_viewer import frames,load_metadata
p=argparse.ArgumentParser();p.add_argument('--traces',type=Path,required=True);p.add_argument('--config',type=Path,required=True);a=p.parse_args()
root=a.traces/'.viewer-binary-v1';root.mkdir(exist_ok=True)
source=frames(a.traces)
signature={'schema':1,'config_sha256':hashlib.sha256(a.config.read_bytes()).hexdigest(),'frames':[[p.name,p.stat().st_size,p.stat().st_mtime_ns] for p in source]}
manifest=root/'manifest.json'
if manifest.exists():
 old=json.loads(manifest.read_text())
 if old['signature']==signature and all((root/n).exists() for n in old['files']):
  print('Existing complete cache reused; no writes');raise SystemExit
 raise SystemExit('Source differs from existing cache; refusing to overwrite')
lock=root/'build.lock';fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY);os.close(fd)
started=time.monotonic();types,metadata=load_metadata(a.config);ids={};names=[];kinds=[];files=[];counts=[]
try:
 for index,path in enumerate(source):
  dest=root/(path.name+'.bin.gz');tmp=dest.with_suffix('.tmp');n=0
  with gzip.open(path,'rt') as inp,gzip.open(tmp,'wb',compresslevel=3) as out:
   for line in inp:
    f=line.split(maxsplit=2)
    if not f:continue
    name=f[2].strip() if len(f)>2 else ''
    if name not in ids:
     ids[name]=len(names);names.append(name);kinds.append(types.get(name,'UNKNOWN') if name else 'VIRTUAL')
    x,y=float(f[0]),float(f[1])
    if not (math.isfinite(x) and math.isfinite(y)):raise ValueError('Non-finite coordinate in '+path.name)
    out.write(struct.pack('<Iff',ids[name],x,y));n+=1
  tmp.replace(dest);files.append(dest.name);counts.append(n)
  print(index+1,len(source),path.name,n,flush=True)
 dest=root/'dictionary.json.gz'
 with gzip.open(dest,'wt',compresslevel=3) as f:json.dump({'names':names,'types':kinds},f,separators=(',',':'))
 files.append(dest.name)
 result={'signature':signature,'files':files,'counts':counts,'dictionary_count':len(names),'original_bytes':sum(p.stat().st_size for p in source),'cache_bytes':sum((root/f).stat().st_size for f in files),'seconds':time.monotonic()-started}
 tmp=root/'manifest.tmp';tmp.write_text(json.dumps(result,indent=2));tmp.replace(manifest);print(json.dumps({k:v for k,v in result.items() if k not in ('signature','files','counts')}))
finally:lock.unlink()
