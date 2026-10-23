#!/usr/bin/env python3
"""Prepare a two-RP large GETRF fixture with an equivalent controller recoding."""
import argparse, hashlib, json, re, shutil
from pathlib import Path
RM='kernel_getrf_0_subUpdate_double_2_512_s'
CORE='kernel_getrf_0_getrf_core_double_2_512_256_s'
CHILD='kernel_getrf_0_subUpdate_double_2_512_Pipeline_LoopMulSub'
NEW='getrf_dfx_rm'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def ports(t):
    return [(d,(w or '').strip(),n) for d,w,n in re.findall(r'^(input|output|inout)\s*(\[[^]]+\])?\s*(\w+)\s*;',t,re.M)]
def recode(t):
    changes=[
      ("ap_ST_fsm_state1 = 2'd1","ap_ST_fsm_state1 = 1'b0"),
      ("ap_ST_fsm_state2 = 2'd2","ap_ST_fsm_state2 = 1'b1"),
      ("reg   [1:0] ap_CS_fsm;","reg ap_CS_fsm;"),
      ("reg   [1:0] ap_NS_fsm;","reg ap_NS_fsm;"),
      ("#0 ap_CS_fsm = 2'd1;","#0 ap_CS_fsm = 1'b0;"),
      ("ap_CS_fsm_state1 = ap_CS_fsm[32'd0];","ap_CS_fsm_state1 = ~ap_CS_fsm;"),
      ("ap_CS_fsm_state2 = ap_CS_fsm[32'd1];","ap_CS_fsm_state2 = ap_CS_fsm;")]
    for a,b in changes:
        assert t.count(a)==1,a
        t=t.replace(a,b)
    return t
def harness(t):
    params=dict(re.findall(r'^parameter\s+(C_\w+)\s*=\s*([^;]+);',t,re.M))
    def width(w):
        if not w:return 1
        a,b=w[1:-1].split(':')
        def val(s):
            for k,v in sorted(params.items(),key=lambda x:-len(x[0])):s=re.sub(r'\b'+k+r'\b','('+v+')',s)
            assert re.fullmatch(r'[0-9\s()+*/-]+',s),s
            return int(eval(s,{'__builtins__':{}},{}))
        return val(a)-val(b)+1
    ps=ports(t); ins=[p for p in ps if p[0]=='input' and p[2]!='ap_clk'];outs=[p for p in ps if p[0]=='output']
    ni=sum(width(w) for d,w,n in ins);no=sum(width(w) for d,w,n in outs);n=max(ni,no)
    lines=['// DFX compile-validation wrapper, not a DDR/PCIe board platform.',
      '// SHIFT loads inputs; UPDATE commits atomically; CAPTURE samples outputs.',
      'module getrf_dfx_top();',
      'wire tck,tdi,tdo,sel,shift,capture,update,jreset,clk;',
      'BSCANE2 #(.JTAG_CHAIN(1)) jtag(.TCK(tck),.TDI(tdi),.TDO(tdo),.SEL(sel),.SHIFT(shift),.CAPTURE(capture),.UPDATE(update),.RESET(jreset));',
      'BUFG clock_buffer(.I(tck),.O(clk));',
      f'(* SHREG_EXTRACT="NO" *) reg [{n-1}:0] scan = 0;',
      f'reg [{ni-1}:0] inputs_latched = 0;',
      f'wire [{no-1}:0] outputs_bus;',
      'assign tdo = scan[0];',
      'always @(posedge clk) begin',
      '  if (jreset) begin scan <= 0; inputs_latched <= 0; end',
      f'  else if (sel) begin if (capture) scan <= outputs_bus;',
      f'    else if (shift) scan <= {{tdi,scan[{n-1}:1]}};',
      f'    if (update) inputs_latched <= scan[{ni-1}:0]; end','end']
    conn=['.ap_clk(clk)'];mapping={}
    for group,bus in [(ins,'inputs_latched'),(outs,'outputs_bus')]:
        k=0
        for d,w,name in group:
            z=width(w);conn.append(f'.{name}({bus}[{k+z-1}:{k}])')
            mapping[name]=dict(direction=d,width=z,lsb=k);k+=z
    lines+=['(* KEEP_HIERARCHY="yes" *) kernel_getrf_0 kernel('+',\n'.join(conn)+');','endmodule']
    return '\n'.join(lines)+'\n',mapping
def equivalence(out,src,old,new):
    eq=out/'equivalence';eq.mkdir()
    (eq/'old.v').write_text(old.replace('module '+NEW+' ','module rm_old '))
    (eq/'new.v').write_text(new.replace('module '+NEW+' ','module rm_new '))
    cp=ports((src/(CHILD+'.v')).read_text())
    # Same arbitrary child outputs in both copies. Compare every child input.
    # The arithmetic child is byte-identical; this checks the changed controller.
    stub=['module '+CHILD+'('+','.join(n for d,w,n in cp)+');']
    for d,w,n in cp:stub.append(f'{d} {w} {n};')
    for d,w,n in cp:
        if d=='output':stub.append(f'assign {n} = tb.child_{n};')
    (eq/'child_stub.v').write_text('\n'.join(stub+['endmodule'])+'\n')
    p=ports(old);tb=[chr(96)+'timescale 1ns/1ps','module tb;','reg ap_clk=0;','integer s,c,r,cases=0;']
    for d,w,n in p:
        if n=='ap_clk':continue
        tb.append(f'{"reg" if d=="input" else "wire"} {w} {n if d=="input" else "old_"+n};')
        if d=='output':tb.append(f'wire {w} new_{n};')
    for d,w,n in cp:
        if d=='output':tb.append(f'reg {w} child_{n};')
    for ver in ('old','new'):
        tb.append(f'rm_{ver} u{0 if ver=="old" else 1}('+','.join(f'.{n}({n if d=="input" else ver+"_"+n})' for d,w,n in p)+');')
    checks=[];inst='grp_subUpdate_double_2_512_Pipeline_LoopMulSub_fu_44'
    for d,w,n in p:
        if d=='output':checks.append(f'if (old_{n} !== new_{n}) $fatal(1,"output mismatch {n}");')
    for d,w,n in cp:
        if d=='input':checks.append(f'if (u0.{inst}.{n} !== u1.{inst}.{n}) $fatal(1,"child input mismatch {n}");')
    tb+=['task check; begin']+checks+['end endtask','initial begin','#1;']
    for d,w,n in p:
        if d=='input' and n!='ap_clk':tb.append(f'{n}=0;')
    for d,w,n in cp:
        if d=='output':tb.append(f'child_{n}=0;')
    tb+=['for(s=0;s<2;s=s+1) for(c=0;c<32;c=c+1) for(r=0;r<2048;r=r+1) begin',
      'ap_clk=0; #1; u0.ap_CS_fsm=(s==0 ? 2\'b01:2\'b10); u1.ap_CS_fsm=s;',
      f'u0.{inst}_ap_start_reg=c[4]; u1.{inst}_ap_start_reg=c[4];',
      'u0.ncols_reg_98=9\'h15a;u1.ncols_reg_98=9\'h15a;',
      'u0.mul_ln44_reg_103=10\'h2a5;u1.mul_ln44_reg_103=10\'h2a5;',
      'ap_rst=c[0];ap_start=c[1];child_ap_done=c[2];child_ap_ready=c[3];',
      'rs=r[10:9];cs=r[8:0]; A_q1=r;rows_q0=~r;cols_q0=r*31;',
      '#1;check; ap_clk=1;#1;check;',
      'if (u0.ap_CS_fsm !== (u1.ap_CS_fsm ? 2\'b10:2\'b01)) $fatal(1,"state mapping mismatch");',
      'cases=cases+1; end',
      '$display("PASS controller equivalence %0d exhaustive transition cases",cases);$finish;end','endmodule']
    (eq/'tb.v').write_text('\n'.join(tb)+'\n')
    shutil.copy2(src/'kernel_getrf_0_mul_9ns_2ns_10_1_1.v',eq/'mul.v')
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    core=(a.source/(CORE+'.v')).read_text();old=(a.source/(RM+'.v')).read_text()
    insts=re.findall(r'\b'+RM+r'\s+(\w+)\s*\(',core);assert len(insts)==256,len(insts)
    selected=[insts[i] for i in (0,128)]
    for n in selected:
        core,count=re.subn(r'\b'+RM+r'\s+'+re.escape(n)+r'\s*\(','(* bblock="true", KEEP_HIERARCHY="yes", DONT_TOUCH="yes" *) '+NEW+' '+n+'(',core)
        assert count==1
    old=old.replace('module '+RM+' ','module '+NEW+' ')
    new=recode(old);assert ports(old)==ports(new)
    top,mapping=harness((a.source/'kernel_getrf_0.v').read_text())
    for v,rm in [('v0',old),('v1',new)]:
        d=a.output/v;d.mkdir()
        for f in sorted(a.source.iterdir()):
            if f.suffix in ('.v','.tcl'):shutil.copy2(f,d/f.name)
        (d/(CORE+'.v')).write_text(core);(d/(NEW+'.v')).write_text(rm)
        (d/'getrf_dfx_top.v').write_text(top)
        (d/'clock.xdc').write_text('create_clock -name dfx_clk -period 10.000 [get_pins jtag/TCK]\n')
    diffs=[f.name for f in (a.output/'v0').iterdir() if sha(f)!=sha(a.output/'v1'/f.name)]
    assert diffs==[NEW+'.v'],diffs
    equivalence(a.output,a.source,old,new)
    record=dict(schema='getrf-dfx-fixture-v1',nrc=512,ncu=256,precision='double',hls_schedule_ns=5,implementation_ns=10,
      selected_indices=[0,128],selected_instances=selected,
      rp_paths=['kernel/grp_getrf_core_double_2_512_256_s_fu_1411/'+n for n in selected],
      change='two-state outer control FSM one-hot to binary; arithmetic child byte-identical',
      equivalence_domain='reachable states from initialization/reset; child unchanged; interface and cycle behavior identical',
      changed_files_v0_v1=diffs,io_harness='JTAG controllable/observable static scan wrapper; not DDR/PCIe board validation',
      port_map=mapping,inputs={f.name:sha(f) for f in sorted(a.source.iterdir()) if f.is_file()},
      outputs={str(f.relative_to(a.output)):sha(f) for f in sorted(a.output.rglob('*')) if f.is_file()})
    (a.output/'fixture.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:record[k] for k in ('rp_paths','changed_files_v0_v1')},indent=2))
if __name__=='__main__':main()

