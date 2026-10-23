#!/usr/bin/env python3
"""Check the completed emitted AMF graph without re-querying the full Vivado design."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re


def rows(path):
    with path.open() as f:return list(csv.DictReader(f, delimiter='\t'))


def validate(root):
    scope=dict(line.split('\t',1) for line in (root/'input_scope.tsv').read_text().splitlines())
    assert scope['schema']=='rm-boundary-cone-v1'
    inventory=rows(root/'cells.tsv');fixed=rows(root/'static_placement.tsv')
    active={x['cell'] for x in inventory};static={x['cell'] for x in fixed}
    assert len(active)==len(inventory) and len(static)==len(fixed) and not active&static
    assert all(x['active']=='1' for x in inventory)
    cell_types={};pins={};drivers=[];pin_driver={};alias_count=0
    for line in (root/'netlist.txt').read_text().splitlines():
        if line.startswith('curCell=> '):
            name,t=line[10:].split(' type=> ')
            assert name not in cell_types,name
            cell_types[name]=t
        else:
            m=re.fullmatch(r' pin=> (\S+) refpin=> (\S+) dir=> (IN|OUT) net=>\s*(.*?)\s*drivepin=>(?: (.*))?',line)
            assert m,line
            pin,ref,direction,net,driver=m.groups();driver=driver or ''
            assert pin not in pins,pin
            pins[pin]=(direction,net)
            pin_driver[pin]=driver
            if driver:drivers.append((pin,driver,net))
    assert set(cell_types)==active|static
    clocks=set((root/'clocks').read_text().splitlines())
    for pin,driver,net in drivers:
        if driver in ('<const0>','<const1>') or driver in clocks:continue
        assert driver in pins,(pin,driver)
        # DesignInfo.cc identifies a net by drivepinName, explicitly ignoring
        # hierarchical Vivado net aliases. Check that canonical driver instead.
        assert pins[driver][0]=='OUT' and pin_driver[driver]==driver,(pin,driver)
        if pins[driver][1]!=net:alias_count+=1
    srl=rows(root/'srl_controls.tsv');srl_names={x['cell'] for x in srl}
    expected_srl={c for c in active if cell_types[c] in ('SRL16E','SRLC32E')}
    assert srl_names==expected_srl and len(srl_names)==len(srl)
    assert int(scope['active_cells'])==len(active)
    assert int(scope['fixed_interface_cells'])==len(static)
    assert int(scope['retained_pins'])==len(pins)
    assert int(scope['active_srl_cells'])==len(srl)
    allowed=set((root/'allowed_sites.txt').read_text().splitlines())
    assert not allowed&{x['site'] for x in fixed},'static context occupies allowed Pblock site'
    files=['netlist.txt','cells.tsv','static_placement.tsv','srl_controls.tsv','clocks','dsp_registered_outputs.txt','interface_context.tsv']
    return dict(state='passed',active_cells=len(active),fixed_interface_cells=len(static),
                total_cells=len(cell_types),pins=len(pins),active_srl_cells=len(srl),
                external_clock_identifiers=len(clocks),
                hierarchical_alias_pin_references=alias_count,
                checks=['active coverage','fixed context coverage','data-driver closure','clock identifiers','active-only SRL metadata','no allowed-site static collision'],
                files={n:dict(sha256=hashlib.sha256((root/n).read_bytes()).hexdigest(),bytes=(root/n).stat().st_size) for n in files})


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('run',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=validate(args.run)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
