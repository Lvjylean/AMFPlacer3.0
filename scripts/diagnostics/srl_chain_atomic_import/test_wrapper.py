from pathlib import Path
import tempfile,subprocess,sys
from srl_chain_import import chains_from_edges
assert chains_from_edges([('a','b'),('b','c'),('d','e')])==[['a','b','c'],['d','e']]
for bad in [[('a','a')],[('a','b'),('a','c')],[('a','c'),('b','c')],[('a','b'),('b','a')]]:
 try:chains_from_edges(bad)
 except AssertionError:pass
 else:raise AssertionError(bad)
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'groups.tsv').write_text('chain\tcell\ttarget\n0\ta\tS/A6LUT\n0\tb\tS/B6LUT\n1\tc\tT/A6LUT\n1\td\tT/B6LUT\n')
 code='source {'+str(Path(__file__).with_name('srl_chain_import.tcl').resolve())+'}\n'
 code+='set root {'+t+'}\n'
 code+=r'''
set native {};set normal {};set fail 0
namespace eval amf_carry_import {}
proc amf_carry_import::logged_place {pairs} {global native fail;lappend native $pairs;if {$fail} {error simulated};return {}}
proc amf_carry_import::place {pairs} {global normal;lappend normal $pairs;return {}}
proc check {v} {if {![uplevel 1 [list expr $v]]} {error "assertion failed: $v"}}
amf_srl_import::init $root/groups.tsv $root/report.tsv
amf_srl_import::place {b S/B6LUT x X/AFF a S/A6LUT}
check {[llength $native]==1 && [lindex $native 0] eq {a S/A6LUT b S/B6LUT}}
check {$normal eq {{x X/AFF}}}
amf_srl_import::place {a S/A6LUT b S/B6LUT}
check {[llength $native]==1}
check {[catch {amf_srl_import::place {a BAD}}]}
check {[llength $native]==1}
set fail 1
check {[catch {amf_srl_import::place {c T/A6LUT}}]}
check {[catch {amf_srl_import::place {d T/B6LUT}}]}
check {[llength $native]==2}
puts MOCK_TESTS_PASSED
'''
 r=subprocess.run(['tclsh'],input=code,text=True,capture_output=True)
 assert r.returncode==0 and 'MOCK_TESTS_PASSED' in r.stdout and not r.stderr,(r.stdout,r.stderr)
 print(r.stdout)
print('Graph and wrapper checks passed')
