#!/usr/bin/env python3
"""Compare published AMF leaf-pin drivers with the matching original DCP."""
import collections
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from inspect_amf_inputs import digest


def compare(inventory, netlist):
    cells={}
    with (inventory/'cells.tsv').open() as f:
        next(f)
        for line in f:
            ident,name,kind=line.rstrip('\n').split('\t')
            cells[ident]=(name,kind)
    kinds={name:kind for name,kind in cells.values()}
    pattern=re.compile(r'^\s*pin=> (\S+) refpin=> (\S+) dir=> (\S+) net=> (.*?) drivepin=>\s*(.*?)\s*$')
    aliases={}
    with zipfile.ZipFile(netlist) as z:
        with z.open(z.namelist()[0]) as f:
            for raw in f:
                line=raw.decode()
                if 'pin=>' not in line:continue
                match=pattern.match(line)
                if not match:raise ValueError('Unrecognized AMF pin record: '+line[:200])
                pin,_,direction,_,drivers=match.groups()
                owner=pin.rsplit('/',1)[0]
                for driver in drivers.split():
                    # The original exporter asks for leaf drivers, which can
                    # be Unisim internals of its own canonical output pin.
                    if direction in ('OUT','INOUT') and driver!=pin and driver.startswith(owner+'/'):
                        if driver in aliases and aliases[driver]!=pin:
                            raise ValueError('Ambiguous internal output alias: '+driver)
                        aliases[driver]=pin
    def endpoint(value):
        ident,pin=value.split(':',1)
        name,kind=cells[ident]
        return name+'/'+pin
    def normalize(drivers):
        drivers=[aliases.get(d,d) for d in drivers]
        return tuple(sorted({('<'+kinds[d.rsplit('/',1)[0]]+'>')
                             if kinds.get(d.rsplit('/',1)[0]) in ('GND','VCC') else d for d in drivers}))
    pins={};singleton_outputs=set()
    with (inventory/'nets.tsv').open() as f:
        next(f)
        for line in f:
            _,drivers,sinks,inouts=line.rstrip('\n').split('\t')
            groups=[[endpoint(p) for p in group.split(',') if p] for group in (drivers,sinks,inouts)]
            signature=normalize(groups[0]+groups[2])
            if len(groups[0])==1 and not groups[1] and not groups[2]:singleton_outputs.add(groups[0][0])
            for direction,group in zip(('OUT','IN','INOUT'),groups):
                for pin in group:
                    value=(direction,signature)
                    if pin in pins and pins[pin]!=value:
                        raise ValueError('DCP pin belongs to different logical nets: '+pin)
                    pins[pin]=value
    counts=collections.Counter();examples=[];expected_cells={}
    with zipfile.ZipFile(netlist) as z:
        with z.open(z.namelist()[0]) as f:
            for raw in f:
                line=raw.decode()
                if line.startswith('curCell=> '):
                    fields=line.split();expected_cells[fields[1]]=fields[3]
                elif 'pin=>' in line:
                    m=pattern.match(line)
                    if not m: raise ValueError('Unrecognized AMF pin record: '+line[:200])
                    pin,refpin,direction,net,drivers=m.groups()
                    value=(direction,normalize(drivers.split()))
                    actual=pins.pop(pin,None)
                    counts['amf_pins']+=1
                    if actual==value: counts['matched_connected_pins']+=1
                    elif actual is None and not net and not drivers: counts['unconnected_amf_pins']+=1
                    elif direction=='OUT' and not net and not drivers and pin in singleton_outputs:
                        counts['dangling_output_net_representation_only']+=1
                    else:
                        counts['mismatches']+=1
                        if len(examples)<20 or counts['mismatches']%5000==0:
                            examples.append(dict(pin=pin,expected=value,actual=actual,amf_net=net))
    constants={n for n,k in kinds.items() if k in ('GND','VCC')}
    counts['extra_constant_output_pins']=sum(pin.rsplit('/',1)[0] in constants for pin in pins)
    pins={pin:value for pin,value in pins.items() if pin.rsplit('/',1)[0] not in constants}
    counts['remaining_dcp_connected_pins']=len(pins)
    missing=set(expected_cells)-set(kinds);extra=set(kinds)-set(expected_cells)-constants
    wrong=sum(expected_cells[n]!=kinds[n] for n in set(expected_cells)&set(kinds))
    return dict(state='matched' if not (counts['mismatches'] or pins or missing or extra or wrong) else 'mismatch',
                counts=dict(counts),cells=dict(amf=len(expected_cells),dcp=len(kinds),missing=len(missing),extra=len(extra),type_mismatch=wrong),
                mismatch_examples=examples,extra_pin_examples=list(pins)[:20],
                constant_driver_normalization='GND and VCC driver instance names are normalized by primitive type',
                internal_output_aliases=len(aliases),
                scope='Canonical leaf-to-leaf pin directions and driver sets; excludes top-level port connectivity and sinkless output net representation; not a formal functional equivalence proof',
                input_netlist_sha256=digest(netlist),dcp_binding=json.loads((inventory/'binding.json').read_text()))


def main():
    batch=Path(sys.argv[1]).resolve()
    if '--canonical-only' in sys.argv[2:]:
        status=[]
        for item in json.loads((batch/'catalog.json').read_text()):
            output=batch/'reports'/(item['case']+'-connectivity')
            deadline=time.monotonic()+7200
            while not (output/'comparison.json').exists():
                if time.monotonic()>deadline:raise RuntimeError('Timed out waiting for inventory: '+item['case'])
                time.sleep(15)
            config=json.loads(Path(item['config']).read_text())
            destination=output/'comparison-canonical.json'
            if destination.exists():
                report=json.loads(destination.read_text())
            else:
                report=compare(output,Path(config['vivado extracted design information file']))
                report['comparison_script_sha256']=digest(Path(__file__))
                destination.write_text(json.dumps(report,indent=2)+'\n')
            status.append(dict(case=item['case'],state=report['state'],counts=report['counts']))
            (batch/'connectivity_canonical_status.json').write_text(json.dumps(status,indent=2)+'\n')
            print(json.dumps(status[-1]),flush=True)
        return
    status=[]
    for item in json.loads((batch/'catalog.json').read_text()):
        output=batch/'reports'/(item['case']+'-connectivity')
        cmd=['/Projects/Xilinx/Vivado/2024.2/bin/vivado','-mode','batch','-notrace','-nojournal',
             '-log',str(batch/'logs'/(item['case']+'-connectivity-vivado.log')),
             '-source',str(Path(__file__).with_suffix('.tcl').with_name('export_amf2_original_connectivity.tcl')),
             '-tclargs',item['dcp'],str(output)]
        record=dict(case=item['case'],command=cmd,state='exporting',started=time.time())
        status.append(record)
        (batch/'connectivity_status.json').write_text(json.dumps(status,indent=2)+'\n')
        with (batch/'logs'/(item['case']+'-connectivity.log')).open('w') as log:
            code=subprocess.call(cmd,cwd=batch,stdout=log,stderr=subprocess.STDOUT)
        record['exit_code']=code
        if code:
            record['state']='export_failed'
        else:
            config=json.loads(Path(item['config']).read_text())
            report=compare(output,Path(config['vivado extracted design information file']))
            (output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
            record['state']=report['state'];record['counts']=report['counts']
        record['elapsed_seconds']=time.time()-record['started']
        (batch/'connectivity_status.json').write_text(json.dumps(status,indent=2)+'\n')
        print(json.dumps(record),flush=True)


if __name__=='__main__':main()
