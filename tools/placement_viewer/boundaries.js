/* Display envelopes only; never used for AMF membership or legalization. */
function paintBoundaries(ctx, metadata, X, Y, options={}) {
  const view=metadata.boundary_view;if(!view)return;
  const rect=b=>[X(b[0]),Y(b[3]),X(b[1])-X(b[0]),Y(b[2])-Y(b[3])];
  const line=s=>{ctx.beginPath();ctx.moveTo(X(s[0]),Y(s[1]));ctx.lineTo(X(s[2]),Y(s[3]));ctx.stroke()};
  ctx.save();ctx.fillStyle='#151e2b';ctx.fillRect(...rect(metadata.bounds));
  for(const r of view.regions){ctx.fillStyle='#223449';ctx.fillRect(...rect(r.bounds));}
  paintHardResources(ctx,metadata,X,Y);
  ctx.lineWidth=1;ctx.strokeStyle='#69809a';ctx.strokeRect(...rect(metadata.bounds));
  if(options.cr){ctx.strokeStyle='#435d78';ctx.lineWidth=.75;for(const s of view.edges)line(s);}
  if(options.raw){ctx.strokeStyle='#9576bc';ctx.lineWidth=.65;ctx.globalAlpha=.7;for(const r of metadata.clock_regions)for(const s of r.outline_segments||[])line(s);ctx.globalAlpha=1;}
  ctx.font='11px system-ui';ctx.textBaseline='top';
  if(options.cr)for(const r of view.regions){const [x,y,w,h]=rect(r.bounds);if(w>45&&h>28){ctx.fillStyle='#9bb0c5';ctx.fillText(r.name,x+5,y+5);}}
  if(options.slr){
    ctx.strokeStyle='#f4be62';ctx.lineWidth=2;for(const s of view.seams)line([s.x0,s.y,s.x1,s.y]);
    ctx.font='600 13px system-ui';ctx.fillStyle='#f4be62';ctx.textBaseline='middle';
    for(const r of metadata.slr_regions){const [x,y,w,h]=rect(r.bounds);const tw=ctx.measureText(r.name).width;ctx.fillStyle='#151e2b';ctx.fillRect(x+w-tw-22,y+h/2-13,tw+14,26);ctx.fillStyle='#f4be62';ctx.fillText(r.name,x+w-tw-15,y+h/2);}
  }
  ctx.restore();
}
