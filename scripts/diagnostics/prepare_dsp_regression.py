#!/usr/bin/env python3
"""Prepare tests from frozen DSP properties/sites; never launches placement."""
import argparse,csv,gzip,hashlib,json,pathlib,zipfile

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
def main():
 ap=argparse.ArgumentParser()
 for key in ['base-config','layout','sites','metadata','outputs','output']:ap.add_argument('--'+key,type=pathlib.Path,required=True)
 a=ap.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 cfg=json.loads(a.base_config.read_text())
 cfg={k:v for k,v in cfg.items() if k in ['device','vivado extracted device information file','cellType2fixedAmo file','cellType2sharedCellType file','sharedCellType2BELtype file','mergedSharedCellType2sharedCellType','clockRegionXNum','clockRegionYNum','clockRegionDSPNum','clockRegionBRAMNum','ClockPeriod','y2xRatio','physical boundary model file','physical device part','PhysicalBoundaryMode']}
 cfg.update(jobs='1',DSPCritical='true',ProPrimitiveTiming='false')
 sites={r['cell']:r for r in csv.DictReader(a.sites.open(),delimiter='\t') if r['ref']=='DSP48E2'}
 metadata=a.metadata.read_text().splitlines();names=[r.split('\t')[0] for r in metadata[4:]]
 assert set(names)==set(sites)
 groups={};group=None;macro=False
 with gzip.open(a.layout,'rt') as f:
  for l in f:
   words=l.split()
   if not words:continue
   if words[0] in ['Macro:','UnpackedCell:']:group=words[1];macro=False
   elif words[0]=='macroType:':macro=words[1]=='PlacementMacroType_DSP'
   elif words[0]=='CellId:' and macro:
    i=words.index('name:');name=words[i+1];assert name in sites
    groups[name]=(group,words[i+2],words[i+3])
   elif words[0]=='cellId:' and words[-1] in sites:groups[words[-1]]=(group,'0','0')
 assert set(groups)==set(sites),(len(groups),len(sites))
 (out/'replay.tsv').write_text(''.join('\t'.join([*groups[n][:1],n,*groups[n][1:],sites[n]['loc']])+'\n' for n in names))
 def archive(path,text):
  with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('allCellPinNet',text)
 archive(out/'coordinates.zip',''.join('curCell=> '+n+' type=> DSP48E2\n' for n in names))
 (out/'coordinates').mkdir()
 coord=dict(cfg,**{'vivado extracted design information file':str(out/'coordinates.zip'),'dumpDirectory':str(out/'coordinates'),'VisualizationTraceFine':'true','DumpAllCoordTrace':'trace'})
 (out/'coordinates.json').write_text(json.dumps(coord,indent=2))
 # One independent test cone per real DSP: FF -> LUT -> DSP -> LUT -> FF.
 # These are synthetic timing edges, not the original GETRF connectivity.
 text=[]
 def cell(n,t,pins):
  text.append('curCell=> '+n+' type=> '+t+'\n')
  for ref,direction,net,driver in pins:text.append(f'   pin=> {n}/{ref} refpin=> {ref} dir=> {direction} net=> {net} drivepin=> {driver}\n')
 for i,n in enumerate(names):
  src,pre,post,sink=[n+s for s in ['_source','_pre','_post','_sink']]
  nets=[f'probe{i}_{j}' for j in range(4)]
  cell(src,'FDRE',[('Q','OUT',nets[0],src+'/Q')])
  cell(pre,'LUT1',[('I0','IN',nets[0],src+'/Q'),('O','OUT',nets[1],pre+'/O')])
  cell(n,'DSP48E2',[('A[0]','IN',nets[1],pre+'/O'),('P[0]','OUT',nets[2],n+'/P[0]')])
  cell(post,'LUT1',[('I0','IN',nets[2],n+'/P[0]'),('O','OUT',nets[3],post+'/O')])
  cell(sink,'FDRE',[('D','IN',nets[3],post+'/O')])
 archive(out/'timing-cones.zip',''.join(text))
 metadata[2]='netlist_sha256 '+sha(out/'timing-cones.zip')
 (out/'timing-cones-properties.tsv').write_text('\n'.join(metadata)+'\n')
 timing=dict(cfg,**{'vivado extracted design information file':str(out/'timing-cones.zip'),'DSP timing properties file':str(out/'timing-cones-properties.tsv'),'DSP timing properties sha256':sha(out/'timing-cones-properties.tsv'),'DSP timing source DCP sha256':metadata[1].split()[1]})
 (out/'timing.json').write_text(json.dumps(timing,indent=2))
 outputs=json.loads(a.outputs.read_text())
 (out/'real-used-outputs.tsv').write_text(''.join('\t'.join([n,*outputs.get(n,[])])+'\n' for n in names))
 (out/'manifest.json').write_text(json.dumps({'purpose':'frozen coordinate replay and synthetic timing cones using all real DCP PREG properties','placement_executed':False,'source_hashes':{str(p):sha(p) for p in [a.layout,a.sites,a.metadata,a.outputs]},'DSPs':len(names),'macros':len(set(x[0] for x in groups.values())),'derived_files':{p.name:sha(p) for p in out.iterdir() if p.is_file()}},indent=2))
 print(json.dumps({'prepared':str(out),'DSPs':len(names),'macros':len(set(x[0] for x in groups.values()))}))
if __name__=='__main__':main()
