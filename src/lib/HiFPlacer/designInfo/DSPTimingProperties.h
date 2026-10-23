#pragma once
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dsp_timing {
using Outputs=std::map<std::string,std::vector<std::string>>;
struct Properties { std::array<int,9> registers; int preg() const {return registers[0];} };
struct Metadata {std::string dcpHash,netlistHash; std::map<std::string,Properties> cells;};
inline void need(bool ok,const std::string &message) {if(!ok)throw std::runtime_error("DSP timing: "+message);}
inline bool hashValid(const std::string &s) {return s.size()==64 && s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::string fileHash(const std::string &path) {
    std::string quoted="'";
    for(char c:path)quoted+=(c=='\''?"'\\''":std::string(1,c));
    quoted+="'";
    FILE *pipe=popen(("sha256sum < "+quoted).c_str(),"r");
    need(pipe!=nullptr,"cannot start SHA-256 reader");
    char buffer[128]={};const bool read=fgets(buffer,sizeof(buffer),pipe)!=nullptr;
    const int status=pclose(pipe);std::istringstream row(buffer);std::string hash;row>>hash;
    need(read && status==0 && hashValid(hash),"cannot hash input "+path);return hash;
}
inline Metadata read(std::istream &input,const Outputs &outputs) {
    Metadata result;std::string line,key,value,extra;
    need(bool(std::getline(input,line)) && line=="AMF_DSP_TIMING 1","invalid schema");
    for(auto field:{"source_dcp_sha256","netlist_sha256"}) {
        need(bool(std::getline(input,line)),"missing provenance");std::istringstream row(line);
        need(bool(row>>key>>value) && key==field && hashValid(value) && !(row>>extra),"invalid provenance");
        (key=="source_dcp_sha256"?result.dcpHash:result.netlistHash)=value;
    }
    need(bool(std::getline(input,line)) && line=="cell\tPREG\tAREG\tACASCREG\tBREG\tBCASCREG\tMREG\tCREG\tDREG\tADREG","invalid columns");
    while(std::getline(input,line)) {
        std::istringstream row(line);std::string name;Properties p;
        need(bool(row>>name),"empty row");
        for(size_t i=0;i<p.registers.size();++i) {
            need(bool(row>>p.registers[i]),"missing register property: "+name);
            const int maximum=(i>=1 && i<=4)?2:1;
            need(p.registers[i]>=0 && p.registers[i]<=maximum,"invalid register property: "+name);
        }
        need(!(row>>extra),"extra property: "+name);
        const auto found=outputs.find(name);need(found!=outputs.end(),"unknown DSP: "+name);
        // Cell-level STA can exactly express the PREG output boundary only
        // when every used output is a P bit. Reject bypass/mixed outputs.
        // PREG=0 preserves the existing combinational cell approximation;
        // input/intermediate pipeline stages are recorded, not newly modeled.
        if(p.preg()==1) for(const auto &pin:found->second) {
            need(pin.size()>=4 && pin.substr(0,2)=="P[" && pin.back()==']',"PREG=1 has unsupported/mixed output: "+name+"/"+pin);
            auto index=pin.substr(2,pin.size()-3);
            need(!index.empty() && index.size()<=2 && index.find_first_not_of("0123456789")==std::string::npos && std::stoi(index)<48,"invalid P output: "+pin);
        }
        need(result.cells.emplace(name,p).second,"duplicate DSP: "+name);
    }
    need(result.cells.size()==outputs.size(),"metadata must cover every DSP, including PREG=0");
    return result;
}
}
