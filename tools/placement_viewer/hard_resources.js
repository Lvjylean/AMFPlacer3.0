/* Site anchors come from Vivado. Label rectangles are annotations, not IP footprints. */
function paintHardResources(ctx,metadata,X,Y){
  const data=metadata.hard_resources;if(!data)return;
  ctx.save();
  for(const region of data.regions){
    for(const [l,h,b,t] of region.bands){ctx.fillStyle='#202735';ctx.fillRect(X(l),Y(t),X(h)-X(l),Y(b)-Y(t));}
    const [l,h,b,t]=region.label_bounds,left=X(l),right=X(h),top=Y(t),bottom=Y(b),width=right-left;
    for(const r of region.resources){
      ctx.fillStyle=r.color;
      for(const a of r.anchors){const [x,y]=a.position;ctx.beginPath();ctx.arc(X(x),Y(y),2.2,0,2*Math.PI);ctx.fill();}
    }
    if(width<36)continue;
    for(let i=0;i<region.resources.length;i++){
      const r=region.resources[i],cy=top+(bottom-top)*(i+.5)/region.resources.length;
      const size=Math.min(18,Math.max(10,width*.20));
      ctx.font=`600 ${size}px system-ui`;ctx.textBaseline='middle';ctx.textAlign='center';
      const tw=ctx.measureText(r.kind).width,cx=(left+right)/2;
      ctx.fillStyle='#18222e';ctx.strokeStyle=r.color;ctx.lineWidth=.8;
      ctx.fillRect(cx-tw/2-5,cy-size/2-4,tw+10,size+8);ctx.strokeRect(cx-tw/2-5,cy-size/2-4,tw+10,size+8);
      ctx.fillStyle=r.color;ctx.fillText(r.kind,cx,cy);
    }
  }
  ctx.restore();
}

function initHardResourceUI(metadata){
  const data=metadata.hard_resources,legend=document.getElementById('hardwareLegend'),details=document.getElementById('hardwareDetails');
  if(!data){legend.hidden=true;details.hidden=true;return;}
  legend.hidden=false;details.hidden=false;
  for(const [name,color] of Object.entries(data.palette)){
    const span=document.createElement('span'),swatch=document.createElement('i');swatch.style.background=color;span.append(swatch,document.createTextNode(name));legend.append(span);
  }
  const note=document.createElement('span');note.className='hardwareNote';note.textContent='圆点：真实站点 · 色签：类别';legend.append(note);
  const button=document.createElement('button');button.textContent='资源明细';button.type='button';button.onclick=()=>{document.getElementById('settings').open=true;details.open=true;requestAnimationFrame(()=>details.scrollIntoView({block:'nearest'}));};legend.append(button);
  const text=document.createElement('p');text.textContent='以下是左侧两处区域的器件资源，不表示本设计已使用。位置按所属 tile 标注；同一 tile 的多个站点合并显示。色签和底色不是硬核的精确面积，未推断 NoC 连线。';details.append(text);
  for(const region of data.regions){
    const section=document.createElement('section'),title=document.createElement('h3');title.textContent=`${region.title} · SLR${region.slr} · SLICE = 0`;section.append(title);
    const cr=document.createElement('p');cr.textContent=region.clock_regions.join(' / ');section.append(cr);
    for(const r of region.resources){
      const row=document.createElement('details'),summary=document.createElement('summary');summary.style.color=r.color;
      const names=Object.entries(r.site_types).map(([k,v])=>`${k} × ${v}`).join('，');summary.textContent=`${r.kind} — ${names}`;row.append(summary);
      const list=document.createElement('table');
      for(const s of r.sites){const tr=document.createElement('tr');for(const value of [s.site,s.tile,`(${s.position.join(', ')})`]){const td=document.createElement('td');td.textContent=value;tr.append(td);}list.append(tr);}
      row.append(list);section.append(row);
    }
    details.append(section);
  }
  const source=document.createElement('p');source.textContent=`来源：Vivado ${data.source.vivado} · ${data.part} · 共 ${data.regions.reduce((n,a)=>n+a.resources.reduce((m,r)=>m+r.sites.length,0),0)} 个非 SLICE 站点。PS9 是数据库站点名，数量不代表可用 CPU 系统数。`;details.append(source);
}
