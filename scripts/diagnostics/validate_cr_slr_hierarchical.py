#!/usr/bin/env python3
"""CR-edge strategy checks on a small netlist; no full implementation runs."""
import argparse,csv,json,os,subprocess,sys,time
from pathlib import Path
from validate_boundary_stage_modes import main as legacy_validation

def main(root,build,out):
    out.mkdir(parents=True,exist_ok=False)
    records=[]
    manifest={'state':'running','synthetic_only':True,'records':records}
    def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    def run(name,cmd):
        start=time.monotonic()
        with (out/(name+'.log')).open('w') as f:
            result=subprocess.run(list(map(str,cmd)),cwd=root,stdout=f,stderr=subprocess.STDOUT,
                                  env=dict(os.environ,OMP_NUM_THREADS='1',PYTHONDONTWRITEBYTECODE='1'),timeout=240)
        records.append({'name':name,'exit_code':result.returncode,'elapsed_seconds':time.monotonic()-start});save()
        if result.returncode: raise AssertionError(name+': '+(out/(name+'.log')).read_text()[-3000:])
    try:
        legacy_validation(root,build,out/'legacy')
        cfg=json.loads((out/'legacy/default/config.json').read_text())
        cfg['BoundaryClusteringStrategy']='cr-slr-hierarchical'
        cases=['lower','upper','both-seams','inside','inside-above','right','side-tie','bottom','top','middle-upper','macro','negative-offset']
        for mode in ['independent-max2','merged']:
            for case in cases:
                d=out/(mode+'-'+case);d.mkdir()
                c=dict(cfg,BoundaryStageMode=mode,dumpDirectory=str(d),BoundaryReportDirectory=str(d))
                (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
                run(d.name,[build/'checkPaperBoundaryClustering',d/'config.json','cr-'+case,d/'result.tsv'])
                rows=list(csv.DictReader((d/'qp_region_targets.tsv').open(),delimiter='\t'))
                assert len(rows)==4,(d.name,len(rows))
                xy=[(int(r['x_attractions']),int(r['y_attractions'])) for r in rows]
                assert xy[0][0]==0 and xy[0][1]>0,(d.name,xy)
                assert xy[1]==(0,0) and xy[3]==(0,0),(d.name,xy)
                assert (xy[2][0]>0)==(case!='side-tie'),(d.name,xy)
                assert (xy[2][1]>0)==(mode=='merged'),(d.name,xy)
                anchors=list(csv.DictReader((d/'cr_cluster_anchors.tsv').open(),delimiter='\t'))
                assert anchors and all(int(a['cr_row'])>=0 for a in anchors)
            # Real GP phase scheduling, projection and fixed/locked lifecycle.
            for clip in ['true','false']:
                d=out/(mode+'-hard-'+clip);d.mkdir()
                c=dict(cfg,BoundaryStageMode=mode,BoundaryRegionClipping=clip,dumpDirectory=str(d),BoundaryReportDirectory=str(d))
                (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
                run(d.name,[build/'checkPaperHardRegionInterop',d/'config.json'])
            for entry in ['false','true']:
                d=out/(mode+'-stage-entry-'+entry);d.mkdir()
                c=dict(cfg,BoundaryStageMode=mode,BoundaryAnchorStopOnRegionEntry=entry,
                       dumpDirectory=str(d),BoundaryReportDirectory=str(d))
                (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
                run(d.name,[build/'checkPaperBoundaryClustering',d/'config.json','stage-mode',d/'result.tsv'])
                rows=list(csv.DictReader((d/'qp_region_targets.tsv').open(),delimiter='\t'))
                if entry=='true': assert any(int(r['entry_omitted']) for r in rows)
                if mode=='independent-max2': assert all(int(r['y_attractions'])==0 for r in rows if r['stage']=='hpio')
            d=out/(mode+'-off');d.mkdir()
            c=dict(cfg,BoundaryStageMode=mode,BoundaryAwareClustering='false',
                   dumpDirectory=str(d),BoundaryReportDirectory=str(d))
            (d/'config.json').write_text(json.dumps(c,indent=2)+'\n')
            run(d.name,[build/'checkPaperBoundaryClustering',d/'config.json','stage-inactive',d/'result.tsv'])
            assert (d/'result.tsv').read_bytes()==(out/'legacy/off-default/result.tsv').read_bytes()
        for name in ['test_boundary_strategy_config.py','test_incremental_config.py']:
            run(name,['python3',root/'scripts'/name])
        sys.path.insert(0,str(root/'scripts'))
        from paper_boundary_config import validate_paper_boundary_binary
        validate_paper_boundary_binary(cfg,build/'AMFPlacer')
        manifest.update(state='passed',full_experiments_started=False,
                        legacy_equivalence=True,cr_modes=['independent-max2','merged'])
    except Exception as e:
        manifest.update(state='failed',error=str(e));raise
    finally: save()
    print(json.dumps({'state':manifest['state'],'new_checks':len(records),'output':str(out)}))
if __name__=='__main__':
    a=argparse.ArgumentParser()
    for k in ['root','build','output']: a.add_argument('--'+k,type=Path,required=True)
    args=a.parse_args();main(args.root.resolve(),args.build.resolve(),args.output.resolve())
