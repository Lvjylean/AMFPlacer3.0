"""Join a selected site fixture with the original exported AMF connectivity."""
import json
from pathlib import Path
import sys
import zipfile

run,out=map(Path,sys.argv[1:])
cells={}
for line in (out/'sites.tsv').read_text().splitlines():
    site,kind,name,target=line.split('\t')
    cells[name]=dict(site=site,kind=kind,bel=target.split('/')[1],pins={})
cfg=json.loads((run/'config.json').read_text())
with zipfile.ZipFile(cfg['vivado extracted design information file']) as z:
    with z.open(z.namelist()[0]) as f:
        current=None
        for raw in f:
            if raw.startswith(b'curCell=> '):
                fields=raw.decode().split();current=cells.get(fields[1])
                if current is not None:current['type']=fields[3]
            elif current is not None and b'refpin=>' in raw:
                fields=raw.decode().split()
                current['pins'][fields[3]]=dict(direction=fields[5],net=fields[7],driver=fields[9])
(out/'netlist_cells.json').write_text(json.dumps(cells,indent=2)+'\n')
for site in sorted({c['site'] for c in cells.values()}):
    print(site)
    for name,c in sorted(cells.items(),key=lambda item:item[1]['bel']):
        if c['site']!=site:continue
        ds=c['pins'].get('D',{}).get('driver','')
        if ds:
            parent=ds.rsplit('/',1)[0]
            if parent in cells:ds=cells[parent]['site']+'/'+cells[parent]['bel']+':'+ds.rsplit('/',1)[1]
            else:ds=ds.rsplit('/',2)[-2:];ds='/'.join(ds)
        print(c['bel'],c['type'],name.rsplit('/',1)[-1], 'D='+ds,
              'R='+c['pins'].get('R',c['pins'].get('S',{})).get('driver','').rsplit('/',1)[-1])
