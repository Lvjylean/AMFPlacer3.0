"""Read-only, part- and geometry-bound resource annotation overlay."""
from pathlib import Path
import json

def attach_inventory(metadata):
    path=Path(__file__).with_name('hard_resource_inventory.json')
    if not path.exists(): return
    data=json.loads(path.read_text())
    if (metadata.get('part','').lower()!=data['part'].lower()
            or metadata.get('device_input_sha256')!=data['device_input_sha256']):
        return
    metadata['hard_resources']=data

def paint_resources(draw, metadata, X, Y, font, unit=1):
    data=metadata.get('hard_resources')
    if not data: return
    # Shaded non-SLICE complement; not a fabricated per-IP area partition.
    for region in data['regions']:
        for l,h,b,t in region['bands']:
            draw.rectangle((X(l),Y(t),X(h),Y(b)), fill='#202735')
        l,h,b,t=region['label_bounds']
        left,right,top,bottom=X(l),X(h),Y(t),Y(b)
        width=right-left
        for r in region['resources']:
            for a in r['anchors']:
                x,y=a['position'];px,py=X(x),Y(y)
                radius=2*unit
                draw.ellipse((px-radius,py-radius,px+radius,py+radius),fill=r['color'])
        if width < 38*unit: continue
        n=len(region['resources'])
        for i,r in enumerate(region['resources']):
            cy=top+(bottom-top)*(i+.5)/n
            label=r['kind'];size=min(22*unit,max(10*unit,int(width*.20)))
            label_font=font.font_variant(size=int(size))
            box=draw.textbbox((0,0),label,font=label_font)
            tw,th=box[2]-box[0],box[3]-box[1]
            cx=(left+right)/2
            draw.rounded_rectangle((cx-tw/2-5*unit,cy-th/2-5*unit,cx+tw/2+5*unit,cy+th/2+5*unit),
                                   radius=4*unit,fill='#18222e',outline=r['color'],width=unit)
            draw.text((cx-tw/2-box[0],cy-th/2-box[1]),label,font=label_font,fill=r['color'])
