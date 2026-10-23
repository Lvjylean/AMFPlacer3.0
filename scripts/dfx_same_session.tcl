# A single joint-backend Vivado owns the shell from cache enrichment to route.
# Barrier files are data only; no caller-supplied Tcl is evaluated.
namespace eval amf_same_session {
    proc publish {name data} {
        global dfx
        set path [file join $dfx(same_session_directory) $name]
        set f [open $path.tmp w];puts $f $data;close $f
        file rename $path.tmp $path
    }
    proc wait_data {name} {
        global dfx
        set path [file join $dfx(same_session_directory) $name]
        set deadline [expr {[clock seconds]+14400}]
        while {![file exists $path]} {
            if {[file exists [file join $dfx(same_session_directory) abort]]} {error "Parent cancelled this session"}
            if {[clock seconds]>$deadline} {error "Single-session barrier timeout"}
            after 250
        }
        set f [open $path r];set data [read $f];close $f
        if {![dict exists $data token] || [dict get $data token] ne $dfx(same_session_token)} {error "Session barrier identity mismatch"}
        return $data
    }
    proc enrich_and_wait {} {
        global dfx
        if {![info exists dfx(same_session_directory)]} {return}
        publish cache_started [list token $dfx(same_session_token) epoch_ms [clock milliseconds]]
        amf_dfx::timed native_owner_enrichment {
            dfx_pin_owner_overlay::collect $dfx(same_session_occupancy) \
                [file join $dfx(same_session_directory) owners.tsv] $dfx(rps)
        }
        publish owners_ready [list token $dfx(same_session_token) epoch_ms [clock milliseconds]]
        set cache [wait_data cache_ready]
        set f [open $dfx(output)/cache_added_seconds.tsv w]
        puts $f [dict get $cache added_seconds];close $f
        set modules [wait_data modules_ready]
        foreach row $dfx(rps) {
            lassign $row id rp pb
            set count [dict get $modules cells_$id]
            if {![string is integer -strict $count] || $count<=0} {error "Invalid module coverage"}
            set dfx(expected_cells_$id) $count
        }
        publish modules_consumed [list token $dfx(same_session_token) epoch_ms [clock milliseconds]]
    }
}

