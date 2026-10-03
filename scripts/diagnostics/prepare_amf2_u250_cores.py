#!/usr/bin/env python3
"""Prepare fresh U250 OOC synthesis jobs from the public AMF2 project bundle.

Original releases stay read-only. Core scope is explicitly different from the
paper's complete board designs. Run on eda072 from the AMFplacer3.0 repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def tq(value):
    value = str(value)
    if any(c in value for c in '{}\n'):
        raise ValueError(value)
    return '{' + value + '}'


def original_sources(project, case):
    old = next(p for p in project.rglob('*.tcl')
               if p.parent.name == 'synth_1' and 'synth_design -top' in p.read_text(errors='replace'))
    text = old.read_text()
    mappings = []

    def resolve(name):
        if case == 'optimsoc':
            suffix = name.split('/src/', 1)[1]
            p = next(p for p in project.rglob('src') if (p/'mor1kx_5.0').is_dir()) / suffix
        else:
            suffix = name.split('/piton/design/', 1)[1]
            p = next(project.rglob('imports/design')) / suffix
        if not p.exists():
            candidates = [p for p in project.rglob(Path(name).name) if p.is_file()]
            hashes = {sha(p) for p in candidates}
            if len(hashes) != 1:
                raise ValueError(f'Unresolved/ambiguous source {name}: {candidates}')
            p = candidates[0]
        mappings.append(dict(original=name, resolved=str(p), sha256=sha(p)))
        return p

    commands = []
    sources = []
    for match in re.finditer(r'^read_verilog([^\n{]*)\{(.*?)\}', text, re.M | re.S):
        opts, names = match.groups()
        paths = [resolve(n) for n in names.split()]
        sources.extend(paths)
        commands.append('read_verilog ' + ('-sv ' if '-sv' in opts else '') + '[list ' + ' '.join(map(tq, paths)) + ']')
    if not sources:
        raise ValueError(f'No sources found in {old}')
    for match in re.finditer(r'^set_property (is_global_include|file_type) (.*?) \[get_files ([^\]]+)\]', text, re.M):
        prop, val, name = match.groups()
        commands.append(f'set_property {prop} {val} [get_files {tq(resolve(name))}]')
    defines = re.search(r'set_property verilog_define \{([^}]+)\}', text)
    if defines:
        commands.insert(0, 'set_property verilog_define {' + defines.group(1) + '} [current_fileset]')
    # Include roots follow the original three directories, with repository paths
    # resolved rather than writing into the old author's filesystem.
    if case == 'openpiton':
        root = next(project.rglob('imports/design'))
        includes = [root/'include', root/'chipset/include', root/'chip/tile/ariane/src/common_cells/include']
    else:
        root = next(p for p in project.rglob('src') if (p/'mor1kx_5.0').is_dir())
        includes = [root/'mor1kx_5.0/rtl/verilog', root/'optimsoc_bootrom_bootrom_0/verilog',
                    root/'optimsoc_lisnoc_dma_0/rtl/dma', root/'optimsoc_lisnoc_dma_0/rtl']
    commands.insert(0, 'set_property include_dirs [list ' + ' '.join(map(tq, includes)) + '] [current_fileset]')
    return old, commands, mappings


def optim_wrapper(project):
    board = next(project.rglob('system_2x2_cccc_vcu108.sv')).read_text()
    begin = board.index('   localparam base_config_t')
    end = board.index('   nasti_channel', begin)
    config = board[begin:end]
    return '''// Derived OOC boundary: unmodified 4-tile, 4-core/tile system.
module amf2_optimsoc_core(
 input clk, rst,
 input [15:0] glip_in_data, input glip_in_valid, output glip_in_ready,
 output [15:0] glip_out_data, output glip_out_valid, input glip_out_ready,
 output [127:0] wb_ext_adr_i, wb_ext_dat_i,
 output [3:0] wb_ext_cyc_i, wb_ext_stb_i, wb_ext_we_i,
 output [15:0] wb_ext_sel_i,
 output [11:0] wb_ext_cti_i, output [7:0] wb_ext_bte_i,
 input [3:0] wb_ext_ack_o, wb_ext_rty_o, wb_ext_err_o,
 input [127:0] wb_ext_dat_o
);
 import optimsoc_config::*;
 localparam NUM_CORES=4;
 localparam ENABLE_VCHANNELS=1;
 localparam LMEM_SIZE=128*1024*1024;
''' + config + '''
 glip_channel c_glip_in(.clk(clk));
 glip_channel c_glip_out(.clk(clk));
 assign c_glip_in.data=glip_in_data;
 assign c_glip_in.valid=glip_in_valid;
 assign glip_in_ready=c_glip_in.ready;
 assign glip_out_data=c_glip_out.data;
 assign glip_out_valid=c_glip_out.valid;
 assign c_glip_out.ready=glip_out_ready;
 system_2x2_cccc_dm #(.CONFIG(CONFIG)) u_system(
 .clk(clk), .rst(rst), .c_glip_in(c_glip_in), .c_glip_out(c_glip_out),
 .wb_ext_adr_i(wb_ext_adr_i), .wb_ext_dat_i(wb_ext_dat_i),
 .wb_ext_cyc_i(wb_ext_cyc_i), .wb_ext_stb_i(wb_ext_stb_i),
 .wb_ext_we_i(wb_ext_we_i), .wb_ext_sel_i(wb_ext_sel_i),
 .wb_ext_cab_i(), .wb_ext_cti_i(wb_ext_cti_i), .wb_ext_bte_i(wb_ext_bte_i),
 .wb_ext_ack_o(wb_ext_ack_o), .wb_ext_rty_o(wb_ext_rty_o),
 .wb_ext_err_o(wb_ext_err_o), .wb_ext_dat_o(wb_ext_dat_o));
endmodule
'''


def piton_wrapper():
    ports = ['input core_ref_clk, rst_n, ndmreset_i',
             'input [`NUM_TILES-1:0] debug_req_i, timer_irq_i, ipi_i',
             'input [`NUM_TILES*2-1:0] irq_i', 'output [`NUM_TILES-1:0] unavailable_o']
    connections = ['.slew(1\'b1)', '.impsel1(1\'b1)', '.impsel2(1\'b1)',
                   '.core_ref_clk(core_ref_clk)', '.io_clk(core_ref_clk)', '.rst_n(rst_n)',
                   '.pll_rst_n(rst_n)', '.clk_en(1\'b1)', '.pll_lock()', '.pll_bypass(1\'b1)',
                   '.pll_rangea(5\'b0)', '.clk_mux_sel(2\'b0)', '.jtag_clk(1\'b0)',
                   '.jtag_rst_l(1\'b1)', '.jtag_modesel(1\'b1)', '.jtag_datain(1\'b0)',
                   '.jtag_dataout()', '.async_mux(1\'b1)', '.oram_on(1\'b0)',
                   '.oram_traffic_gen(1\'b0)', '.oram_dummy_gen(1\'b0)']
    for n in ('ndmreset_i', 'debug_req_i', 'timer_irq_i', 'ipi_i', 'irq_i', 'unavailable_o'):
        connections.append(f'.{n}({n})')
    for i in (1, 2, 3):
        for prefix, direction, back in [('processor_offchip', 'output', 'input'), ('offchip_processor', 'input', 'output')]:
            for signal, width, dr in [('valid', '', direction), ('data', '[`NOC_DATA_WIDTH-1:0] ', direction), ('yummy', '', back)]:
                name = f'{prefix}_noc{i}_{signal}'
                ports.append(f'{dr} {width}{name}')
                connections.append(f'.{name}({name})')
    return ('// Core-only OOC boundary; original source and compile definitions retained.\n'
            '`include "define.tmp.h"\n`include "piton_system.vh"\n'
            'module amf2_openpiton_core(\n ' + ',\n '.join(ports) + '\n);\n chip chip(\n ' +
            ',\n '.join(connections) + '\n );\nendmodule\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    bundle = root/'data/reference/amf2-cases-20260930'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(__file__, output/Path(__file__).name)
    settings = [('minimap2', 'device_chain_kernel', 'ap_clk', 8., 8.),
                ('optimsoc', 'amf2_optimsoc_core', 'clk', 20., 19.992),
                ('openpiton', 'amf2_openpiton_core', 'core_ref_clk', 10., 9.999)]
    catalog = []
    for case, top, clock, amf_ns, vivado_ns in settings:
        directory = output/case
        directory.mkdir()
        project = bundle/'projects'/case
        if case == 'minimap2':
            rtl = next(project.rglob('ipshared/adc8/hdl/verilog'))
            sources = sorted(rtl.glob('*.v'))
            old = next(p for p in project.rglob('design_1_wrapper.tcl') if p.parent.name=='synth_1')
            commands = ['read_verilog [list ' + ' '.join(map(tq, sources)) + ']']
            mappings = [dict(original=str(p), resolved=str(p), sha256=sha(p)) for p in sources]
        else:
            old, commands, mappings = original_sources(project, case)
            wrapper = directory/(top+'.sv')
            wrapper.write_text(optim_wrapper(project) if case=='optimsoc' else piton_wrapper())
            commands.append('read_verilog -sv ' + tq(wrapper))
        xdc = directory/'core_clock.xdc'
        xdc.write_text(f'create_clock -name {clock} -period {vivado_ns} [get_ports {clock}]\n'
                       '# Core register-to-register timing only. No invented board I/O delays.\n')
        tcl = directory/'synthesize.tcl'
        body = '\n'.join(commands) + '\n'
        body += f'read_xdc {tq(xdc)}\n'
        body += f'synth_design -top {top} -part xcu250-figd2104-2L-e -mode out_of_context'
        body += ' -flatten_hierarchy ' + ('full' if case=='minimap2' else 'rebuilt') + '\n'
        body += '''if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Synthesis contains black boxes"}
write_checkpoint post_synth.dcp
report_utilization -hierarchical -file post_synth_utilization.rpt
opt_design
if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Post-opt contains black boxes"}
write_checkpoint post_opt.dcp
write_edif post_opt.edf
write_xdc -type timing post_opt_timing.xdc
report_clocks -file clocks.rpt
check_timing -verbose -file check_timing.rpt
report_utilization -hierarchical -file utilization.rpt
report_timing_summary -delay_type min_max -report_unconstrained -file timing_summary.rpt
set f [open synthesis_summary.tsv w]
puts $f "part\\t[get_property PART [current_design]]"
puts $f "cells\\t[llength [get_cells -hier -filter {IS_PRIMITIVE}]]"
puts $f "black_boxes\\t[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]"
foreach clk [get_clocks] {puts $f "clock\\t$clk\\t[get_property PERIOD $clk]"}
close $f
puts "AMF2_U250_SYNTHESIS_COMPLETED"
'''
        tcl.write_text('set_param general.maxThreads 4\ncreate_project -in_memory -part xcu250-figd2104-2L-e\n'
                       'set_property XPM_LIBRARIES {XPM_CDC XPM_FIFO XPM_MEMORY} [current_project]\n'
                       'if {[catch {\n' + body + '\n} message options]} {\n'
                       'puts stderr $message\nputs stderr [dict get $options -errorinfo]\nexit 1\n}\nexit 0\n')
        meta = dict(case=case, scope='compute-core OOC; not paper full-board design', top=top,
                    original_amf_period_ns=amf_ns, vivado_core_period_ns=vivado_ns,
                    clock_provenance=str(bundle/'cases'/case/'clocks.tsv'),
                    period_provenance=str(bundle/'cases'/case/'original_config.jsonc'),
                    original_synthesis_script=str(old), original_synthesis_script_sha256=sha(old),
                    source_mappings=mappings, target_part='xcu250-figd2104-2L-e',
                    io_timing_scope='register-to-register only; external interface budgets unspecified',
                    excluded={'minimap2':['PCIe DMA shell'], 'optimsoc':['board/DDR shell', 'GLIP UART transport', 'Wishbone-to-AXI adapters'],
                              'openpiton':['chipset and board/DDR/SD/UART shell']}[case])
        (directory/'input_manifest.json').write_text(json.dumps(meta, indent=2)+'\n')
        catalog.append(dict(case=case, directory=str(directory), top=top, amf_period_ns=amf_ns, vivado_period_ns=vivado_ns))
    (output/'catalog.json').write_text(json.dumps(catalog, indent=2)+'\n')
    (output/'git_status.txt').write_text(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True))
    (output/'source_commit.txt').write_text(subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True))
    print(output)


if __name__ == '__main__':
    main()
