#include "StaticDesignCache.h"
#include "BoundarySitePolicy.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <zlib.h>

namespace amf_native {
namespace {
void need(bool ok,const std::string &message) { if(!ok) throw std::runtime_error("Native static cache: "+message); }
std::string required(const Config &cfg,const std::string &key) {
    auto it=cfg.find(key); need(it!=cfg.end()&&!it->second.empty(),"missing "+key); return it->second;
}
bool starts(const std::string &s,const std::string &p) {return s.compare(0,p.size(),p)==0;}
bool under(const std::string &s,const std::string &p) {return starts(s,p+"/");}
bool special(const std::string &s) {return s=="<const0>"||s=="<const1>"||starts(s,"@DFX_CLOCK/");}
std::string parent(const std::string &p) {return p.substr(0,p.rfind('/'));}
std::string token(const Port &p) {return "@RM/"+p.module+"/"+p.name;}
std::vector<std::string> split(const std::string &s,char sep='\t') {
    std::vector<std::string> v; std::size_t p=0,q;
    do {q=s.find(sep,p);v.push_back(s.substr(p,q-p));p=q+1;}while(q!=std::string::npos);
    return v;
}
std::vector<std::vector<std::string>> table(const std::string &file,const std::string &header) {
    std::ifstream f(file);std::string line;need(bool(f)&&bool(std::getline(f,line))&&line==header,"invalid table "+file);
    std::vector<std::vector<std::string>> rows;
    while(std::getline(f,line)) {need(!line.empty(),"blank row in "+file);rows.push_back(split(line));}
    return rows;
}
void atom(const std::string &s) {need(!s.empty()&&s.find_first_of(" \t\r\n{}")==std::string::npos,"invalid name "+s);}
std::set<std::string> siteFile(const std::string &file) {
    std::ifstream f(file);need(bool(f),"cannot read "+file);std::string s;std::set<std::string> result;
    while(std::getline(f,s)) {atom(s);need(result.insert(s).second,"duplicate site "+s);}return result;
}
void checkedText(const std::filesystem::path &file,const std::string &text) {
    need(!std::filesystem::exists(file),"refusing overwrite "+file.string());
    std::ofstream f(file);f<<text;f.close();need(bool(f),"write failed "+file.string());
}
std::string field(const std::string &line,const std::string &label,const std::string &next="") {
    auto begin=line.find(label);need(begin!=std::string::npos,"missing "+label+" in "+line);begin+=label.size();
    auto end=next.empty()?line.size():line.find(next,begin);need(end!=std::string::npos,"missing "+next);
    auto s=line.substr(begin,end-begin);auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return "";
    return s.substr(a,s.find_last_not_of(" \t\r")-a+1);
}
bool terminal(const Cell &c,bool dspCritical) {
    if(c.type=="AMF_INTERFACE")return true;
    if(c.type=="DSP48E2")return !dspCritical||c.registeredDSP;
    static const std::regex re("^(FD(RE|SE|CE|PE)|LDCE|AND2B1L|SRL16E|SRLC32E|RAM.*|FIFO(18|36)E2|URAM288.*)$");
    return std::regex_match(c.type,re);
}
// A compact, deterministic, bounded binary format with payload CRC. The launch
// contract additionally validates SHA256 of every input (including this file).
void number(std::ostream &o,std::uint64_t v) {for(int i=0;i<8;++i)o.put(char((v>>(8*i))&255));}
std::uint64_t number(std::istream &i) {std::uint64_t v=0;for(int k=0;k<8;++k){int b=i.get();need(b!=EOF,"truncated database");v|=std::uint64_t(b)<<(8*k);}return v;}
void str(std::ostream &o,const std::string &s) {number(o,s.size());o.write(s.data(),s.size());}
std::string str(std::istream &i) {auto n=number(i);need(n<=64*1024*1024,"invalid string size");std::string s(n,'\0');i.read(&s[0],n);need(bool(i),"truncated string");return s;}
std::uint64_t count(std::istream &i) {auto n=number(i);need(n<=20000000,"invalid collection count");return n;}
void saveSet(std::ostream &o,const std::set<std::string>&s){number(o,s.size());for(auto &x:s)str(o,x);}
std::uint32_t checksum(const std::string &data) {
    std::uint32_t crc=0;
    for(std::size_t offset=0;offset<data.size();offset+=1048576)
        crc=crc32(crc,reinterpret_cast<const Bytef*>(data.data()+offset),std::min<std::size_t>(1048576,data.size()-offset));
    return crc;
}
std::set<std::string> loadSet(std::istream &i){std::set<std::string>s;auto n=count(i);while(n--)need(s.insert(str(i)).second,"duplicate set item");return s;}
void metadataFiles(std::vector<Cell> &cells,const Config &cfg) {
    std::unordered_map<std::string,Cell*> map;for(auto &c:cells)map.emplace(c.name,&c);
    if(cfg.count("SRL control metadata file")) {
        auto rows=table(cfg.at("SRL control metadata file"),"cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks");
        std::set<std::string> seen;
        for(auto &r:rows) {need(r.size()==8&&seen.insert(r[0]).second,"invalid SRL metadata");
            if(!map.count(r[0]))continue;
            auto &c=*map.at(r[0]);need(c.type==r[1],"SRL type mismatch");
            c.srlMetadata.clear();for(std::size_t k=1;k<r.size();++k)c.srlMetadata+=(k==1?"":"\t")+r[k];}
    }
    if(cfg.count("DSP registered outputs file")) {
        std::ifstream f(cfg.at("DSP registered outputs file"));std::string line;
        need(bool(std::getline(f,line))&&line=="AMF_DSP_REGISTERED_OUTPUTS 1","invalid DSP metadata");
        while(std::getline(f,line)) {std::istringstream s(line);std::string name,value,extra;s>>name>>value;
            need(value=="PREG=1"&&!(s>>extra),"invalid DSP row");if(map.count(name)){need(map.at(name)->type=="DSP48E2","non-DSP certificate");map.at(name)->registeredDSP=true;}}
    }
    for(auto &c:cells)if(c.type=="SRL16E"||c.type=="SRLC32E")need(!c.srlMetadata.empty(),"missing SRL metadata "+c.name);
}
}

bool StaticDesignCache::captureEnabled(const Config &c){return c.count("Native static cache write")!=0;}
bool StaticDesignCache::reuseEnabled(const Config &c){return c.count("Native static cache read")!=0;}
std::string StaticDesignCache::owner(const std::string &name) const {
    for(auto &m:modules)if(under(name,m.second.instance))return m.first;
    return "static";
}
std::vector<Port> StaticDesignCache::readPorts(const std::string &file) {
    std::vector<Port> ports;std::set<std::pair<std::string,std::string>> seen;
    for(auto &r:table(file,"module\tport\tdirection\tdriver\tclock")) {
        need(r.size()==5,"port row field count");atom(r[0]);atom(r[1]);
        need((r[2]=="IN"||r[2]=="OUT")&&(r[4]=="0"||r[4]=="1"),"unsupported port direction/clock");
        need(seen.emplace(r[0],r[1]).second,"duplicate port bit");
        ports.push_back({r[0],r[1],r[2],r[3]=="-"?"":r[3],r[4]=="1"});
    }need(!ports.empty(),"empty interface");return ports;
}
StaticDesignCache::StaticDesignCache(const Config &cfg) {
    outputPath=required(cfg,"Native static cache write");
    need(!reuseEnabled(cfg),"cannot capture and reuse simultaneously");
    metadata["identity"]=required(cfg,"Native static identity");
    metadata["device"]=required(cfg,"device");metadata["clock_period"]=required(cfg,"ClockPeriod");
    metadata["input_sha256"]=required(cfg,"Native full input sha256");
    metadata["source_sha256"]=required(cfg,"Native source sha256");
    metadata["timing_model"]="AMF estimated connectivity delays; no routed boundary budgets or wire occupancy";
    if(cfg.count("Native dedicated interfaces file"))
        for(auto &r:table(cfg.at("Native dedicated interfaces file"),"fabric_pin\tdedicated_driver")) {
            need(r.size()==2&&!r[1].empty(),"invalid dedicated-interface row");
            need(metadata.emplace("dedicated_pin/"+r[0],r[1]).second,"duplicate dedicated-interface pin");
        }
    for(auto &r:table(required(cfg,"DFX regions file"),"region\tinstance")) {
        need(r.size()==2,"region row");if(r[0]=="static"){need(r[1]=="*","static region");continue;}
        atom(r[0]);atom(r[1]);need(r[1].back()!='/',"trailing slash");
        for(auto &m:modules)need(r[1]!=m.second.instance&&!under(r[1],m.second.instance)&&!under(m.second.instance,r[1]),"overlapping modules");
        need(modules.emplace(r[0],Module{r[0],r[1],{}, {}}).second,"duplicate module");
    }need(!modules.empty(),"no modules");
    for(auto &r:table(required(cfg,"DFX region sites file"),"region\tsite")) {
        need(r.size()==2,"region sites row");if(r[0]=="static")continue;
        need(modules.count(r[0])&&modules.at(r[0]).sites.insert(r[1]).second,"invalid region site");
    }
    if(cfg.count("Native prohibited sites file"))baselineBlocked=siteFile(cfg.at("Native prohibited sites file"));
    for(auto &p:readPorts(required(cfg,"Native full ports file"))) {
        need(modules.count(p.module),"port module missing");modules.at(p.module).ports.push_back(p);
        if(p.direction=="OUT"&&!p.driver.empty()&&!special(p.driver)) {
            auto entry=dynamicOutputTokens.emplace(p.driver,token(p));
            if(!entry.second)entry.first->second=""; // explicit boundary-pin mapping required for ambiguity
        }
        need(p.direction!="OUT"||p.driver.empty()||!special(p.driver),"constant/clock output needs preserved outer-net mapping: "+p.name);
    }
    if(cfg.count("Native boundary pins file"))for(auto &r:table(cfg.at("Native boundary pins file"),"pin\tmodule\tport")) {
        need(r.size()==3&&modules.count(r[1]),"boundary row");
        const auto &ports=modules.at(r[1]).ports;
        auto p=std::find_if(ports.begin(),ports.end(),[&](const Port &p){return p.name==r[2]&&p.direction=="OUT";});
        need(p!=ports.end()&&boundaryPinTokens.emplace(r[0],token(*p)).second,"invalid/duplicate boundary pin");
    }
    for(auto &m:modules)need(!m.second.ports.empty()&&!m.second.sites.empty(),"empty module ports or mask");
}
void StaticDesignCache::observeCell(const std::string &name,const std::string &type) {
    atom(name);atom(type);currentCell=name;currentStatic=owner(name)=="static";++ownerCounts[owner(name)];
    if(currentStatic){need(nameIndex.emplace(name,cells.size()).second,"duplicate static cell "+name);cells.push_back({name,type,"","",{},"",false});}
}
void StaticDesignCache::observePin(const Pin &pin) {
    if(!currentStatic)return;
    need(parent(pin.name)==currentCell,"pin owner mismatch");cells.back().pins.push_back(pin);
}
std::string StaticDesignCache::interfaceDriver(const std::string &d) const {
    if(d.empty()||special(d)||owner(parent(d))=="static")return d;
    auto it=dynamicOutputTokens.find(d);
    need(it!=dynamicOutputTokens.end()&&!it->second.empty(),"ambiguous/missing RP output mapping for "+d);
    return it->second;
}
void StaticDesignCache::finishCapture() {
    for(auto &c:cells)for(auto &p:c.pins) {
        if(boundaryPinTokens.count(p.name))p.driver=boundaryPinTokens.at(p.name);
        else p.driver=interfaceDriver(p.driver);
    }
    for(auto &m:modules)for(auto &p:m.second.ports)if(p.direction=="IN")p.driver=interfaceDriver(p.driver);
    captured=true;indexAndValidate();
}
void StaticDesignCache::indexAndValidate() {
    nameIndex.clear();std::unordered_set<std::string> pins;
    for(std::size_t i=0;i<cells.size();++i) {
        const auto &c=cells[i];need(owner(c.name)=="static"&&nameIndex.emplace(c.name,i).second,"nonstatic/duplicate cached cell");
        for(auto &p:c.pins)need(parent(p.name)==c.name&&pins.insert(p.name).second&&(p.direction=="IN"||p.direction=="OUT"),"bad cached pin");
    }
    for(auto &c:cells)for(auto &p:c.pins) {
        if(p.driver.empty()||special(p.driver)||starts(p.driver,"@RM/"))continue;
        auto dedicated=metadata.find("dedicated_pin/"+p.name);
        need(pins.count(p.driver)||(dedicated!=metadata.end()&&dedicated->second==p.driver),"unrepresented static driver "+p.driver);
    }
}
void StaticDesignCache::savePlacement(const std::string &path,const Config &cfg) {
    need(captured&&!sealed,"capture lifecycle");metadataFiles(cells,cfg);
    std::ifstream f(path);need(bool(f),"cannot read final AMF placement");std::string line;bool in=false;std::set<std::string> seen;
    while(std::getline(f,line)) {
        auto pos=line.find("[catch {place_cell {");
        if(pos!=std::string::npos) {in=true;line=line.substr(pos+std::strlen("[catch {place_cell {"));}
        if(!in)continue;
        auto end=line.find("}}]");if(end!=std::string::npos){line=line.substr(0,end);in=false;}
        std::istringstream s(line);std::string name,loc;while(s>>name>>loc) {
            if(!nameIndex.count(name))continue;
            auto &c=cells.at(nameIndex.at(name));need(seen.insert(name).second,"duplicate placement "+name);
            auto slash=loc.find('/');c.site=loc.substr(0,slash);
            if(slash!=std::string::npos)c.bel=loc.substr(slash+1);
            else if(c.type=="RAMB36E2"||c.type=="FIFO36E2")c.bel="RAMB36E2";
            else throw std::runtime_error("Native static cache: missing exported BEL for "+name);
        }
    }
    need(seen.size()==cells.size(),"incomplete static placement export "+std::to_string(seen.size())+"/"+std::to_string(cells.size()));
    metadata["placement_source"]="AMF final export; backend certification pending";
    write(outputPath);
}
std::vector<Cell> StaticDesignCache::readNetlist(std::istream &in) {
    std::vector<Cell> cells;std::set<std::string> names;std::string line;
    while(std::getline(in,line)) {
        if(starts(line,"curCell=> ")) {
            auto name=field(line,"curCell=> "," type=> ");atom(name);need(names.insert(name).second,"duplicate module cell");
            cells.push_back({name,field(line,"type=> "),"","",{},"",false});
        }else {
            need(!cells.empty(),"pin before cell");
            Pin p{field(line,"pin=> "," refpin=> "),field(line,"refpin=> "," dir=> "),
                  field(line,"dir=> "," net=> "),field(line,"net=> "," drivepin=>"),field(line,"drivepin=>")};
            need(parent(p.name)==cells.back().name,"module pin parent");cells.back().pins.push_back(std::move(p));
        }
    }need(!cells.empty(),"empty module netlist");return cells;
}

void StaticDesignCache::write(const std::string &file) const {
    std::ostringstream p(std::ios::out|std::ios::binary);number(p,sealed?1:0);number(p,metadata.size());
    for(auto &x:metadata){str(p,x.first);str(p,x.second);}number(p,modules.size());
    for(auto &x:modules){auto &m=x.second;str(p,m.id);str(p,m.instance);saveSet(p,m.sites);number(p,m.ports.size());
        for(auto &v:m.ports){str(p,v.name);str(p,v.direction);str(p,v.driver);number(p,v.clock);
            str(p,v.partitionNode);str(p,std::to_string(v.x));str(p,std::to_string(v.y));}}
    saveSet(p,baselineBlocked);number(p,cells.size());
    for(auto &c:cells){str(p,c.name);str(p,c.type);str(p,c.site);str(p,c.bel);str(p,c.srlMetadata);number(p,c.registeredDSP);number(p,c.pins.size());
        for(auto &q:c.pins){str(p,q.name);str(p,q.ref);str(p,q.direction);str(p,q.alias);str(p,q.driver);}}
    auto data=p.str();need(data.size()<32ULL*1024*1024*1024,"database too large");
    std::ostringstream out(std::ios::out|std::ios::binary);out.write(metadata.count("dfx_resource_policy")?"AMFSTATIC3\n":"AMFSTATIC2\n",11);number(out,data.size());
    number(out,checksum(data));out<<data;
    checkedText(file,out.str());
}
void StaticDesignCache::writeContextAuditSpec(const std::string &file) const {
    std::unordered_map<std::string,std::size_t> pins;
    std::unordered_map<std::string,std::vector<std::size_t>> sinks;
    for(std::size_t i=0;i<cells.size();++i)for(auto &p:cells[i].pins) {
        pins[p.name]=i;if(p.direction=="IN"&&!p.driver.empty())sinks[p.driver].push_back(i);
    }
    std::set<std::size_t> selected,seenUp,seenDown;std::queue<std::size_t> up,down;
    auto input=[&](const std::string &d){if(pins.count(d)){auto i=pins.at(d);selected.insert(i);up.push(i);}};
    auto output=[&](const std::string &d){if(sinks.count(d))for(auto i:sinks.at(d)){selected.insert(i);down.push(i);}};
    for(auto &m:modules)for(auto &p:m.second.ports)if(!p.clock) {
        if(p.direction=="IN")input(p.driver);else output(token(p));
    }
    while(!up.empty()||!down.empty()) {
        if(!up.empty()){auto i=up.front();up.pop();if(!seenUp.insert(i).second||terminal(cells[i],true))continue;
            for(auto &p:cells[i].pins)if(p.direction=="IN")input(p.driver);
        }else{auto i=down.front();down.pop();if(!seenDown.insert(i).second||terminal(cells[i],true))continue;
            up.push(i);for(auto &p:cells[i].pins)if(p.direction=="OUT")output(p.driver);}
    }
    std::unordered_map<std::string,std::set<std::string>> sinkNames;
    for(auto &c:cells)for(auto &p:c.pins)if(p.direction=="IN"&&!p.driver.empty())sinkNames[p.driver].insert(p.name);
    for(auto &m:modules)for(auto &p:m.second.ports)if(p.direction=="IN"&&!p.driver.empty())sinkNames[p.driver].insert(token(p));
    std::ostringstream out;out<<"cell\tprimitive\tpin\tdirection\tdriver\tsinks\n";
    for(auto i:selected)for(auto &p:cells[i].pins) {
        std::string names;
        if(p.direction=="OUT"&&sinkNames.count(p.name))for(auto &s:sinkNames.at(p.name))names+=(names.empty()?"":" ")+s;
        out<<cells[i].name<<'\t'<<cells[i].type<<'\t'<<p.name<<'\t'<<p.direction<<'\t'<<(p.driver.empty()?"-":p.driver)<<'\t'<<(names.empty()?"-":names)<<'\n';
    }
    checkedText(file,out.str());
}
StaticDesignCache StaticDesignCache::load(const std::string &file) {
    std::ifstream f(file,std::ios::binary);need(bool(f),"cannot open database");char magic[11];f.read(magic,11);
    const std::string version(magic,11);
    need(f&&(version=="AMFSTATIC2\n"||version=="AMFSTATIC3\n"),"database version mismatch");auto n=number(f),crc=number(f);
    need(n<32ULL*1024*1024*1024&&std::filesystem::file_size(file)==n+27,"invalid database length");
    std::string data(n,'\0');f.read(&data[0],n);need(bool(f)&&crc==checksum(data),"database checksum mismatch");
    std::istringstream p(data,std::ios::in|std::ios::binary);StaticDesignCache db;auto state=number(p);need(state<=1,"cache seal state");db.sealed=state;
    auto k=count(p);while(k--){auto key=str(p),value=str(p);need(db.metadata.emplace(key,value).second,"duplicate metadata");}
    k=count(p);while(k--){Module m;m.id=str(p);m.instance=str(p);m.sites=loadSet(p);auto j=count(p);
        while(j--){Port q;q.module=m.id;q.name=str(p);q.direction=str(p);q.driver=str(p);auto clk=number(p);need(clk<=1,"clock boolean");q.clock=clk;
            q.partitionNode=str(p);q.x=std::stod(str(p));q.y=std::stod(str(p));need(std::isfinite(q.x)&&std::isfinite(q.y),"invalid endpoint coordinate");m.ports.push_back(q);}
        need(db.modules.emplace(m.id,std::move(m)).second,"duplicate module");}
    db.baselineBlocked=loadSet(p);k=count(p);while(k--){Cell c;c.name=str(p);c.type=str(p);c.site=str(p);c.bel=str(p);c.srlMetadata=str(p);auto reg=number(p);need(reg<=1,"DSP boolean");c.registeredDSP=reg;
        auto j=count(p);while(j--){Pin q;q.name=str(p);q.ref=str(p);q.direction=str(p);q.alias=str(p);q.driver=str(p);c.pins.push_back(std::move(q));}db.cells.push_back(std::move(c));}
    need(p.peek()==EOF,"trailing database data");
    need((version=="AMFSTATIC3\n")==bool(db.metadata.count("dfx_resource_policy")),"resource policy/database version mismatch");
    db.captured=true;db.indexAndValidate();return db;
}
void StaticDesignCache::seal(const std::string &positions,const Config &contract,bool allowCorrection) {
    need(captured&&!sealed,"seal requires unsealed capture");
    for(const auto &key:{"identity","device","clock_period"})need(required(contract,key)==required(metadata,key),std::string("seal mismatch ")+key);
    auto shell=required(contract,"static_shell_sha256");need(std::regex_match(shell,std::regex("[0-9a-f]{64}")),"invalid shell SHA256");
    need(required(contract,"static_connectivity_verified")=="1"&&required(contract,"interfaces_verified")=="1","backend connectivity/interface certification missing");
    std::set<std::string> seen,occupied;std::size_t changed=0;
    for(auto &r:table(positions,"cell\tsite\tbel")) {
        need(r.size()==3&&nameIndex.count(r[0])&&seen.insert(r[0]).second&&!r[1].empty()&&!r[2].empty(),"invalid static final position");
        auto &c=cells.at(nameIndex.at(r[0]));bool same=c.site==r[1]&&c.bel==r[2];need(same||allowCorrection,"static backend position changed "+c.name);
        if(!same)++changed;
        c.site=r[1];c.bel=r[2];need(occupied.insert(c.site+"/"+c.bel).second,"duplicate static BEL");
    }
    need(seen.size()==cells.size(),"incomplete backend static positions");
    // Coordinates are supplied by the frozen device-coordinate adapter, never
    // by old RM cell locations. Preserve the actual partition node identity.
    if(contract.count("partition_pins_file")) {
        std::set<std::pair<std::string,std::string>> used;
        for(auto &r:table(contract.at("partition_pins_file"),"module\tport\tnode\tx\ty")) {
            need(r.size()==5&&modules.count(r[0])&&used.emplace(r[0],r[1]).second,"invalid partition-pin row");
            auto &ports=modules.at(r[0]).ports;auto it=std::find_if(ports.begin(),ports.end(),[&](const Port &p){return p.name==r[1];});
            need(it!=ports.end()&&!r[2].empty(),"unknown partition pin");it->partitionNode=r[2];
            it->x=std::stod(r[3]);it->y=std::stod(r[4]);need(std::isfinite(it->x)&&std::isfinite(it->y),"nonfinite partition coordinate");
        }
    }
    metadata["static_shell_sha256"]=shell;metadata["corrected_static_cells"]=std::to_string(changed);
    metadata["placement_source"]="verified final static DCP positions";sealed=true;
    if(contract.count("resource_inventory_file"))certifyResources(contract);
}

void StaticDesignCache::certifyResources(const Config &contract) {
    need(sealed,"resource certification requires sealed static context");
    for(const auto &k:{"identity","device","clock_period","static_shell_sha256"})
        need(required(contract,k)==required(metadata,k),std::string("resource certification mismatch ")+k);
    need(!metadata.count("dfx_resource_policy"),"resource policy already certified; create a new capture for changed shell/geometry");
    std::map<std::string,std::set<std::string>> allowed;
    for(const auto &m:modules)allowed[m.first]=m.second.sites;
    const auto inventory=required(contract,"resource_inventory_file"),geometry=required(contract,"resource_geometry_file");
    auto result=BoundarySitePolicy::derive(inventory,geometry,allowed,required(contract,"resource_side_policy"));
    for(const auto &key:{"resource_inventory_sha256","resource_geometry_sha256"})
        need(std::regex_match(required(contract,key),std::regex("[0-9a-f]{64}")),std::string("invalid ")+key);
    for(const auto &m:modules) {
        const auto it=result.prohibited.find(m.first);
        if(it!=result.prohibited.end())baselineBlocked.insert(it->second.begin(),it->second.end());
        std::string mask;for(const auto &site:result.margin[m.first])mask+=site+"\n";
        metadata["dfx_edge_mask/"+m.first]=mask;
    }
    metadata["dfx_resource_policy"]=BoundarySitePolicy::version;
    metadata["dfx_resource_side_policy"]=contract.at("resource_side_policy");
    metadata["dfx_resource_inventory_sha256"]=contract.at("resource_inventory_sha256");
    metadata["dfx_resource_geometry_sha256"]=contract.at("resource_geometry_sha256");
    metadata["dfx_resource_audit"]=result.audit;
}

std::vector<OutputBuffer> StaticDesignCache::normalizeOutputInterfaces(
    const std::string &id,std::vector<Cell> &dynamic,std::vector<Port> &ports) {
    std::set<std::string> names,aliases,outputs,portNames;
    for(const auto &c:dynamic) {
        need(names.insert(c.name).second,"duplicate OOC cell "+c.name);
        for(const auto &p:c.pins) {
            if(!p.alias.empty())aliases.insert(p.alias);
            if(p.direction=="OUT")need(outputs.insert(p.name).second,"duplicate OOC output "+p.name);
        }
    }
    std::map<std::string,std::vector<std::size_t>> groups;
    for(std::size_t i=0;i<ports.size();++i) {
        const auto &p=ports[i];atom(p.name);
        need(p.module==id&&portNames.insert(p.name).second,"port owner/duplicate during interface normalization");
        if(p.direction=="OUT"&&!p.driver.empty())groups[p.driver].push_back(i);
    }
    std::vector<OutputBuffer> plan;
    for(const auto &group:groups) {
        if(group.second.size()<2)continue;
        need(!special(group.first)&&!starts(group.first,"@")&&outputs.count(group.first),
             "shared output must have one represented fabric driver: "+group.first);
        for(auto i:group.second) {
            const auto &p=ports[i];need(!p.clock,"shared clock output requires a dedicated interface policy");
            std::ostringstream encoded;encoded<<std::hex<<std::setfill('0');
            for(unsigned char ch:p.name)encoded<<std::setw(2)<<unsigned(ch);
            const auto suffix=encoded.str(),cell="AMF_DFX_OutputBuffer_"+suffix,net="AMF_DFX_OutputNet_"+suffix;
            need(names.insert(cell).second&&aliases.insert(net).second,"interface buffer name collision: "+p.name);
            plan.push_back({id,p.name,cell,group.first,net});
        }
    }
    // Validate every planned change before mutating the caller's graph.
    for(const auto &b:plan) {
        dynamic.push_back({b.cell,"LUT1","","",{{b.cell+"/I0","I0","IN",b.driver,b.driver},
                          {b.cell+"/O","O","OUT",b.net,b.cell+"/O"}},"",false});
        for(auto &p:ports)if(p.name==b.port)p.driver=b.cell+"/O";
    }
    return plan;
}
PreparedInput StaticDesignCache::compose(const std::string &id,std::vector<Cell> dynamic,const std::vector<Port> &inputPorts,Config &cfg) const {
    need(sealed,"unsealed AMF placement cache cannot drive incremental placement");need(modules.count(id),"unknown module "+id);
    need(required(cfg,"Native static identity")==required(metadata,"identity"),"cache identity mismatch");
    need(required(cfg,"Native static shell sha256")==required(metadata,"static_shell_sha256"),"static shell mismatch");
    need(required(cfg,"device")==required(metadata,"device")&&required(cfg,"ClockPeriod")==required(metadata,"clock_period"),"device/clock mismatch");
    const auto policy=cfg.count("Native DFX edge policy")?cfg.at("Native DFX edge policy"):"off";
    need(policy=="off"||policy==BoundarySitePolicy::version,"unknown Native DFX edge policy");
    std::set<std::string> edgeBlocked;
    if(policy!="off") {
        need(metadata.count("dfx_resource_policy")&&metadata.at("dfx_resource_policy")==policy,
             "edge policy requires final resource certification; old cache is not implicitly upgraded");
        need(metadata.count("dfx_edge_mask/"+id),"missing certified module edge mask");
        std::istringstream mask(metadata.at("dfx_edge_mask/"+id));std::string name;
        while(std::getline(mask,name)) {
            need(modules.at(id).sites.count(name)&&edgeBlocked.insert(name).second,"invalid cached edge site");
        }
    }
    auto newPorts=inputPorts;
    const auto outputBuffers=normalizeOutputInterfaces(id,dynamic,newPorts);
    const auto &m=modules.at(id);std::map<std::string,Port> np;
    for(auto &p:newPorts)need(p.module==id&&np.emplace(p.name,p).second,"new module port owner/duplicate");
    need(np.size()==m.ports.size(),"module port count changed");
    std::map<std::string,std::string> connection;
    for(auto &p:m.ports){need(np.count(p.name)&&np.at(p.name).direction==p.direction&&np.at(p.name).clock==p.clock,"port direction/clock/name changed "+p.name);
        if(p.direction=="IN")connection["@PORT/"+p.name]=p.driver;
        else connection[token(p)]=np.at(p.name).driver;}
    auto qualify=[&](const std::string &s){
        if(starts(s,"@DFX_CLOCK/")&&!starts(s,"@DFX_CLOCK/@"))return "@DFX_CLOCK/"+m.instance+"/"+s.substr(11);
        return s.empty()||special(s)||starts(s,"@")?s:m.instance+"/"+s;
    };
    for(auto &p:newPorts)if(p.direction=="OUT")connection["@RM/"+id+"/"+p.name]=qualify(p.driver);
    auto resolve=[&](std::string s){std::set<std::string> visited;
        while(connection.count(s)){need(visited.insert(s).second,"interface connection cycle");s=connection.at(s);}
        need(!starts(s,"@PORT/")&&!starts(s,"@RM/"),"unresolved/cross-RM connection "+s);return s;};
    std::vector<Cell> graph=cells;metadataFiles(dynamic,cfg);
    std::map<std::string,Port> endpoints;
    for(auto &other:modules)if(other.first!=id)for(auto &p:other.second.ports)if(!p.clock&&!p.driver.empty()) {
        const auto name="@AMF_INTERFACE/"+other.first+"/"+p.name;
        const bool output=p.direction=="OUT";const auto pin=name+(output?"/O":"/I0");
        if(output)connection[token(p)]=pin;
        endpoints[name]=p;
        graph.push_back({name,"AMF_INTERFACE","","",{{pin,output?"O":"I0",p.direction,pin,output?pin:p.driver}},"",false});
    }
    const auto fixedCount=graph.size();
    for(auto &c:dynamic){need(c.type!="GND"&&c.type!="VCC","constants must be normalized");
        c.name=qualify(c.name);need(under(c.name,m.instance),"module name escaped RP");
        for(auto &p:c.pins){p.name=qualify(p.name);p.driver=resolve(qualify(p.driver));p.alias=p.driver;}
        graph.push_back(std::move(c));}
    // Resolve cached boundary drivers to the new module's actual driver pins.
    // Other RM outputs are left symbolic until selection; they are never
    // replaced with stale cells/coordinates or silently dropped.
    for(std::size_t i=0;i<fixedCount;++i)for(auto &p:graph[i].pins)if(connection.count(p.driver))p.driver=resolve(p.driver);
    std::unordered_map<std::string,std::pair<std::size_t,std::size_t>> pinIndex;
    std::unordered_map<std::string,std::vector<std::pair<std::size_t,std::size_t>>> sinks;
    for(std::size_t i=0;i<graph.size();++i)for(std::size_t j=0;j<graph[i].pins.size();++j){auto &p=graph[i].pins[j];
        need(pinIndex.emplace(p.name,std::make_pair(i,j)).second,"duplicate composed pin");
        if(p.direction=="IN"&&!p.driver.empty())sinks[p.driver].push_back({i,j});}
    std::vector<bool> selected(graph.size(),false),seenUp(graph.size(),false),seenDown(graph.size(),false);
    std::unordered_set<std::string> keep;std::queue<std::size_t> up,down;
    auto input=[&](const Pin &p){if(p.driver.empty())return;keep.insert(p.name);if(special(p.driver))return;
        need(pinIndex.count(p.driver),"required interface driver absent "+p.driver);
        auto x=pinIndex.at(p.driver);need(graph[x.first].pins[x.second].direction=="OUT","driver is not output");
        selected[x.first]=true;keep.insert(p.driver);if(x.first<fixedCount)up.push(x.first);};
    auto output=[&](const Pin &p){if(p.driver.empty()||special(p.driver))return;
        need(p.driver==p.name,"output driver differs from pin "+p.name);
        auto it=sinks.find(p.name);if(it==sinks.end())return;
        for(auto x:it->second){selected[x.first]=true;keep.insert(p.name);keep.insert(graph[x.first].pins[x.second].name);if(x.first<fixedCount)down.push(x.first);}};
    for(std::size_t i=fixedCount;i<graph.size();++i){selected[i]=true;for(auto &p:graph[i].pins){keep.insert(p.name);if(p.direction=="IN")input(p);else if(p.direction=="OUT")output(p);else need(false,"INOUT pin");}}
    bool dspCritical=cfg.count("DSPCritical")&&cfg.at("DSPCritical")=="true";
    while(!up.empty()||!down.empty()){
        if(!up.empty()){auto i=up.front();up.pop();if(seenUp[i])continue;seenUp[i]=true;if(terminal(graph[i],dspCritical))continue;
            for(auto &p:graph[i].pins)if(p.direction=="IN")input(p);
        }else{auto i=down.front();down.pop();if(seenDown[i])continue;seenDown[i]=true;if(terminal(graph[i],dspCritical))continue;
            up.push(i);for(auto &p:graph[i].pins)if(p.direction=="OUT")output(p);}}
    auto out=std::filesystem::path(required(cfg,"Native workspace"));need(std::filesystem::create_directory(out),"workspace already exists");
    std::ostringstream net,stat,cellTable,srl,dsp,clock,allow,block,scope,anchors,buffers;
    buffers<<"module\tport\tcell\tinput_driver\toutput_net\tinit\n";
    for(const auto &b:outputBuffers)buffers<<b.module<<'\t'<<b.port<<'\t'<<b.cell<<'\t'<<b.driver<<'\t'<<b.net<<"\t2'h2\n";
    anchors<<"cell\tx\ty\tnode\n";anchors<<std::setprecision(12);
    stat<<"cell\tsite\tbel\n";cellTable<<"cell\tprimitive\tactive\n";
    srl<<"cell\ttype\tclk\tce\tclk_inverted\tloc_fixed\tbel_fixed\tq31_sinks\n";dsp<<"AMF_DSP_REGISTERED_OUTPUTS 1\n";
    PreparedInput result;std::set<std::string> clocks,blocked=baselineBlocked;
    blocked.insert(edgeBlocked.begin(),edgeBlocked.end());
    // Occupancy includes ALL static cells, even cells outside the timing cone.
    for(auto &c:cells){need(!c.site.empty()&&!c.bel.empty(),"unplaced static cell");blocked.insert(c.site);}
    std::vector<std::size_t> order;for(std::size_t i=0;i<graph.size();++i)if(selected[i])order.push_back(i);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return graph[a].name<graph[b].name;});
    for(auto i:order){const auto &c=graph[i];bool active=i>=fixedCount;
        if(active)++result.dynamicCells;
        else if(c.type=="AMF_INTERFACE") {auto &p=endpoints.at(c.name);need(!p.partitionNode.empty(),"missing final partition-pin location "+p.module+"/"+p.name);
            ++result.interfaceEndpoints;anchors<<c.name<<'\t'<<p.x<<'\t'<<p.y<<'\t'<<p.partitionNode<<'\n';}
        else{++result.fixedCells;stat<<c.name<<'\t'<<c.site<<'\t'<<c.bel<<'\n';}
        cellTable<<c.name<<'\t'<<c.type<<'\t'<<active<<'\n';net<<"curCell=> "<<c.name<<" type=> "<<c.type<<'\n';
        for(auto &p:c.pins){bool clk=starts(p.driver,"@DFX_CLOCK/");if(!active&&!keep.count(p.name)&&!clk)continue;
            need(!starts(p.driver,"@RM/")&&!starts(p.driver,"@PORT/"),"selected cone reaches unresolved other RP "+p.driver);
            net<<" pin=> "<<p.name<<" refpin=> "<<p.ref<<" dir=> "<<p.direction<<" net=> "<<p.alias<<" drivepin=> "<<p.driver<<'\n';
            if(clk&&p.direction=="IN")clocks.insert(p.driver);
            ++result.retainedPins;}
        if(!c.srlMetadata.empty())srl<<c.name<<'\t'<<c.srlMetadata<<'\n';
        bool usedDSP=false;for(auto &p:c.pins)if(p.direction=="OUT"&&keep.count(p.name)&&sinks.count(p.name))usedDSP=true;
        if(c.registeredDSP&&usedDSP)dsp<<c.name<<" PREG=1\n";
    }
    for(auto &s:m.sites)allow<<s<<'\n';
    for(auto &s:blocked)block<<s<<'\n';
    for(auto &s:clocks)clock<<s<<'\n';
    scope<<"schema\tnative-static-cache-v2\nactive_cells\t"<<result.dynamicCells<<"\nfixed_interface_cells\t"<<result.fixedCells<<"\nretained_pins\t"<<result.retainedPins<<"\nstatic_cells_in_cache\t"<<cells.size()<<"\nstatic_dcp_opened\t0\n";
    if(metadata.count("dfx_resource_policy")) {
        scope<<"dfx_edge_policy\t"<<policy<<"\nedge_blocked_sites\t"<<edgeBlocked.size()<<'\n';
        checkedText(out/"edge_policy_audit.tsv",metadata.at("dfx_resource_audit"));
    }
    scope<<"interface_output_buffers\t"<<outputBuffers.size()<<'\n';
    checkedText(out/"interface_buffers.tsv",buffers.str());
    checkedText(out/"static_placement.tsv",stat.str());checkedText(out/"cells.tsv",cellTable.str());
    checkedText(out/"interface_endpoints.tsv",anchors.str());
    checkedText(out/"srl_controls.tsv",srl.str());checkedText(out/"dsp_registered_outputs.txt",dsp.str());
    checkedText(out/"clocks",clock.str());checkedText(out/"allowed_sites.txt",allow.str());checkedText(out/"blocked_sites.txt",block.str());checkedText(out/"input_scope.tsv",scope.str());
    cfg["DFX active partition"]=m.instance;cfg["DFX static placement file"]=(out/"static_placement.tsv").string();
    cfg["Native interface endpoints file"]=(out/"interface_endpoints.tsv").string();
    cfg["DFX allowed sites file"]=(out/"allowed_sites.txt").string();cfg["DFX blocked sites file"]=(out/"blocked_sites.txt").string();
    cfg["clock file"]=(out/"clocks").string();cfg["SRL control metadata file"]=(out/"srl_controls.tsv").string();
    cfg["DSP registered outputs file"]=(out/"dsp_registered_outputs.txt").string();cfg["DFX input scope"]="rm-boundary-cone-v1";
    cfg["DFX placement mode"]="incremental";cfg["IncremenralCompilation"]="true";
    result.netlist=net.str();return result;
}
PreparedInput StaticDesignCache::prepare(Config &cfg) {
    auto cache=load(required(cfg,"Native static cache read"));
    std::ifstream net(required(cfg,"Native module netlist"));need(bool(net),"module netlist missing");
    auto cells=readNetlist(net);auto ports=readPorts(required(cfg,"Native module ports file"));
    return cache.compose(required(cfg,"Native active module"),std::move(cells),ports,cfg);
}
}
