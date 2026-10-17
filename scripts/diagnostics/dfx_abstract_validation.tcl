# A single-RM Abstract Shell validation. No AMF rerun and no static re-placement.
source [lindex $argv 0]
set here [file dirname [info script]]
source $here/dfx_multi_common.tcl
source $here/dfx_import.tcl
source $here/dfx_boundary_compatibility.tcl
source $here/dfx_probe_checks.tcl
namespace eval abscheck {
    proc region {} {
        global dfx
        set pb [get_pblocks $dfx(pblock)]
        return [list [lsort [get_sites -of_objects $pb]] [get_property IS_SOFT $pb] \
            [get_property EXCLUDE_PLACEMENT $pb] [get_property CONTAIN_ROUTING $pb]]
    }
    proc identities {} {
        global dfx
        set result [dict create]
        set filter [format {IS_PRIMITIVE == 1 && PRIMITIVE_LEVEL != INTERNAL && REF_NAME != GND && REF_NAME != VCC && NAME =~ "%s/*"} $dfx(active_partition)]
        foreach c [get_cells -hierarchical -filter $filter] {
            dict set result [get_property NAME $c] [get_property REF_NAME $c]
        }
        return $result
    }
    proc counts {label} {
        global dfx
        set f [open $dfx(output)/design_counts.tsv a]
        puts $f "$label\t[llength [amf_dfx::primitive_cells]]\t[llength [get_nets -hierarchical]]"
        close $f
    }
    proc ensure_drc {label} {
        global dfx
        set f [open $dfx(output)/${label}_drc_counts.tsv r]
        set rows [read $f]; close $f
        foreach row [split [string trim $rows] \n] {
            lassign [split $row \t] key value
            if {$value != 0} {error "$label DRC $key=$value"}
        }
    }
    proc marker {name} {
        global dfx
        set f [open $dfx(output)/${name}.txt w]; puts $f passed; close $f
    }
    proc boundary {label baseline} {
        global dfx
        set now [amf_dfx::boundary_snapshot]
        amf_dfx::save_dict $dfx(output)/${label}_boundary.tcldata $now
        amf_dfx::save_dict $dfx(output)/${label}_boundary_comparison.tcldata \
            [dfx_boundary::compare $baseline $now]
    }
}
proc main {} {
    global dfx
    set out $dfx(output)
    set_param general.maxThreads $dfx(vivado_threads)
    switch -- $dfx(phase) {
        generate {
            amf_dfx::timed open_reference {open_checkpoint $dfx(reference_checkpoint)}
            amf_dfx::assert_context
            amf_dfx::save_dict $out/reference_boundary.tcldata [amf_dfx::boundary_snapshot]
            amf_dfx::save_dict $out/reference_region.tcldata [abscheck::region]
            amf_dfx::timed reference_counts {abscheck::counts full_reference}
            # Includes Vivado's automatic creation-time verification; no -quiet.
            amf_dfx::timed generate_abstract_shell {
                write_abstract_shell -cell $dfx(active_partition) $dfx(abstract_checkpoint)
            }
            abscheck::marker shell_generation_passed
        }
        implement {
            # One-time identity audit of the exact input used by the full-shell control.
            amf_dfx::timed open_control_imported {open_checkpoint $dfx(control_imported)}
            amf_dfx::assert_context
            amf_dfx::timed control_identity_audit {
                set identities [abscheck::identities]
                amf_dfx::save_dict $out/control_identities.tcldata $identities
            }
            close_design
            amf_dfx::timed open_abstract {open_checkpoint $dfx(abstract_checkpoint)}
            amf_dfx::assert_context
            if {[abscheck::region] ne [amf_dfx::read_dict $dfx(generation_dir)/reference_region.tcldata]} {
                error "Abstract Shell Pblock sites/properties changed"
            }
            set boundary [amf_dfx::read_dict $dfx(generation_dir)/reference_boundary.tcldata]
            abscheck::boundary shell $boundary
            amf_dfx::timed abstract_counts {abscheck::counts abstract_shell}
            amf_dfx::timed link_rm {read_checkpoint -cell $dfx(active_partition) $dfx(rm_checkpoint)}
            abscheck::boundary linked $boundary
            amf_dfx::timed active_opt {opt_design}
            amf_dfx::assert_same $identities [abscheck::identities] "Full/abstract active cell names and primitives"
            abscheck::boundary optimized $boundary
            probe::load_inputs
            amf_dfx::timed reset_active_lut_groups {amf_dfx::reset_active_lut_groups $out $probe::requested}
            amf_dfx::timed import_amf_placement {
                dict for {cell target} $probe::requested {place_cell [list $cell $target]}
            }
            amf_dfx::timed imported_checks {probe::positions imported 1; abscheck::boundary imported $boundary}
            amf_dfx::timed write_imported {write_checkpoint $out/reports/imported.dcp}
            puts "AMF_ABSTRACT_PLACEMENT_SKIPPED paired_control=route-only"
            amf_dfx::timed vivado_route {route_design}
            amf_dfx::timed write_routed {write_checkpoint $out/reports/routed.dcp}
            amf_dfx::timed routed_checks {probe::positions routed 1; abscheck::boundary routed $boundary}
            amf_dfx::timed audit_routed {amf_dfx::audit $out routed; abscheck::ensure_drc routed}
            amf_dfx::timed route_status {report_route_status -file $out/route_status.rpt}
            # Full and Abstract Shell checkpoints cannot be directly pr_verify compared.
            amf_dfx::timed pr_verify_abstract {
                pr_verify -full_check $dfx(abstract_checkpoint) $out/reports/routed.dcp
            }
            abscheck::marker pr_verify_abstract_passed
            amf_dfx::timed save_module_audit {
                amf_dfx::save_dict $out/module_placement.tcldata [amf_multi::module_snapshot $dfx(active_partition)]
                amf_dfx::save_dict $out/module_routes.tcldata [amf_multi::module_routing $dfx(active_partition)]
            }
            amf_dfx::timed write_rm_routed {write_checkpoint -cell $dfx(active_partition) $out/reports/rm_routed.dcp}
            amf_dfx::timed partial_bitstream {write_bitstream -cell $dfx(active_partition) $out/reports/partial.bit}
        }
        assemble {
            amf_dfx::timed open_reference {open_checkpoint $dfx(reference_checkpoint)}
            amf_dfx::assert_context
            set boundary [amf_dfx::boundary_snapshot]
            amf_dfx::timed carve_active_rm {update_design -cell $dfx(active_partition) -black_box; lock_design -level routing}
            amf_dfx::timed import_routed_rm {
                read_checkpoint -strict -cell $dfx(active_partition) $dfx(implementation_dir)/reports/rm_routed.dcp
            }
            amf_dfx::timed assembly_preservation {
                probe::load_inputs; probe::positions assembled 1
                abscheck::boundary assembled $boundary
                amf_dfx::assert_same [amf_dfx::read_dict $dfx(implementation_dir)/module_placement.tcldata] \
                    [amf_multi::module_snapshot $dfx(active_partition)] "Assembled RM placement"
                amf_dfx::assert_same [amf_dfx::read_dict $dfx(implementation_dir)/module_routes.tcldata] \
                    [amf_multi::module_routing $dfx(active_partition)] "Assembled RM footprint routing"
            }
            # Only A is replaced: this validates A1/B0, not the combined A1/B1 configuration.
            amf_dfx::timed write_assembled {write_checkpoint $out/reports/assembled_routed.dcp}
            amf_dfx::timed audit_assembled {amf_dfx::audit $out assembled; abscheck::ensure_drc assembled}
            amf_dfx::timed route_status {report_route_status -file $out/route_status.rpt}
            amf_dfx::timed pr_verify_full {
                pr_verify -full_check $dfx(reference_checkpoint) $out/reports/assembled_routed.dcp
            }
            abscheck::marker pr_verify_full_passed
            amf_dfx::timed full_bitstream {write_bitstream $out/reports/full.bit}
            amf_dfx::timed partial_bitstream {write_bitstream -cell $dfx(active_partition) $out/reports/partial.bit}
        }
        default {error "Unknown Abstract Shell validation phase"}
    }
    puts "AMF_ABSTRACT_PHASE_COMPLETED $dfx(phase)"
}
if {[catch {main} msg opts]} {
    set f [open $dfx(output)/failure.txt w]; puts $f $msg; puts $f [dict get $opts -errorinfo]; close $f
    puts stderr $msg; puts stderr [dict get $opts -errorinfo]; exit 2
}
exit 0
