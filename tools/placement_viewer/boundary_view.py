"""Display-only region envelopes. Raw tile outlines and placement geometry stay intact."""
from collections import defaultdict
from hard_resources import paint_resources


def merge_segments(segments):
    lines = defaultdict(list)
    for x0, y0, x1, y1 in segments:
        if x0 == x1:
            lines['v', x0].append(sorted((y0, y1)))
        elif y0 == y1:
            lines['h', y0].append(sorted((x0, x1)))
        else:
            raise ValueError('Expected an axis-aligned CR envelope')
    result = []
    for (axis, fixed), intervals in sorted(lines.items()):
        merged = []
        for low, high in sorted(intervals):
            if merged and low <= merged[-1][1]:
                merged[-1][1] = max(high, merged[-1][1])
            else:
                merged.append([low, high])
        for low, high in merged:
            result.append([fixed, low, fixed, high] if axis == 'v' else [low, fixed, high, fixed])
    return result


def display_layers(metadata):
    regions = [r for r in metadata['clock_regions'] if r.get('slice_count', 1) > 0]
    segments = []
    for r in regions:
        l, h, b, t = r['bounds']
        segments.extend([[l,b,h,b], [h,b,h,t], [h,t,l,t], [l,t,l,b]])
    slrs = sorted(metadata['slr_regions'], key=lambda r:r['bounds'][2])
    seams = []
    for a, b in zip(slrs, slrs[1:]):
        y = (a['bounds'][3]+b['bounds'][2])/2
        if abs(a['bounds'][3]-b['bounds'][2]) > 1e-6:
            raise ValueError('Nonadjacent SLRs: no synthetic seam allowed')
        seams.append(dict(y=y, x0=max(a['bounds'][0],b['bounds'][0]),
                          x1=min(a['bounds'][1],b['bounds'][1]),
                          label=a['name']+' / '+b['name']))
    return dict(schema=1, mode='cr-envelope', regions=regions, edges=merge_segments(segments),
                seams=seams, total_cr=len(metadata['clock_regions']),
                fabric_cr=len(regions), slr_count=len(slrs),
                semantics='CR bounding envelopes containing SLICE; not exact resource membership or a PCB outline')


def paint_overview(draw, metadata, X, Y, font, large_font, unit=1):
    """Pillow painter, same layers/colors as the interactive canvas."""
    view = metadata['boundary_view']
    x0,x1,y0,y1 = metadata['bounds']
    draw.rectangle((X(x0),Y(y1),X(x1),Y(y0)),fill='#151e2b',outline='#69809a',width=unit)
    for r in view['regions']:
        l,h,b,t = r['bounds']
        draw.rectangle((X(l),Y(t),X(h),Y(b)),fill='#223449')
    paint_resources(draw,metadata,X,Y,font,unit)
    for x,y,xx,yy in view['edges']:
        draw.line((X(x),Y(y),X(xx),Y(yy)),fill='#435d78',width=unit)
    for seam in view['seams']:
        draw.line((X(seam['x0']),Y(seam['y']),X(seam['x1']),Y(seam['y'])),fill='#f4be62',width=2*unit)
    for r in metadata['slr_regions']:
        l,h,b,t=r['bounds']
        px,py=X(h)-118*unit,Y((b+t)/2)-18*unit
        draw.rectangle((px-6*unit,py-4*unit,px+110*unit,py+34*unit),fill='#151e2b')
        draw.text((px,py),r['name'],font=large_font,fill='#f4be62')
    for r in view['regions']:
        l,h,b,t=r['bounds']
        if X(h)-X(l)>54*unit and Y(b)-Y(t)>22*unit:
            draw.text((X(l)+4*unit,Y(t)+4*unit),r['name'],font=font,fill='#9bb0c5')
