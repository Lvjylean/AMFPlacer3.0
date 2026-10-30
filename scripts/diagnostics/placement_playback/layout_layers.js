// SLL cells are an independent per-frame layer, never ordinary anonymous points.
let layoutLayer={state:'missing',crossings:[],tiles:[]}, layoutHits=[], selectedTile=null;
let layerDictionary=null, layoutMembership=null, selectedCrossings=[], visibleTiles=[];

async function fetchLayers(name){
 const r=await fetch('/api/layers?name='+encodeURIComponent(name),{cache:'no-store'});
 if(!r.ok)throw Error('本帧 SLL 数据读取失败');
 const d=await r.json();
 if(!['missing','recorded'].includes(d.state)||!Array.isArray(d.crossings)||!Array.isArray(d.tiles))throw Error('SLL 数据格式错误');
 return d;
}
function installLayers(d,dict){
 layoutLayer=d;layoutMembership=dict;selectedTile=null;$('tileInspector').textContent='点击 SLL tile 查看规划占用与 net。';
 rebuildLayerSelection();
}
let layoutFilterRegion='',layoutShowContext=false;
function beginLayoutDraw(){layoutFilterRegion=$('layoutRegion').value;layoutShowContext=$('staticContext').checked;}
function layoutVisible(id){
 const region=layoutFilterRegion;
 return (!region||layoutMembership?.regions[id]===region)&&
        (layoutShowContext||layoutMembership?.roles[id]!=='context');
}
function layoutAlpha(id){return layoutMembership?.roles[id]==='context'?.18:1;}
function layoutFit(){
 let rects=metadata.pblocks?.filter(p=>p.id===$('layoutRegion').value).flatMap(p=>p.rects)||[];
 if(!rects.length){fit();return;}
 const x0=Math.min(...rects.map(r=>r[0])),x1=Math.max(...rects.map(r=>r[1]));
 const y0=Math.min(...rects.map(r=>r[2])),y1=Math.max(...rects.map(r=>r[3]));
 scale=Math.min((canvas.width-100)/((x1-x0)*($('wide').checked?4:1)),(canvas.height-80)/(y1-y0));
 ox=(canvas.width-xscale()*(x0+x1))/2;oy=(canvas.height+scale*(y0+y1))/2;draw();
}
function initLayoutMetadata(){
 document.querySelector('h1').textContent=metadata.title||'AMF3 / 布局与 SLL';
 $('layoutRegion').replaceChildren(new Option('全部布局',''));
 for(const p of metadata.pblocks||[])$('layoutRegion').add(new Option(p.name+' · '+p.id,p.id));
 $('layoutRegion').value=metadata.active_region||'';
 $('staticContext').checked=false;
 $('staticContext').disabled=metadata.mode!=='incremental';
 $('layoutNote').textContent=(metadata.mode==='incremental'?'独立增量模块；静态上下文仅含缓存中的相关接口单元。SLL 统计仅属于本模块计划，不包含静态 shell 布线。':'包含 Pblock 的全量布局。')+' '+metadata.history_note;
 $('layoutPanel').open=true;
}
function rebuildLayerSelection(){
 const term=$('sllSearch').value.trim();
 selectedCrossings=term?layoutLayer.crossings.filter(c=>c.net.includes(term)||c.resource.includes(term)):[];
 const keys=term?new Set(selectedCrossings.map(c=>c.tile)):null;
 visibleTiles=keys?layoutLayer.tiles.filter(t=>keys.has(t.id)):layoutLayer.tiles;
 const max=layoutLayer.tiles.reduce((v,t)=>Math.max(v,t.used/t.capacity),0);
 $('sllSummary').textContent=layoutLayer.state==='missing'?'本帧未记录 SLL；不借用其他帧的计划。':
  `${metadata.mode==='incremental'?'模块规划':'规划'} crossing ${layoutLayer.crossings.length.toLocaleString()} 个 · 占用 tile 对 ${layoutLayer.tiles.length.toLocaleString()} 个 · 最高 tile 占用 ${(max*100).toFixed(1)}%`+(term?` · 匹配 ${selectedCrossings.length} 个 crossing`:'');
}
function drawLayoutLayers(){
 layoutHits=[];
 ctx.save();ctx.globalAlpha=1;
 if($('showPblocks').checked){
  (metadata.pblocks||[]).forEach((p,i)=>{
   ctx.strokeStyle=['#8cf5da','#c5a0ff','#ffd166'][i%3];ctx.lineWidth=2;
   for(const [x0,x1,y0,y1] of p.rects){
    const x=ox+x0*xscale(),y=oy-y1*scale,w=(x1-x0)*xscale(),h=(y1-y0)*scale;
    ctx.globalAlpha=.07;ctx.fillStyle=ctx.strokeStyle;ctx.fillRect(x,y,w,h);ctx.globalAlpha=1;
    ctx.setLineDash([6,3]);ctx.strokeRect(x,y,w,h);ctx.setLineDash([]);
    ctx.fillStyle=ctx.strokeStyle;ctx.font='bold 12px system-ui';ctx.fillText(p.name+' ('+p.id+')',x+4,y+15);
   }
  });
 }
 if($('showSll').checked){
  for(const t of visibleTiles){
   const u=t.used/t.capacity;ctx.fillStyle=u>=1?'#ff6680':u>.8?'#ffbc55':'#59d3e5';
   const w=Math.max(4,Math.min(12,xscale()*.45)),h=Math.max(3,Math.min(8,scale));
   for(const p of (t.points||[t.low,t.high])){
    const x=ox+p[0]*xscale(),y=oy-p[1]*scale;
    if(x<0||x>canvas.width||y<0||y>canvas.height)continue;
    ctx.globalAlpha=.85;ctx.fillRect(x-w/2,y-h/2,w,h);
    if(t.id===selectedTile){ctx.strokeStyle='#fff';ctx.strokeRect(x-w/2-2,y-h/2-2,w+4,h+4);}
    layoutHits.push({x,y,t});
   }
  }
  const links=selectedTile?layoutLayer.crossings.filter(c=>c.tile===selectedTile):selectedCrossings;
  ctx.globalAlpha=.85;
  for(const c of links.slice(0,500)){
   const x=ox+c.entry[0]*xscale(),y=oy-c.entry[1]*scale,q=ox+c.exit[0]*xscale(),v=oy-c.exit[1]*scale;
   ctx.strokeStyle='#95e9f5';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(q,v);ctx.stroke();
   ctx.fillStyle='#69efff';ctx.fillRect(x-3,y-3,6,6);
   ctx.fillStyle='#ff9bdf';ctx.beginPath();ctx.moveTo(q,v-5);ctx.lineTo(q+5,v);ctx.lineTo(q,v+5);ctx.lineTo(q-5,v);ctx.closePath();ctx.fill();
  }
 }
 ctx.restore();
}
function inspectTile(t){
 selectedTile=t.id;$('tileInspector').parentElement.open=true;
 const cs=layoutLayer.crossings.filter(c=>c.tile===t.id);
 $('tileInspector').textContent=`SLR${t.boundary} / SLR${t.boundary+1} · physical COLUMN ${t.column}\n`+
  `实际规划端点 AMF：${(t.points||[t.low,t.high]).map(p=>'('+p.join(', ')+')').join(' / ')}\n`+
  `规划占用 ${t.used}/${t.capacity} = ${(100*t.used/t.capacity).toFixed(2)}%（两端只计一次）\n`+
  cs.slice(0,12).map(c=>`${c.net}\n  ${c.from_slr} → ${c.to_slr} · ${c.resource}`).join('\n')+(cs.length>12?'\n…仅显示前 12 项':'');
 draw();
}
function initLayoutControls(){
 for(const id of ['showSll','showPblocks','staticContext'])$(id).onchange=()=>{
  if(id==='staticContext'&&$('staticContext').checked)$('layoutRegion').value='';draw();
 };
 $('layoutRegion').onchange=()=>{if($('layoutRegion').value)$('staticContext').checked=false;draw();};
 $('focusRegion').onclick=layoutFit;
 $('sllSearch').oninput=()=>{selectedTile=null;rebuildLayerSelection();draw();};
 $('layoutPanel').ontoggle=resize;for(const d of $('layoutPanel').querySelectorAll('details'))d.ontoggle=resize;
 let start;
 canvas.addEventListener('pointerdown',e=>start=[e.clientX,e.clientY]);
 canvas.addEventListener('pointerup',e=>{
  if(!start||Math.hypot(e.clientX-start[0],e.clientY-start[1])>4)return;
  const r=canvas.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;
  const hits=layoutHits.map(p=>({...p,d:Math.hypot(p.x-x,p.y-y)})).filter(p=>p.d<10).sort((a,b)=>a.d-b.d);
  if(hits.length)inspectTile(hits[0].t);
 });
}
