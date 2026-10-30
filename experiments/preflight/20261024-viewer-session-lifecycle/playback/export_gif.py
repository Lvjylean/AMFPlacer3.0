#!/usr/bin/env python3
import argparse,gzip,json,struct,time
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from placement_viewer import load_metadata
from trace_stages import trace_stages
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);p.add_argument('--speed',type=float,choices=[0.5,1,1.5,2,5],default=2);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
cache=a.run/'placement/.viewer-binary-v1';manifest=json.loads((cache/'manifest.json').read_text());d=json.loads(gzip.decompress((cache/'dictionary.json.gz').read_bytes()));_,meta=load_metadata(a.run/'config.json')
stages=trace_stages(a.run/'logs/amf.log');duration=round(600/a.speed/10)*10
frame_count=len(manifest['signature']['frames'])
colors={'LUT':'#55baf2','FF':'#60dc91','DSP':'#ff9d47','BRAM':'#d68bff','URAM':'#ff6490','CARRY':'#f4d35e','MUX':'#b3a5ed','LUTRAM/SRL':'#44ddd0','OTHER':'#b9c3cf'}
W,H=760,1000;bg='#111923';palette=[bg,'#e2ebf4','#697c90','#ffd166',*colors.values()];pal=Image.new('P',(1,1));rgb=[]
for c in palette:rgb.extend(tuple(bytes.fromhex(c[1:])))
pal.putpalette(rgb+[0]*(768-len(rgb)))
try:font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
except OSError:font=ImageFont.load_default()
x0,x1,y0,y1=meta['bounds'];sx=3.4;sy=.83;left=(W-(x1-x0)*sx)/2;bottom=930
X=lambda x:round(left+(x-x0)*sx)
Y=lambda y:round(bottom-(y-y0)*sy)
images=[];start=time.monotonic()
for index,(name,_,_) in enumerate(manifest['signature']['frames']):
 im=Image.new('RGB',(W,H),bg);dr=ImageDraw.Draw(im);dr.text((22,12),f'AMF3 GETRF | {a.speed:g}x | 10 ns | CR-SLR hierarchical',font=font,fill=palette[1]);dr.text((22,37),f'Frame {index+1}/{frame_count}  {name}',font=font,fill=palette[1])
 dr.text((22,108),stages.get(name,{}).get('label','Stage unavailable'),font=font,fill=palette[1])
 for k,(t,c) in enumerate(colors.items()):dr.text((22+(k%5)*147,65+(k//5)*22),t,font=font,fill=c)
 groups={t:[] for t in colors}
 data=gzip.decompress((cache/(name+'.bin.gz')).read_bytes())
 for cell,x,y in struct.iter_unpack('<Iff',data):
  t=d['types'][cell]
  if t in groups:groups[t].append((X(x),Y(y)))
 for t in ['OTHER','LUT','FF','LUTRAM/SRL','CARRY','MUX','DSP','BRAM','URAM']:
  dr.point(groups[t],fill=colors[t])
  if t in ('DSP','BRAM','URAM'):
   for x,y in groups[t]:dr.rectangle((x,y,x+1,y+1),fill=colors[t])
 for x in meta['cr_x']:dr.line((X(x),Y(y0),X(x),Y(y1)),fill=palette[2])
 for y in meta['cr_y']:dr.line((X(x0),Y(y),X(x1),Y(y)),fill=palette[2])
 for y in meta['slr']:dr.line((X(x0),Y(y),X(x1),Y(y)),fill=palette[3],width=2)
 ys=[y0,*meta['slr'],y1]
 for i in range(4):dr.text((X(x1)+8,Y((ys[i]+ys[i+1])/2)),f'SLR {i}',font=font,fill=palette[3])
 dr.text((22,960),f'AMF coordinates | {duration} ms/frame | horizontal display expanded',font=font,fill=palette[1])
 images.append(im.quantize(palette=pal,dither=0));print(index+1,flush=True)
a.output.parent.mkdir(parents=True,exist_ok=True)
temporary=a.output.with_suffix(".tmp")
images[0].save(temporary,format="GIF",save_all=True,append_images=images[1:],duration=duration,loop=0,optimize=False,disposal=2)
with Image.open(temporary) as im:
 assert im.n_frames==frame_count
 assert all((im.seek(i) is None and im.info['duration']==duration) for i in range(im.n_frames))
temporary.replace(a.output)
print(json.dumps({'file':str(a.output),'bytes':a.output.stat().st_size,'frames':frame_count,'duration_ms':duration*frame_count,'seconds':time.monotonic()-start}),flush=True)
