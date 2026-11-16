from pathlib import Path


def install(backend):
    """Log the membership of each native rejected submission, including ordinary retries."""
    helper = Path(backend) / 'inputs/carry_site_import.tcl'
    original = helper.read_text()
    text = original
    for name in ('pairs', 'whole', 'normal'):
        needle = '::place_cell $' + name
        assert text.count(needle) == 1
        text = text.replace(needle, 'amf_carry_import::logged_place $' + name)
    text += r'''
proc amf_carry_import::logged_place {pairs} {
    variable report
    if {[catch {::place_cell $pairs} message opts]} {
        set f [open [file join [file dirname $report] rejected-native-cells.tsv] a]
        try {
            puts $f "batch\t[llength $pairs]\t[string map [list \n { } \r { } \t { }] $message]"
            foreach {cell target} $pairs {puts $f "$cell\t$target"}
        } finally {close $f}
        return -options $opts $message
    }
    return $message
}
'''
    helper.write_text(text)
