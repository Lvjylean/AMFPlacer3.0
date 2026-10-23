#!/usr/bin/env python3
"""Create a concise prepared-case index after the public-data audits finish."""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import zipfile
from prepare_amf2_cases import save, sha, now

def link(target, path):
    target=Path(target).resolve()
    if path.is_symlink():
        if path.resolve()!=target: raise RuntimeError('Existing link differs: '+str(path))
    elif path.exists():
        raise RuntimeError('Existing file is not a link: '+str(path))
    else:
        path.symlink_to(os.path.relpath(target,path.parent))

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--bundle',type=Path,required=True)
    p.add_argument('--audit',type=Path,required=True)
    p.add_argument('--matching',type=Path,required=True)
    args=p.parse_args()
    root,bundle,audit,matching=[x.resolve() for x in [args.root,args.bundle,args.audit,args.matching]]
    for path in [audit/'manifest.json',matching/'manifest.json',bundle/'expansion.json']:
        if json.loads(path.read_text())['state']!='completed': raise RuntimeError('Preparation still running: '+str(path))
    catalog=json.loads((bundle/'catalog.json').read_text())
    loading_path=root/'experiments/preflight/20260930-amf2-input-loading/summary.json'
    loading={entry['case']:entry for entry in json.loads(loading_path.read_text())}
    if len(loading)!=len(catalog):raise RuntimeError('AMF input loading has not finished')
    records=[]
    for case in catalog:
        directory=bundle/'cases'/case['name']
        counts=collections.Counter()
        with (directory/'benchmark_cells.tsv').open() as f:
            next(f)
            for line in f: counts[line.rstrip('\n').split('\t',1)[0]]+=1
        duplicates={n:c for n,c in counts.items() if c>1}
        record={k:case[k] for k in ['name','upstream_config','upstream_design','cell_count','amf_clock_period_ns','clock_driver_count','device_specific_primitives','project_status']}
        record.update(unique_cell_names=len(counts),duplicate_extra_records=sum(c-1 for c in duplicates.values()),
                      duplicates=duplicates,case_directory=str(directory),original_device='xcvu095-ffva2104-2-e',
                      u250_ready=False,connectivity_equivalence_checked=False,placement_executed=False)
        record['amf_input_loading']=loading[case['name']]
        inspection=Path(loading[case['name']]['inspection_directory'])
        if loading[case['name']]['exit_code']==0:
            record['amf_loaded_input']=json.loads((inspection/'inputs.json').read_text())
            if case['name']=='openpiton':
                dedup_path=directory/'deduplication.json'
                dedup=json.loads(dedup_path.read_text())
                dedup.update(amf_loader_validated=True,inspection_directory=str(inspection))
                save(dedup_path,dedup)
        pair_path=directory/'backend_pair.json'
        if pair_path.exists():
            pair=json.loads(pair_path.read_text())
            record['backend_pair']=pair
            if pair['chosen']:
                chosen=pair['chosen']
                if sha(chosen['dcp'])!=chosen['sha256']:raise RuntimeError('Chosen DCP changed')
                selected_result=Path(chosen['audit'])
                link(chosen['dcp'],directory/'reference.dcp')
                record['prepared_status']='identity_matched_original_device' if not duplicates else 'unique_identities_matched_duplicate_input_and_clock_review_required'
                if duplicates and (directory/'deduplication.json').exists():
                    record['deduplication']=json.loads((directory/'deduplication.json').read_text())
            else:
                selected_result=audit/case['name']/'result.json'
                record['prepared_status']='downloaded_constraints_available_input_reexport_required'
            result=json.loads(selected_result.read_text())
            reports=selected_result.parent/'reports'
            record['audit_result']=str(selected_result)
            record['design']=result.get('design')
            if result['state']=='completed':
                for filename in ['original_timing.xdc','clocks.tsv','clocks.rpt']:
                    link(reports/filename,directory/filename)
                clock_rows=[]
                for line in (reports/'clocks.tsv').read_text().splitlines()[1:]:
                    values=line.split('\t')
                    clock_rows.append({'name':values[0],'period_ns':float(values[1]),'waveform':values[2],'generated':values[3]=='1','source_pins':values[4]})
                record['vivado_clocks']=clock_rows
                text=(reports/'check_timing.rpt').read_text()
                record['check_timing']={key:int(value) for key,value in re.findall(r'^\d+\. checking (\w+) \((\d+)\)',text,re.M)}
        else:
            record['prepared_status']='amf_inputs_only_no_public_project_in_release'
        # Check all AMF input ZIP CRCs, including shared device and cluster inputs.
        checked=[]
        for item in case['inputs'].values():
            path=Path(item['snapshot'])
            if sha(path)!=item['sha256']:raise RuntimeError('AMF input hash changed')
            if path.suffix=='.zip':
                with zipfile.ZipFile(path) as z:
                    if z.testzip() is not None:raise RuntimeError('AMF input ZIP CRC mismatch')
                checked.append(str(path))
        record['input_zip_crcs_verified']=checked
        record['finalized_at']=now()
        save(directory/'prepared.json',record)
        readme=[f"# {case['name']}", '', f"Status: `{record['prepared_status']}`",'',
                f"AMF input records: {case['cell_count']}; unique names: {len(counts)}.",
                f"Original AMF ClockPeriod: {case['amf_clock_period_ns']:g} ns. Original Vivado constraints are kept separately.",
                'Original device: xcvu095-ffva2104-2-e. U250 conversion and placement have not been executed.',
                'See prepared.json for provenance, clock coverage, checkpoint matching and limitations.']
        (directory/'readme.md').write_text('\n'.join(readme)+'\n')
        records.append(record)
    result={'finalized_at':now(),'bundle':str(bundle),'archive_count':sum('archive' in c for c in catalog),
            'archive_bytes':sum(c.get('archive',{}).get('bytes',0) for c in catalog),
            'extraction':json.loads((bundle/'expansion.json').read_text()),'cases':records,
            'u250_conversion_executed':False,'placement_executed':False,'dcp_storage':'server-only'}
    save(bundle/'prepared_catalog.json',result)
    (bundle/'SHA256SUMS').write_text(''.join(c['archive']['sha256']+'  '+str(Path(c['archive']['path']).relative_to(bundle))+'\n' for c in catalog if 'archive' in c))
    print(json.dumps({'archive_count':result['archive_count'],'archive_bytes':result['archive_bytes'],
          'cases':[{k:c.get(k) for k in ['name','cell_count','duplicate_extra_records','prepared_status','check_timing']} for c in records]},indent=2),flush=True)

if __name__=='__main__':
    main()
