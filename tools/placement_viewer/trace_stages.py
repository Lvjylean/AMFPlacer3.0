"""Frame labels derived from preceding log events; never infer from frame index."""
import re
from pathlib import Path

def trace_stages(log, event_file=None):
    event_file=event_file or log.parent.parent/'placement/DumpAllCoordTrace-events.tsv'
    if event_file.exists():
        result={}
        for line in event_file.read_text().splitlines():
            fields=line.split('\t')
            if len(fields)!=7:continue
            seq,phase,round_no,iteration,step,elapsed,count=fields
            name='DumpAllCoordTrace-'+seq+'.gz'
            phase_label={'initial':'初始布局','slr':'SLR 优化','hpio':'HPIO 优化','packing':'Packing','detailed':'详细布局','final':'AMF 最终布局'}.get(phase,phase)
            step_label={'before_qp':'QP 开始前','after_qp':'QP 完成','begin':'本轮开始','after_timing':'时序分析后','after_qp1':'QP 第 1 次后','after_qp2':'QP 第 2 次后','after_spreading':'扩散后','after_clip':'裁侧后','after_legalization':'宏合法化后','after_pack_iteration':'打包迭代后','after_unfixed_qp':'未固定单元 QP 后','after_exception_handling':'异常处理后','amf_final':'最终位置'}.get(step,step)
            parts=[phase_label]
            if phase=='initial' and int(iteration)>=0:parts.append('QP '+str(int(iteration)+1)+'/6')
            elif int(round_no)>=0:parts.append('全局轮次 '+round_no)
            if phase!='initial' and int(iteration)>=0:parts.append('阶段迭代 '+str(int(iteration)+1))
            parts.append(step_label)
            result[name]={'phase':phase,'step':step,'round':int(round_no),'iteration':int(iteration),'elapsed_seconds':float(elapsed),'point_count':int(count),'time_origin':'first_snapshot','label':' | '.join(parts)}
        return result
    result={};phase='Initialization';step='Initial placement';iteration=None;packing_iter=None
    if not log.exists():return result
    for lineno,raw in enumerate(log.read_text(errors='replace').splitlines(),1):
        line=re.sub(r'\x1b\[[0-9;]*m','',raw)
        if 'GlobalPlacement_fixedCLB started' in line:
            phase='Initial fixed-CLB placement';step='Initial placement';iteration=None
        m=re.search(r'phase=(\w+) iter=(\d+) step=(\w+)',line)
        if m:
            phase={'slr':'SLR optimization','hpio':'HPIO optimization'}.get(m[1],m[1]);iteration=int(m[2]);step=m[3]
        if 'WLOptimizer Iteration#' in line:step='After QP'
        if 'Spreader Iteration#' in line:step='After spreading / clipping'
        m=re.search(r'ParallelCLBPacker: iter#(\d+)',line)
        if m:packing_iter=int(m[1])
        if 'dumping coordinate archieve' not in line:continue
        m=re.search(r'(DumpAllCoordTrace-(?:Packing-)?\d+\.gz)',line)
        if not m:continue
        t=re.search(r'elapsed time: ([\d.]+) s',line)
        if 'Packing-' in m[1]:p,s,it='CLB packing','Packing iteration snapshot',packing_iter
        else:p,s,it=phase,{'after_legalization':'After macro legalization','after_clip':'After clipping','after_qp2':'After QP'}.get(step,step),iteration
        result[m[1]]={'phase':p,'step':s,'iteration':it,'elapsed_seconds':float(t[1]) if t else None,'log_line':lineno,'label':p+' | '+s+(f' | iter {it}' if it is not None else '')}
    return result
