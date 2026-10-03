#!/usr/bin/env python3
"""Prepare a full U250 MiniMap2 system, preserving kernel and DMA interfaces."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tq(value):
    value = str(value)
    if any(c in value for c in '{}\n'):
        raise ValueError(value)
    return '{' + value + '}'


def parameters(path):
    values = {}
    for item in ET.parse(path).getroot().iter():
        if not item.tag.endswith('configurableElementValue'):
            continue
        key = next((v for k, v in item.attrib.items() if k.endswith('}referenceId')), '')
        if key.startswith('PARAM_VALUE.'):
            values[key[12:]] = item.text
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.directory.resolve()
    source = root/'data/reference/amf2-cases-20260930/projects/minimap2/VCU108_PCIE_minimap2/VCU108_PCIE_gene'
    bd = source/'VCU108_PCIE_gene.srcs/sources_1/bd/design_1'
    inputs = out/'full-inputs'
    inputs.mkdir(exist_ok=False)
    rtl = inputs/'rtl'
    rtl.mkdir()
    provenance = {'scope': 'complete PCIe DMA and compute system; top synthesis is not OOC',
                  'part': 'xcu250-figd2104-2L-e', 'core_period_ns': 8.0,
                  'pcie_reference_period_ns': 10.0, 'original_project': str(source),
                  'board_source': json.loads((out/'board_source.json').read_text()),
                  'sources': [], 'adaptations': [
                      'Regenerate XDMA for UltraScale+ GTY and PCIe hard block; preserve Gen3 x4, AXIS 256-bit at 125 MHz, one H2C/C2H channel, AXI Lite control.',
                      'Change reference-clock input buffer IBUFDS_GTE3 to IBUFDS_GTE4.',
                      'Remove the disabled cfg_mgmt_type1_cfg_reg_access input and three unconnected QPLL outputs absent from the UltraScale+ XDMA interface.',
                      'U250 has three user LEDs: map original user-reset, link-up and heartbeat; omit redundant external system-reset LED.',
                      'Use official U250 x4 lane, reference clock, PERST and LED pin mappings; do not reuse VCU108 pin or PCIe physical constraints.',
                      'Keep original generated block-design connections, kernel RTL and HLS wrapper unchanged; regenerate standard AXIS width converters and reset IP with original parameters.']}

    def copy(path, name=None, transform=None):
        dest = rtl/(name or path.name)
        data = path.read_bytes()
        if transform:
            data = transform(data.decode()).encode()
        dest.write_bytes(data)
        provenance['sources'].append({'source': str(path), 'source_sha256': sha(path),
                                      'derived': str(dest), 'derived_sha256': sha(dest),
                                      'modified': data != path.read_bytes()})
        return dest

    sources = []
    for p in sorted((bd/'ipshared/adc8/hdl/verilog').glob('*.v')):
        sources.append(copy(p))
    sources += [copy(bd/'synth/design_1.v'),
                copy(bd/'ip/design_1_device_chain_kernel_0_0/synth/design_1_device_chain_kernel_0_0.v'),
                copy(bd/'ip/design_1_xilinx_dma_pcie_ep_0_0_1/synth/design_1_xilinx_dma_pcie_ep_0_0.sv')]

    def clock_buffer(text):
        assert text.count('IBUFDS_GTE3') == 1
        text = text.replace('IBUFDS_GTE3', 'IBUFDS_GTE4')
        for port, expression in [('cfg_mgmt_type1_cfg_reg_access', "1'b0"),
                                 ('int_qpll1lock_out', ''), ('int_qpll1outrefclk_out', ''),
                                 ('int_qpll1outclk_out', '')]:
            pattern = r'^\s*\.' + port + r'\s*\(\s*' + re.escape(expression) + r'\s*\),[^\n]*\n'
            text, count = re.subn(pattern, '', text, flags=re.M)
            assert count == 1, port
        return text

    def leds(text):
        old = 'OBUF led_0_obuf (.O(leds[0]), .I(sys_resetn));'
        assert old in text
        return text.replace(old, "assign leds[0] = 1'b0; // Unconnected: no fourth physical user LED on U250.")

    sources += [copy(bd/'ipshared/7a2c/imports/xilinx_dma_pcie_ep.sv', transform=clock_buffer),
                copy(bd/'ipshared/7a2c/imports/xdma_app.v', transform=leds)]
    top = rtl/'amf3_minimap2_u250_full.v'
    top.write_text('''// Full PCIe system; original three status functions use U250 user LEDs.
module amf3_minimap2_u250_full(
 input sys_clk_p, sys_clk_n, sys_rst_n,
 input [3:0] pci_exp_rxp, pci_exp_rxn,
 output [3:0] pci_exp_txp, pci_exp_txn,
 output [2:0] led
);
 design_1 design_1_i (
 .sys_clk_p(sys_clk_p), .sys_clk_n(sys_clk_n), .sys_rst_n(sys_rst_n),
 .pci_exp_rxp(pci_exp_rxp), .pci_exp_rxn(pci_exp_rxn),
 .pci_exp_txp(pci_exp_txp), .pci_exp_txn(pci_exp_txn),
 .led_0(), .led_1(led[0]), .led_2(led[1]), .led_3(led[2])
 );
endmodule
''')
    sources.append(top)
    board = out/'official-board-reference/board_files/Xilinx/au250/1.3'
    pins = {p.attrib['name']: p.attrib for p in ET.parse(board/'part0_pins.xml').getroot().iter('pin')}
    pinmap = {'sys_clk_p': ('AM11', None), 'sys_clk_n': ('AM10', None),
              'sys_rst_n': (pins['pcie_perstn_rst']['loc'], 'LVCMOS12')}
    for index in range(4):
        for direction in ('rx', 'tx'):
            for polarity in ('p', 'n'):
                pinmap[f'pci_exp_{direction}{polarity}[{index}]'] = (pins[f'pcie_{direction}{index}_{polarity}']['loc'], None)
    for index in range(3):
        pinmap[f'led[{index}]'] = (pins[f'GPIO_LED_{index}_LS']['loc'], 'LVCMOS12')
    provenance['pinmap'] = pinmap
    provenance['led_drive_ma'] = 8
    xdc = inputs/'u250_board.xdc'
    text = '# Pin mapping from fixed Xilinx/open-nic-shell commit recorded in input_manifest.json.\n'
    for port, (pin, standard) in pinmap.items():
        text += f'set_property PACKAGE_PIN {pin} [get_ports {tq(port)}]\n'
        if standard:
            text += f'set_property IOSTANDARD {standard} [get_ports {tq(port)}]\n'
    text += '''set_property DRIVE 8 [get_ports {led[*]}]
create_clock -name sys_clk -period 10.000 [get_ports sys_clk_p]
set_false_path -from [get_ports sys_rst_n]
set_false_path -to [get_ports {led[*]}]
set_property PULLUP true [get_ports sys_rst_n]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]
'''
    xdc.write_text(text)
    commands = []
    old_xdma = next(bd.rglob('xdma_0.xci'))
    original = parameters(old_xdma)
    keys = ['pl_link_cap_max_link_speed', 'pl_link_cap_max_link_width', 'axi_data_width',
            'axisten_freq', 'xdma_axi_intf_mm', 'xdma_rnum_chnl', 'xdma_wnum_chnl',
            'xdma_rnum_rids', 'xdma_wnum_rids', 'xdma_num_usr_irq', 'axilite_master_en',
            'axilite_master_scale', 'axilite_master_size', 'pciebar2axibar_axil_master',
            'ref_clk_freq', 'pf0_device_id', 'pf0_vendor_id', 'pf0_subsystem_id',
            'pf0_subsystem_vendor_id', 'pf0_class_code', 'pf0_msi_cap_multimsgcap',
            'xdma_dsc_bypass', 'xdma_axilite_slave', 'xdma_sts_ports', 'cfg_mgmt_if',
            'en_axi_master_if', 'en_axi_slave_if', 'xdma_pcie_64bit_en',
            'xdma_pcie_prefetchable', 'xdma_scale', 'xdma_size']
    selected = {k: original[k] for k in keys if k in original}
    selected.update(mode_selection='Advanced', en_gt_selection='true', select_quad='GTY_Quad_227',
                    pcie_blk_locn='X0Y1', xlnx_ref_board='AU250')
    specs = [('xdma', 'xdma_0', selected)]
    for name in ['design_1_axis_dwidth_converter_0_0', 'design_1_axis_dwidth_converter_0_1', 'design_1_proc_sys_reset_0_0']:
        p = next(bd.rglob(name+'.xci'))
        values = parameters(p)
        values.pop('Component_Name', None)
        values.pop('RESET_BOARD_INTERFACE', None)
        values.pop('USE_BOARD_FLOW', None)
        specs.append(('proc_sys_reset' if 'reset' in name else 'axis_dwidth_converter', name, values))
    provenance['ip_parameters'] = {name: values for _, name, values in specs}
    provenance['original_xdma_parameters'] = original
    for kind, name, values in specs:
        commands += [f'create_ip -name {kind} -vendor xilinx.com -library ip -module_name {name}',
                     'set_property -dict [list ' + ' '.join(f'CONFIG.{k} {tq(v)}' for k, v in values.items()) + f'] [get_ips {name}]',
                     f'report_property [get_ips {name}] -file {tq(out/(name+"_properties.rpt"))}']
    commands += ['generate_target all [get_ips]',
                 'set_property generate_synth_checkpoint false [get_files -all *.xci]']
    for p in sources:
        commands.append('read_verilog ' + ('-sv ' if p.suffix == '.sv' else '') + tq(p))
    commands += [f'read_xdc {tq(xdc)}',
                 'synth_design -top amf3_minimap2_u250_full -part xcu250-figd2104-2L-e -flatten_hierarchy full',
                 'if {[llength [get_cells -hier -quiet -filter {IS_BLACKBOX}]]} {error "Black boxes in full system"}',
                 'write_checkpoint post_synth.dcp', 'report_utilization -file post_synth_utilization.rpt',
                 'opt_design', 'write_checkpoint post_opt.dcp',
                 'write_xdc post_opt_constraints.xdc', 'report_clocks -file clocks.rpt',
                 'check_timing -verbose -file check_timing.rpt',
                 'report_utilization -hierarchical -file utilization.rpt',
                 'report_io -file io.rpt', 'report_drc -file post_opt_drc.rpt',
                 'puts "MINIMAP2_U250_FULL_SYNTHESIS_COMPLETED"']
    tcl = out/'synthesize_full.tcl'
    tcl.write_text('set_param general.maxThreads 4\nif {[catch {\n'
                   + f'set_param board.repoPaths [list {tq(out/"official-board-reference/board_files")}]\n'
                   + f'create_project full_system {tq(out/"vivado_full")} -part xcu250-figd2104-2L-e\n'
                   + 'set_property board_part xilinx.com:au250:part0:1.3 [current_project]\n'
                   + 'set_property XPM_LIBRARIES {XPM_CDC XPM_FIFO XPM_MEMORY} [current_project]\n'
                   + '\n'.join(commands) + '\n} msg opts]} {puts stderr $msg;puts stderr [dict get $opts -errorinfo];exit 1}\nexit 0\n')
    provenance['generated_files'] = {str(p): sha(p) for p in [tcl, top, xdc]}
    (out/'input_manifest.json').write_text(json.dumps(provenance, indent=2)+'\n')
    shutil.copy2(__file__, out/Path(__file__).name)
    print(tcl)


if __name__ == '__main__':
    main()
