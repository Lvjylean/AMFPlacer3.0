#!/usr/bin/env python3
"""Independent raw-netlist count and reproducibility/attribution checks; no DCP IO."""
import bisect, collections, csv, gzip, hashlib, json, math, re, sys, zipfile
from pathlib import Path

ROOT=Path('/Orion/jinyang/workspace/AMFplacer3/experiments/preflight/20261013-sll-quantity-01')
NAMES=('original','disabled-final','release-control-final','quantity-100-final')

def tsv(path):
    with path.open() as f:return list(csv.DictReader(f,delimiter='\t'))

def coordinates(path):
    with gzip.open(path,'rt') as f:
        return {r['cell']:(float(r['x']),float(r['y']),int(r['pu'])) for r in csv.DictReader(f,delimiter='\t')}

def independent_count(run,coords):
    manifest=json.loads((run/'manifest.json').read_text())
    cfg=json.loads((run/'config.json').read_text())
    clocks=set(Path(manifest['inputs']['clock file']['path']).read_text().split())
    boundaries=[];low=float('inf');high=-low
    for line in Path(manifest['inputs']['physical boundary model file']['path']).read_text().splitlines():
        fields=line.split('\t')
        if fields[0]=='REGION':low=min(low,float(fields[5]));high=max(high,float(fields[6]))
        if fields[0]=='BOUNDARY' and fields[2:4]==['SLR','Y'] and fields[-1]=='1':boundaries.append(float(fields[4]))
    boundaries.sort()
    nets={};invalid=set();cell=None;unconnected=0;pcie=0;missing=set();power=set();global_nets=set()
    raw=Path(manifest['inputs']['vivado extracted design information file']['path'])
    with zipfile.ZipFile(raw) as z, z.open('allCellPinNet') as f:
        for line in f:
            fields=line.decode().split()
            if fields[0]=='curCell=>':
                cell=fields[1];pcie+=fields[3]=='PCIE_3_1'
                if cell not in coords:missing.add(cell)
                continue
            if len(fields)<10 or fields[7]=='drivepin=>':unconnected+=1;continue
            alias,driver=fields[7],fields[9]
            if alias.endswith('/<const0>'):driver='<const0>'
            if alias.endswith('/<const1>'):driver='<const1>'
            if driver in ('<const0>','<const1>'):power.add(driver);continue
            if driver in clocks:global_nets.add(driver);continue
            if cell not in coords:invalid.add(driver);continue
            y=coords[cell][1]
            if not low<=y<=high:invalid.add(driver)
            slr=bisect.bisect_left(boundaries,y)
            # Driver aliases are the canonical net identity in DesignInfo.
            r=nets.get(driver)
            if r is None:nets[driver]=[slr,slr,1,int(fields[5]=='OUT')]
            else:r[0]=min(r[0],slr);r[1]=max(r[1],slr);r[2]+=1;r[3]+=fields[5]=='OUT'
    if pcie:raise RuntimeError('Independent checker needs physical pin offsets for PCIE_3_1')
    seams=[0]*len(boundaries);demand=optimized=0;excluded=0
    network_classes=collections.Counter()
    for name,(lo,hi,pins,drivers) in nets.items():
        network_classes['driverless' if drivers==0 else 'multiple_drivers' if drivers>1 else 'single_driver']+=1
        if pins<2:network_classes['single_pin']+=1
        if name in invalid:continue
        demand+=hi-lo
        for b in range(lo,hi):seams[b]+=1
        if drivers==1 and pins>=2:optimized+=hi-lo
        else:excluded+=1
    return dict(S=demand,S_optimized=optimized,seams=seams,nets=len(nets),invalid_nets=len(invalid),
                missing_cells=len(missing),power_nets=len(power),global_marked_nets=len(global_nets),
                optimization_excluded=excluded,unconnected_pins=unconnected,pcie_cells=pcie,
                network_classes=dict(network_classes),
                convention='Original driver-pin net identity; cell positions already contain macro offsets; non-PCIE pin offsets are zero')

def main():
    result={'schema':'sll-quantity-validation-v1','cases':{},'checks':{}}
    baseline=None;baseline_inputs=None;baseline_config=None;baseline_user_config=None;algorithm_binary=None
    for name in NAMES:
        status=json.loads((ROOT/'cases'/name/'worker.json').read_text())
        if status['state']!='completed':raise RuntimeError(f'{name} not completed: {status["state"]}')
        assert status['exit_code']==0 and len(status['runs'])==1
        user_config_path=Path(status['command'][status['command'].index('--config')+1])
        assert hashlib.sha256(user_config_path.read_bytes()).hexdigest()==status['config_sha256']
        user_config={k:v for k,v in json.loads(user_config_path.read_text()).items() if not k.startswith('Sll')}
        if baseline_user_config is None:baseline_user_config=user_config
        else:assert user_config==baseline_user_config, 'Unexpected non-SLL config difference'
        if name!='original':
            if algorithm_binary is None:algorithm_binary=status['binary_sha256']
            else:assert algorithm_binary==status['binary_sha256'], 'Comparison binaries differ'
        run=Path(status['runs'][0]);manifest=json.loads((run/'manifest.json').read_text())
        report=json.loads((run/'reports/global_placement.json').read_text())
        assert report['global_placement_executed'] and not report['routing_executed']
        cfg=json.loads((run/'config.json').read_text())
        assert str(cfg['ProPrimitiveTiming']).lower()=='false' and str(cfg['ClockPeriod'])=='10'
        input_hashes={k:v['sha256'] for k,v in manifest['inputs'].items()}
        if baseline_inputs is None:baseline_inputs=input_hashes;baseline_config=cfg
        else:
            assert input_hashes==baseline_inputs
            for key in ['jobs','GlobalPlacementIteration','y2xRatio','PseudoNetWeight','SLRBoundaryDelayNs','BoundaryWeakAttraction','SRLMaxPerSLICEM','BoundaryClusteringStrategy']:
                assert cfg[key]==baseline_config[key],key
        cells=coordinates(run/'reports/global_placement.json.cells.tsv.gz')
        count=independent_count(run,cells)
        assert count['missing_cells']==0 and count['invalid_nets']==0
        row={'status':status,'global_report':report,'independent_count':count}
        if name=='original':baseline=cells
        if name=='disabled-final':
            assert set(baseline)==set(cells)
            changed=sum(baseline[k][:2]!=cells[k][:2] for k in baseline)
            max_dx=max(abs(baseline[k][0]-cells[k][0]) for k in baseline)
            max_dy=max(abs(baseline[k][1]-cells[k][1]) for k in baseline)
            pu_changed=sum(baseline[k][2]!=cells[k][2] for k in baseline)
            result['checks']['disabled_coordinate_reproduction']={'cells':len(cells),'changed_cells':changed,'pu_id_changes':pu_changed,'max_abs_dx':max_dx,'max_abs_dy':max_dy,'exact':changed==0 and pu_changed==0}
            baseline=None
        if name!='original':
            stages=tsv(ROOT/'cases'/name/'sll/stages.tsv');qps=tsv(ROOT/'cases'/name/'sll/qp.tsv')
            assert stages and int(stages[-1]['S'])==count['S'],(name,stages[-1]['S'],count['S'])
            assert int(stages[-1]['S_optimized'])==count['S_optimized']
            assert max(int(r['invalid_nets']) for r in stages)==0
            effects=collections.defaultdict(lambda:dict(delta_S=0,events=0,pu_slr_moves=0,x_projections=0,y_projections=0))
            for a,b in zip(stages,stages[1:]):
                step=b['step'] if b['step']!='begin' else 'between_iterations_or_phases'
                e=effects[step];e['delta_S']+=int(b['S'])-int(a['S']);e['events']+=1
                for key in ('pu_slr_moves','x_projections','y_projections'):e[key]+=int(b[key])
            assert sum(e['delta_S'] for e in effects.values())==int(stages[-1]['S'])-int(stages[0]['S'])
            row['trace']={'first':stages[0],'last':stages[-1],'records':len(stages),'effects':dict(effects),'mapping':tsv(ROOT/'cases'/name/'sll/mapping.tsv')}
            accepted=[r for r in qps if r['accepted']=='1']
            assert all(float(r['combined_delta'])<=1e-8 for r in accepted)
            by_sequence={int(r['seq']):r for r in stages}
            for q in qps:
                seq=int(q['seq'])
                assert int(q['S_before'])==int(by_sequence[seq]['S'])
                assert int(q['S_after'])==int(by_sequence[seq+1]['S'])
                assert math.isfinite(float(q['grad_norm']))
                assert float(q['max_y_move'])<=float(cfg['SllQuantityMaxStep'])+1e-3
            row['qp']={'calls':len(qps),'accepted':len(accepted),'rollback':len(qps)-len(accepted),
                       'backtracked':sum(int(r['backtracks'])>0 for r in qps),
                       'delta_S':sum(int(r['S_after'])-int(r['S_before']) for r in qps),
                       'smooth_delta':sum(float(r['smooth_delta']) for r in qps),
                       'writeback_count_consistent':True,
                       'largest_combined_increase':max([float(r['combined_delta']) for r in accepted]+[0])}
        log=(run/'logs/amf.log').read_text(errors='replace')
        row['phase_budgets']=re.findall(r'Global placement phase: name=(\S+) budget=(\d+) strict=(\S+)',log)
        row['phase_completions']=re.findall(r'Global placement phase completed: name=(\S+) rounds=(\d+) reason=(\S+)',log)
        result['cases'][name]=row
    result['checks']['same_inputs_and_ordinary_pro']=True
    result['checks']['same_phase_budgets']=all(r['phase_budgets']==result['cases']['original']['phase_budgets'] for r in result['cases'].values())
    assert result['checks']['same_phase_budgets']
    control=result['cases']['release-control-final']['independent_count']['S'];quantity=result['cases']['quantity-100-final']['independent_count']['S']
    result['checks']['quantity_vs_matching_control']={'control':control,'quantity':quantity,'delta':quantity-control,'relative_percent':100*(quantity-control)/control}
    result['checks']['quantity_vs_ordinary_pro']={'ordinary_pro':result['cases']['original']['independent_count']['S'],'quantity':quantity,'delta':quantity-result['cases']['original']['independent_count']['S']}
    cr=tsv(ROOT/'cases/release-control-final/sll/stages.tsv')
    qr=tsv(ROOT/'cases/quantity-100-final/sll/stages.tsv')
    common=None
    for a,b in zip(cr,qr):
        if any(a[k]!=b[k] for k in ('phase','iteration','step')):break
        if a['step']=='after_legalization':common={'seq':int(a['seq']),'phase':a['phase'],'iteration':int(a['iteration']),'control_S':int(a['S']),'quantity_S':int(b['S'])}
    result['checks']['common_gp_prefix']=common
    (ROOT/'validation/results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'checks':result['checks'],'S':{k:v['independent_count']['S'] for k,v in result['cases'].items()}},indent=2))

if __name__=='__main__':main()
