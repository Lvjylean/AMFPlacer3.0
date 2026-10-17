#include "../lib/HiFPlacer/designInfo/native/StaticDesignCache.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
using namespace amf_native;
namespace fs=std::filesystem;
static int checks=0;
void check(bool x,const std::string &why){++checks;if(!x)throw std::runtime_error(why);}
void text(const fs::path &p,const std::string &s){std::ofstream f(p);f<<s;}
std::string read(const fs::path &p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void reject(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"expected rejection");}
int main(){try{
    auto root=fs::temp_directory_path()/("amf-native-cache-test-"+std::to_string(getpid()));fs::create_directory(root);
    text(root/"regions","region\tinstance\nstatic\t*\na\ttop/a\nb\ttop/b\n");
    text(root/"sites","region\tsite\nstatic\tS0\na\tS1\na\tS2\nb\tS3\n");
    text(root/"ports","module\tport\tdirection\tdriver\tclock\na\tx\tIN\ts/comb/O\t0\na\ty\tOUT\ttop/a/old/O\t0\na\tclk\tIN\t@DFX_CLOCK/buf/O\t1\nb\tx\tIN\ts/ff/Q\t0\nb\ty\tOUT\ttop/b/old/O\t0\n");
    Config cfg{{"Native static cache write",(root/"proposed.bin").string()},{"Native static identity","id1"},
        {"device","U250"},{"ClockPeriod","20"},{"Native full input sha256",std::string(64,'1')},
        {"Native source sha256",std::string(64,'2')},{"DFX regions file",(root/"regions").string()},
        {"DFX region sites file",(root/"sites").string()},{"Native full ports file",(root/"ports").string()}};
    StaticDesignCache db(cfg);
    auto cell=[&](std::string n,std::string type,std::vector<Pin> pins){db.observeCell(n,type);for(auto &p:pins)db.observePin(p);};
    cell("s/ff","FDRE",{{"s/ff/Q","Q","OUT","n1","s/ff/Q"},{"s/ff/C","C","IN","clk","@DFX_CLOCK/buf/O"}});
    cell("s/comb","LUT3",{{"s/comb/I0","I0","IN","n1","s/ff/Q"},{"s/comb/I1","I1","IN","nb","top/b/old/O"},{"s/comb/I2","I2","IN","nc","<const0>"},{"s/comb/O","O","OUT","n2","s/comb/O"}});
    cell("top/a/old","LUT1",{{"top/a/old/I0","I0","IN","n2","s/comb/O"},{"top/a/old/O","O","OUT","n3","top/a/old/O"}});
    cell("top/b/old","LUT1",{});
    cell("s/sink","FDRE",{{"s/sink/D","D","IN","n3","top/a/old/O"},{"s/sink/C","C","IN","clk","@DFX_CLOCK/buf/O"}});
    cell("top/abc/outside","LUT1",{{"top/abc/outside/I0","I0","IN","n0","<const0>"}});
    db.finishCapture();check(db.staticCells().size()==4,"static owner index must exclude both RMs and respect slash");
    db.writeContextAuditSpec((root/"audit.tsv").string());
    auto audit=read(root/"audit.tsv");
    check(audit.find("s/comb\tLUT3\ts/comb/I1\tIN\t@RM/b/y\t-")!=std::string::npos,"audit preserves cross-RM boundary driver identity");
    check(audit.find("s/comb\tLUT3\ts/comb/O\tOUT\ts/comb/O\t@RM/a/x")!=std::string::npos,"audit includes named RP fanout endpoints");
    text(root/"place.tcl","set result [catch {place_cell {s/ff S0/AFF\ns/comb S0/A6LUT\ns/sink S0/BFF\ntop/abc/outside S2/A6LUT\ntop/a/old S1/A6LUT\n}}]\n");
    db.savePlacement((root/"place.tcl").string(),cfg);
    auto draft=StaticDesignCache::load((root/"proposed.bin").string());check(!draft.isSealed(),"draft not usable");
    Config seal{{"identity","id1"},{"device","U250"},{"clock_period","20"},{"static_shell_sha256",std::string(64,'3')},{"static_connectivity_verified","1"},{"interfaces_verified","1"}};
    text(root/"endpoints","module\tport\tnode\tx\ty\nb\ty\tINT_X10Y20/TEST_NODE\t10\t20\n");
    seal["partition_pins_file"]=(root/"endpoints").string();
    text(root/"positions","cell\tsite\tbel\ns/ff\tS0\tAFF\ns/comb\tS0\tB6LUT\ns/sink\tS0\tBFF\ntop/abc/outside\tS2\tA6LUT\n");
    reject([&]{auto x=draft;x.seal((root/"positions").string(),seal,false);});
    db.seal((root/"positions").string(),seal,true);check(db.identity().at("corrected_static_cells")=="1","one-time backend correction");
    db.write((root/"sealed.bin").string());db=StaticDesignCache::load((root/"sealed.bin").string());
    check(db.isSealed()&&db.staticCells()[1].bel=="B6LUT","seal roundtrip");
    std::vector<Cell> dynamic{{"new/logic","LUT2","","",{{"new/logic/I0","I0","IN","x","@PORT/x"},{"new/logic/I1","I1","IN","c","<const1>"},{"new/logic/O","O","OUT","y","new/logic/O"}},"",false},
        {"new/reg","FDRE","","",{{"new/reg/D","D","IN","y","new/logic/O"},{"new/reg/C","C","IN","clk","@PORT/clk"},{"new/reg/Q","Q","OUT","z","new/reg/Q"}},"",false}};
    std::vector<Port> ports{{"a","x","IN","@PORT/x",false},{"a","y","OUT","new/reg/Q",false},{"a","clk","IN","@PORT/clk",true}};
    Config inc{{"Native static identity","id1"},{"Native static shell sha256",std::string(64,'3')},{"device","U250"},{"ClockPeriod","20"},{"Native workspace",(root/"inc").string()}};
    reject([&]{auto c=inc;draft.compose("a",dynamic,ports,c);});
    auto result=db.compose("a",dynamic,ports,inc);
    check(result.dynamicCells==2&&result.fixedCells==3,"changed cells + exact upstream/downstream cone");
    check(result.interfaceEndpoints==1,"other RM is one nonphysical endpoint");
    check(read(root/"inc/interface_endpoints.tsv").find("@AMF_INTERFACE/b/y\t10\t20\tINT_X10Y20/TEST_NODE")!=std::string::npos,"actual partition node and coordinates retained");
    check(result.netlist.find("drivepin=> @AMF_INTERFACE/b/y/O")!=std::string::npos,"static side input reaches other RP endpoint");
    check(result.netlist.find("top/a/old")==std::string::npos&&result.netlist.find("top/b/old")==std::string::npos,"no old dynamic cells");
    check(result.netlist.find("pin=> s/sink/D refpin=> D dir=> IN net=> n3 drivepin=> top/a/new/reg/Q")!=std::string::npos,"static sink reconnected to new driver");
    check(result.netlist.find("drivepin=> @DFX_CLOCK/buf/O")!=std::string::npos,"clock identity retained");
    check(result.netlist.find("pin=> s/comb/I2 refpin=> I2 dir=> IN net=> nc drivepin=> <const0>")!=std::string::npos,"static combinational constant pins preserved");
    check(read(root/"inc/blocked_sites.txt").find("S2\n")!=std::string::npos,"non-cone static occupancy still blocks placement");
    check(read(root/"inc/static_placement.tsv").find("s/comb\tS0\tB6LUT")!=std::string::npos,"final backend coordinate used");
    auto tryBad=[&](Config c,std::vector<Port> p){reject([&]{db.compose("a",dynamic,p,c);});};
    auto bad=inc;bad["ClockPeriod"]="10";tryBad(bad,ports);bad=inc;bad["Native static identity"]="wrong";tryBad(bad,ports);
    bad=inc;bad["Native static shell sha256"]=std::string(64,'4');tryBad(bad,ports);
    auto missing=ports;missing.pop_back();tryBad(inc,missing);auto changed=ports;changed[0].direction="OUT";tryBad(inc,changed);
    changed=ports;changed[2].clock=false;tryBad(inc,changed);
    reject([&]{auto c=inc;db.compose("a",dynamic,ports,c);}); // private workspace cannot be reused
    auto bytes=read(root/"sealed.bin");bytes.back()^=1;text(root/"broken.bin",bytes);
    reject([&]{StaticDesignCache::load((root/"broken.bin").string());});
    text(root/"truncated.bin",bytes.substr(0,30));reject([&]{StaticDesignCache::load((root/"truncated.bin").string());});
    reject([&]{db.write((root/"sealed.bin").string());});
    auto invalid=dynamic;invalid[0].pins[0].driver="@PORT/missing";reject([&]{auto c=inc;db.compose("a",invalid,ports,c);});
    std::istringstream input(result.netlist);auto parsed=StaticDesignCache::readNetlist(input);check(parsed.size()==6,"native output parsable");
    // Unconnected data pin syntax must survive, not inherit the previous driver.
    std::istringstream empty("curCell=> c type=> LUT1\n pin=> c/I0 refpin=> I0 dir=> IN net=>  drivepin=> \n");
    check(StaticDesignCache::readNetlist(empty)[0].pins[0].driver.empty(),"unconnected pin");
    // A shared data driver must retain its internal consumers while each
    // exported port gets its own AMF-placed identity LUT1 (truth table 0,1).
    auto normCells=dynamic;
    std::vector<Port> normPorts{{"a","out[0]","OUT","new/logic/O",false},
                               {"a","out[1]","OUT","new/logic/O",false},
                               {"a","unique","OUT","new/reg/Q",false}};
    auto buffers=StaticDesignCache::normalizeOutputInterfaces("a",normCells,normPorts);
    check(buffers.size()==2&&normCells.size()==4,"shared output adds exactly one buffer per port");
    check(normCells[1].pins[0].driver=="new/logic/O","internal fanout retained");
    check(normPorts[0].driver!=normPorts[1].driver&&normPorts[2].driver=="new/reg/Q","unique external drivers, unrelated port unchanged");
    for(std::size_t i=0;i<buffers.size();++i) {
        const auto &b=buffers[i];const auto &c=normCells[2+i];
        check(c.type=="LUT1"&&c.pins.size()==2&&c.pins[0].driver=="new/logic/O"&&c.pins[1].driver==c.name+"/O","one-input identity buffer graph");
        check(b.cell.find('[')==std::string::npos&&b.cell.find('/')==std::string::npos,"escaped flat adapter name");
    }
    check(StaticDesignCache::normalizeOutputInterfaces("a",normCells,normPorts).empty(),"normalized outputs are not buffered twice");
    auto rawPorts=std::vector<Port>{{"a","x","OUT","new/logic/O",false},{"a","y","OUT","new/logic/O",false}};
    for(const auto &driver:std::vector<std::string>{"<const0>","@PORT/x","missing/O"}) {
        auto pp=rawPorts;for(auto &p:pp)p.driver=driver;
        reject([&]{auto cc=dynamic;StaticDesignCache::normalizeOutputInterfaces("a",cc,pp);});
    }
    auto clockPorts=rawPorts;clockPorts[0].clock=true;
    reject([&]{auto cc=dynamic;StaticDesignCache::normalizeOutputInterfaces("a",cc,clockPorts);});
    auto collision=dynamic;collision.push_back({"AMF_DFX_OutputBuffer_78","LUT1","","",{},"",false});
    reject([&]{auto pp=rawPorts;StaticDesignCache::normalizeOutputInterfaces("a",collision,pp);});
    check(collision.size()==3,"rejected plan cannot partially mutate the graph");
    std::cout<<"native static cache: "<<checks<<" checks passed; evidence="<<root<<"\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}
