#!/usr/bin/env python3
"""Small synthetic-netlist checks only: no GETRF placement or Vivado jobs."""
import argparse,csv,hashlib,json,os,re,subprocess,sys,time,zipfile
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

def cell(name,kind,pins=()):
    return 'curCell=> %s type=> %s\n'%(name,kind)+''.join(
        'pin=> %s/%s refpin=> %s dir=> %s net=> %s drivepin=> %s\n'%(name,pin,pin,direction,net,driver)
        for pin,direction,net,driver in pins)

def main(root,build,out):
    out.mkdir(parents=True,exist_ok=False)
    records=[];manifest=dict(state="running",synthetic_only=True,records=records)
    def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    def run(name,cmd,expect=0):
        start=time.monotonic()
        with (out/(name+'.log')).open('w') as f:
            r=subprocess.run([str(x) for x in cmd],cwd=root,stdout=f,stderr=subprocess.STDOUT,
                             env=dict(os.environ,OMP_NUM_THREADS='1'),timeout=240)
        records.append(dict(name=name,exit_code=r.returncode,elapsed_seconds=time.monotonic()-start));save()
        assert (r.returncode==0)==(expect==0),name+': '+(out/(name+'.log')).read_text()[-2500:]
    try:
        cfg=json.loads((root/'configs/experiments/getrf-external-floorplan-paper-boundaries-10ns.json').read_text())
        for k in list(cfg):
            if k.startswith(('external floorplan','clock resource')) or k=='clock file':cfg.pop(k)
        for k in ['cellType2fixedAmo file','cellType2sharedCellType file','sharedCellType2BELtype file','mergedSharedCellType2sharedCellType']:
            cfg[k]=str(root/'configs/devices/u250'/Path(cfg[k]).name)
        cfg.update(jobs='1',MKL='false',BoundaryPaperPathLengthThreshold='1',BoundaryWeakAttraction='true',
                   ProPrimitiveTiming='false',BoundaryAnchorStopOnRegionEntry='false')
        text=cell('source','FDRE',[('Q','OUT','n0','source/Q')])
        for i in range(64):
            text+=cell(f'v{i}','LUT1',[('I0','IN',f'n{i}','source/Q' if i==0 else f'v{i-1}/O'),
                                      ('O','OUT',f'n{i+1}',f'v{i}/O')])
        text+=cell('sink','FDRE',[('D','IN','n64','v63/O')])
        with zipfile.ZipFile(out/'netlist.zip','x',zipfile.ZIP_DEFLATED) as z:z.writestr('allCellPinNet',text)
        cases=[('default',{},'stage-mode'),('independent',{'BoundaryStageMode':'independent-max2'},'stage-mode'),
               ('merged',{'BoundaryStageMode':'merged'},'stage-mode'),
               ('strategy-omitted',{},'stage-mode'),
               ('strategy-alias',{'BoundaryClusteringStrategy':'paper-hierarchical'},'stage-mode'),
               ('merged-omitted',{'BoundaryStageMode':'merged'},'stage-mode'),
               ('merged-alias',{'BoundaryStageMode':'merged','BoundaryClusteringStrategy':'paper-hierarchical'},'stage-mode'),
               ('merged-entry',{'BoundaryStageMode':'merged','BoundaryAnchorStopOnRegionEntry':'true'},'stage-mode'),
               ('merged-strong',{'BoundaryStageMode':'merged','BoundaryWeakAttraction':'false'},'stage-mode'),
               ('off-default',{'BoundaryAwareClustering':'false'},'stage-inactive'),
               ('off-explicit',{'BoundaryAwareClustering':'false'},'stage-inactive'),
               ('off-alias',{'BoundaryAwareClustering':'false','BoundaryClusteringStrategy':'paper-hierarchical'},'stage-inactive'),
               ('off-merged',{'BoundaryAwareClustering':'false','BoundaryStageMode':'merged'},'stage-inactive')]
        for name,extra,scenario in cases:
            d=out/name;d.mkdir();c=dict(cfg,**extra)
            if name in ('strategy-omitted','merged-omitted','off-default','off-merged'):c.pop('BoundaryClusteringStrategy')
            c.update({'vivado extracted design information file':str(out/'netlist.zip'),
                      'dumpDirectory':str(d),'BoundaryReportDirectory':str(d)})
            (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
            run(name,[build/'checkPaperBoundaryClustering',d/'config.json',scenario,d/'result.tsv'])
            log=(out/(name+'.log')).read_text()
            phases=re.findall(r'Global placement phase: name=(\w+) budget=(\d+)',log)
            slr=[int(n) for ph,n in phases if ph=='slr' and int(n)>0]
            if name in ('default','independent','strategy-omitted','strategy-alias'):
                assert slr==[2,2,2],phases
                assert [(ph,int(n)) for ph,n in phases if ph=='hpio' and int(n)>0]==[('hpio',3),('hpio',1)]*3
            elif name.startswith('merged'):
                assert not slr,phases
                assert [(ph,int(n)) for ph,n in phases if ph=='merged' and int(n)>0]==[('merged',3),('merged',1)]*3
            else:assert all(ph=='ordinary' for ph,n in phases),phases
            if scenario=='stage-mode':
                rows=list(csv.DictReader((d/'qp_region_targets.tsv').open(),delimiter='\t'))
                if name in ('default','independent','strategy-omitted','strategy-alias'):
                    assert not any(int(x['x_attractions']) and int(x['y_attractions']) for x in rows)
                    assert all(int(x['y_attractions'])==0 for x in rows if x['stage']=='hpio')
                elif name!='merged-entry':
                    assert any(int(x['x_attractions']) and int(x['y_attractions']) for x in rows if x['stage']=='merged')
                else:assert any(int(x['entry_omitted'])>0 for x in rows), 'entry-stop policy lost'
        assert (out/'default/result.tsv').read_bytes()==(out/'independent/result.tsv').read_bytes()
        assert (out/'default/qp_region_targets.tsv').read_bytes()==(out/'independent/qp_region_targets.tsv').read_bytes()
        assert (out/'off-default/result.tsv').read_bytes()==(out/'off-merged/result.tsv').read_bytes()
        for name,reference in [('strategy-omitted','default'),('strategy-alias','default'),('merged-omitted','merged'),('merged-alias','merged')]:
            for result in ['result.tsv','qp_region_targets.tsv']:
                assert (out/name/result).read_bytes()==(out/reference/result).read_bytes(),(name,result)
        for name in ['off-explicit','off-alias']:
            assert (out/name/'result.tsv').read_bytes()==(out/'off-default/result.tsv').read_bytes()
        for mode in ('independent-max2','merged'):
            for clip in ('true','false'):
                d=out/('hard-'+mode+'-'+clip);d.mkdir()
                c=dict(cfg,BoundaryStageMode=mode,BoundaryRegionClipping=clip)
                c.update({'vivado extracted design information file':str(out/'netlist.zip'),'dumpDirectory':str(d)})
                (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
                run(d.name,[build/'checkPaperHardRegionInterop',d/'config.json'])
        c=dict(json.loads((out/'default/config.json').read_text()),BoundaryStageMode='typo')
        (out/'invalid.json').write_text(json.dumps(c))
        run('invalid-mode',[build/'checkPaperBoundaryClustering',out/'invalid.json','stage-mode',out/'invalid.tsv'],1)
        assert 'BoundaryStageMode must be' in (out/'invalid-mode.log').read_text()
        c=dict(json.loads((out/'default/config.json').read_text()),BoundaryClusteringStrategy='unknown-strategy')
        (out/'invalid-strategy.json').write_text(json.dumps(c))
        run('invalid-strategy',[build/'checkPaperBoundaryClustering',out/'invalid-strategy.json','stage-mode',out/'invalid-strategy.tsv'],1)
        assert 'Unknown BoundaryClusteringStrategy' in (out/'invalid-strategy.log').read_text()
        run('hierarchical-policy',[build/'checkHierarchicalBoundaryPolicy'])
        run('clock-guidance-batch',['python3',root/'scripts/test_clock_guidance_batch.py'])
        run('clock-guidance-opt-in',['tclsh',root/'scripts/tests/clock_guidance_test.tcl'])
        sys.path.insert(0,str(root/'scripts'))
        from paper_boundary_config import validate_paper_boundary_binary
        cap=json.loads(subprocess.check_output([str(build/'AMFPlacer'),'--capabilities'],text=True))
        assert cap['boundary_clustering_strategy_schema']==1
        assert cap['boundary_clustering_strategy_default']=='slr-hierarchical'
        assert cap['paper_boundary_stage_mode_schema']==1
        assert cap['native_local_pin_access_schema']==2 and cap['native_pin_cache_packing_schema']==1
        validate_paper_boundary_binary(cfg,build/'AMFPlacer')
        validate_paper_boundary_binary(dict(cfg,BoundaryStageMode='merged'),build/'AMFPlacer')
        old=dict(cap);old.pop('paper_boundary_stage_mode_schema')
        with patch('paper_boundary_config.subprocess.run',return_value=SimpleNamespace(returncode=0,stdout=json.dumps(old))):
            validate_paper_boundary_binary(cfg,'unused-old-binary')
            try:validate_paper_boundary_binary(dict(cfg,BoundaryStageMode='merged'),'unused-old-binary')
            except ValueError as e:assert 'BoundaryStageMode' in str(e)
            else:raise AssertionError('Unsupported binary silently accepted selector')
        manifest.update(state='passed',default_matches_explicit=True,disabled_selector_inert=True,
                        old_binary_guard=True,capabilities=cap,full_experiments_started=False)
    except Exception as e:
        manifest.update(state='failed',error=str(e));raise
    finally:save()
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':
    a=argparse.ArgumentParser()
    for k in ('root','build','output'):a.add_argument('--'+k,type=Path,required=True)
    x=a.parse_args();main(x.root.resolve(),x.build.resolve(),x.output.resolve())

