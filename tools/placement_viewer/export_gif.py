#!/usr/bin/env python3
import argparse,gzip,json,struct,time
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from placement_viewer import load_metadata
from trace_stages import trace_stages
from boundary_view import paint_overview
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--traces',type=Path);p.add_argument('--config',type=Path);p.add_argument('--speed',type=float,choices=[0.5,1,1.5,2,5],default=2);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
traces=a.traces or a.run/'placement/visualization';cache=traces/'.viewer-binary-v1';manifest=json.loads((cache/'manifest.json').read_text());d=json.loads(gzip.decompress((cache/'dictionary.json.gz').read_bytes()));_,meta=load_metadata(a.config or a.run/'config.json')
stages=trace_stages(a.run/'logs/amf.log');duration=round(600/a.speed/10)*10
frame_count=len(manifest['signature']['frames'])
colors={'LUT':'#55baf2','FF':'#60dc91','DSP':'#ff9d47','BRAM':'#d68bff','URAM':'#ff6490','CARRY':'#f4d35e','MUX':'#b3a5ed','LUTRAM/SRL':'#44ddd0','OTHER':'#b9c3cf'}
W,H=760,1000;bg='#111923';palette=[bg,'#e2ebf4','#697c90','#ffd166',*colors.values(),'#151e2b','#69809a','#223449','#435d78','#f4be62','#9bb0c5','#202735','#18222e','#bd9bff','#59d5d1','#f3b873','#f482a7'];pal=Image.new('P',(1,1));rgb=[]
for c in palette:rgb.extend(tuple(bytes.fromhex(c[1:])))
pal.putpalette(rgb+[0]*(768-len(rgb)))
try:font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
except OSError:font=ImageFont.load_default()
x0,x1,y0,y1=meta['bounds'];stretch=meta.get('display_x_stretch',1);sy=min((W-100)/((x1-x0)*stretch),780/(y1-y0));sx=sy*stretch;left=(W-(x1-x0)*sx)/2;bottom=930
X=lambda x:round(left+(x-x0)*sx)
Y=lambda y:round(bottom-(y-y0)*sy)
images=[];start=time.monotonic()
for index,(name,_,_) in enumerate(manifest['signature']['frames']):
 im=Image.new('RGB',(W,H),bg);dr=ImageDraw.Draw(im);dr.text((22,12),f'AMF3 {meta["device"]} | {a.speed:g}x | placement trace',font=font,fill=palette[1]);dr.text((22,37),f'Frame {index+1}/{frame_count}  {name}',font=font,fill=palette[1])
 dr.text((22,108),stages.get(name,{}).get('label','Stage unavailable'),font=font,fill=palette[1])
 for k,(t,c) in enumerate(colors.items()):dr.text((22+(k%5)*147,65+(k//5)*22),t,font=font,fill=c)
 paint_overview(dr,meta,X,Y,font,font)
 groups={t:[] for t in colors}
 data=gzip.decompress((cache/(name+'.bin.gz')).read_bytes())
 for cell,x,y in struct.iter_unpack('<Iff',data):
  t=d['types'][cell]
  if t in groups:groups[t].append((X(x),Y(y)))
 for t in ['OTHER','LUT','FF','LUTRAM/SRL','CARRY','MUX','DSP','BRAM','URAM']:
  dr.point(groups[t],fill=colors[t])
  if t in ('DSP','BRAM','URAM'):
   for x,y in groups[t]:dr.rectangle((x,y,x+1,y+1),fill=colors[t])
 dr.text((22,960),f'AMF coordinates | {duration} ms/frame | device-specific display scale',font=font,fill=palette[1])
 images.append(im.quantize(palette=pal,dither=0));print(index+1,flush=True)
a.output.parent.mkdir(parents=True,exist_ok=True)
temporary=a.output.with_suffix(".tmp")
images[0].save(temporary,format="GIF",save_all=True,append_images=images[1:],duration=duration,loop=0,optimize=False,disposal=2)
with Image.open(temporary) as im:
 assert im.n_frames==frame_count
 assert all((im.seek(i) is None and im.info['duration']==duration) for i in range(im.n_frames))
temporary.replace(a.output)
print(json.dumps({'file':str(a.output),'bytes':a.output.stat().st_size,'frames':frame_count,'duration_ms':duration*frame_count,'seconds':time.monotonic()-start}),flush=True)
