#!/usr/bin/env python3
"""Prepare the release MemN2N core at its original dimensions and ROM contents."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

from prepare_amf2_u250_cores import tq, sha

root=Path(__file__).resolve().parents[2]
base=Path(sys.argv[1]).resolve()
bundle=root/'data/reference/amf2-cases-20260930'
project=bundle/'projects/memn2n'
rtl=next(project.rglob('sources_1/new'))
mem=rtl.parent/'mem'
original=next(p for p in project.rglob('*.tcl') if p.parent.name=='synth_1' and 'synth_design -top' in p.read_text(errors='replace'))
directory=base/'memn2n'
directory.mkdir(exist_ok=False)
wrapper=directory/'amf2_memn2n_core.v'
wrapper.write_text('''// The original xillydemo core instance, with its same constant bindings.
`include "common.h"
module amf2_memn2n_core(
 input clk, rst_sys, en_init_sys,
 input empty_fifo_in, valid_fifo_in, valid_fifo_out,
 input [`BW_DATA_IN-1:0] dout_fifo_in,
 output rd_en_fifo_in, wr_en_fifo_out,
 output [`BW_DATA_IN-1:0] din_fifo_out,
 output rb_n_debug
);
 memory_network_top memory_network_top_module(
 .clk(clk), .rst_sys(rst_sys), .en_init_sys(en_init_sys),
 .empty_fifo_in(empty_fifo_in), .valid_fifo_in(valid_fifo_in),
 .valid_fifo_out(valid_fifo_out), .dout_fifo_in(dout_fifo_in),
 .done_init_memory_network(1'b1),
 .rst_fifo_in(), .rst_fifo_out(), .dout_debug(),
 .rd_en_fifo_in(rd_en_fifo_in), .wr_en_fifo_out(wr_en_fifo_out),
 .din_fifo_out(din_fifo_out), .rb_n_debug(rb_n_debug));
endmodule
''')
sources=sorted(rtl.glob('*.v'))
headers=sorted(rtl.glob('*.h'))
memory_files=sorted(mem.glob('*.mem'))
assert len(memory_files)>100
for p in memory_files:
    shutil.copy2(p,directory/p.name)
xdc=directory/'core_clock.xdc'
xdc.write_text('create_clock -name clk -period 9.999 [get_ports clk]\n# Core internal timing; no invented board I/O delays.\n')
commands=[f'set_property include_dirs [list {tq(rtl)}] [current_fileset]',
          'read_verilog [list '+' '.join(map(tq,sources+[wrapper]))+']',
          'read_mem [list '+' '.join(tq(directory/p.name) for p in memory_files)+']',
          'read_xdc '+tq(xdc),
          'synth_design -top amf2_memn2n_core -part xcu250-figd2104-2L-e -mode out_of_context -flatten_hierarchy rebuilt']
template=(base/'minimap2/synthesize.tcl').read_text()
tail=template[template.index('if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]}'):]
(directory/'synthesize.tcl').write_text('set_param general.maxThreads 4\ncreate_project -in_memory -part xcu250-figd2104-2L-e\n'
    'set_property XPM_LIBRARIES {XPM_CDC XPM_FIFO XPM_MEMORY} [current_project]\nif {[catch {\n'+'\n'.join(commands)+'\n'+tail)
meta=dict(case='memn2n',scope='compute-core OOC; not paper full-board design',top='amf2_memn2n_core',
          original_amf_period_ns=10.,vivado_core_period_ns=9.999,
          clock_provenance=str(bundle/'cases/memn2n/clocks.tsv'),
          period_provenance=str(bundle/'cases/memn2n/original_config.jsonc'),
          original_synthesis_script=str(original),original_synthesis_script_sha256=sha(original),
          source_mappings=[dict(original=str(p),resolved=str(p),sha256=sha(p)) for p in sources+headers+memory_files],
          generated_wrapper_sha256=sha(wrapper),target_part='xcu250-figd2104-2L-e',
          retained_configuration='Unmodified define.h/common.h: INFER_ONLY_MODE, BW_DATA=32, BW_DIM_IN=256, BW_DIM_EMB=5, BW_MEM_ADDR=6',
          excluded=['PCIe/Xillybus transport','host FIFOs','board clock generator'],
          io_timing_scope='register-to-register only; external interface budgets unspecified')
(directory/'input_manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
items=[i for i in json.loads((base/'catalog-v3.json').read_text()) if i['case']!='openpiton']
for i in items:
    if i['case']=='minimap2':
        i.update(preparation_tag='amf-preparation-v2',exporter='export_amf2_core_inventory_v2.tcl')
items.append(dict(case='memn2n',directory=str(directory),top='amf2_memn2n_core',amf_period_ns=10.,vivado_period_ns=9.999,
                  exporter='export_amf2_core_inventory_v2.tcl'))
(base/'catalog-v4.json').write_text(json.dumps(items,indent=2)+'\n')
print(directory)
