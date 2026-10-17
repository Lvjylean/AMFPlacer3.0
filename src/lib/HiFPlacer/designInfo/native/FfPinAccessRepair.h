#ifndef AMF_NATIVE_FF_PIN_ACCESS_REPAIR_H
#define AMF_NATIVE_FF_PIN_ACCESS_REPAIR_H
#include "StaticDesignCache.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace amf_native {
// Explicit recovery of already packed FDREs. No global placement, no coordinate
// constants, and no change to the normal placer. A candidate stays in the same
// SLICE half. Its CE bank must be the source bank or completely empty. The live
// backend independently certifies the full cell set, controls and pin access.
struct FfPinAccessRepair {
    using Table = std::map<std::string,std::string>;
    struct Result {Table placement; Table from,to;};
    static void need(bool ok,const std::string &why) {
        if(!ok)throw std::runtime_error("Native FF pin repair: "+why);
    }
    static std::vector<std::string> fields(const std::string &s) {
        std::istringstream in(s);std::string f;std::vector<std::string> out;
        while(std::getline(in,f,'\t'))out.push_back(f);return out;
    }
    static std::vector<std::string> lines(const std::string &p) {
        std::ifstream f(p);need(bool(f),"cannot read "+p);std::vector<std::string> out;std::string s;
        while(std::getline(f,s)){need(s.find('\r')==std::string::npos,"CR in input");if(!s.empty())out.push_back(s);}return out;
    }
    static Table table(const std::string &p) {
        Table t;for(const auto &s:lines(p)){auto f=fields(s);need(f.size()==2&&!f[0].empty()&&!f[1].empty()&&t.emplace(f[0],f[1]).second,"bad/duplicate table row");}return t;
    }
    static std::string site(const std::string &p){return p.substr(0,p.find('/'));}
    static std::string bel(const std::string &p){auto i=p.find('/');need(i!=std::string::npos,"missing BEL");return p.substr(i+1);}
    static bool ff(const std::string &b){return std::regex_match(b,std::regex("[A-H]FF2?"));}
    static int half(const std::string &b){need(ff(b),"not an FF BEL");return (b[0]-'A')/4;}
    static int bank(const std::string &b){return half(b)*2+(b.size()==4);}
    static bool hard(const std::string &s){return s=="ALL_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA";}
    static Result plan(const Table &base,const Table &types,const std::set<std::string> &allowed,
                       const std::set<std::string> &blocked,const Table &access) {
        need(base.size()==types.size()&&!base.empty(),"active coverage mismatch");
        std::set<std::string> occupied;for(const auto &p:base){need(types.count(p.first),"missing cell type");need(occupied.insert(p.second).second,"BEL collision in input");}
        Result r{base,{},{}};
        for(const auto &p:base){auto a=access.find(p.second);if(a==access.end()||!hard(a->second))continue;
            const auto s=site(p.second),b=bel(p.second);
            need(types.at(p.first)=="FDRE"&&ff(b),"only standalone FDRE slots supported by this minimal recovery");
            need(allowed.count(s)&&!blocked.count(s),"source site outside allowed region");
            // Require complete extracted Q access for this entire site, never
            // treat an absent row as a free slot.
            for(char c='A';c<='H';++c)for(const auto &suffix:{std::string("FF"),std::string("FF2")})
                need(access.count(s+"/"+c+suffix),"incomplete site FF access inventory");
            std::vector<std::tuple<int,int,std::string>> candidates;
            for(const auto &q:access){if(site(q.first)!=s||occupied.count(q.first)||q.second!="NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP")continue;
                const auto dst=bel(q.first);if(!ff(dst)||half(dst)!=half(b))continue;
                bool ceBankEmpty=true;
                for(const auto &live:r.placement)if(site(live.second)==s&&ff(bel(live.second))&&bank(bel(live.second))==bank(dst))ceBankEmpty=false;
                if(bank(dst)!=bank(b)&&!ceBankEmpty)continue;
                candidates.emplace_back(bank(dst)!=bank(b),std::abs(dst[0]-b[0]),q.first);
            }
            need(!candidates.empty(),"no certified empty FF slot in the same SLICE half; no cross-site fallback");
            std::sort(candidates.begin(),candidates.end());const auto dst=std::get<2>(candidates.front());
            occupied.erase(p.second);occupied.insert(dst);r.placement.at(p.first)=dst;r.from[p.first]=p.second;r.to[p.first]=dst;
        }
        for(const auto &p:r.placement){auto a=access.find(p.second);need(a==access.end()||!hard(a->second),"restricted FF remains");}
        return r;
    }
    static void run(const Config &cfg) {
        const auto start=std::chrono::steady_clock::now();
        need(cfg.at("Native FF pin repair enabled")=="true","explicit opt-in required");
        const auto &rp=cfg.at("DFX active partition");need(!rp.empty()&&rp.back()!='/',"invalid partition");
        auto cert=lines(cfg.at("Native FF pin access file"));need(cert.size()>=6&&cert[0]=="AMF_FF_Q_ACCESS\t1","certificate schema");
        for(int i=1;i<=3;++i){auto f=fields(cert[i]);need(f.size()==2,"certificate identity row");
            const std::string keys[]={"", "Native repair source checkpoint sha256", "Native static shell sha256", "DFX active partition"};
            const std::string names[]={"", "source_checkpoint_sha256", "static_shell_sha256", "partition"};
            need(f[0]==names[i]&&f[1]==cfg.at(keys[i]),"certificate identity mismatch");
            if(i<3)need(std::regex_match(f[1],std::regex("[0-9a-f]{64}")),"invalid identity hash");}
        need(cert[4]=="site\tbel\tstate","certificate table schema");
        Table access;const std::set<std::string> states={"ALL_MAPPED_SITE_PIN_OPTIONS_BLOCKED_FOR_GENERAL_DATA","NO_FIXED_STATIC_BLOCKAGE_AT_FIRST_HOP","PARTIAL_STATIC_FIRST_HOP_BLOCKAGE","BOUNDARY_NET_DEPENDENT","NONFIXED_ROUTE_POLICY_DEPENDENT"};
        for(std::size_t i=5;i<cert.size();++i){auto f=fields(cert[i]);need(f.size()==3&&std::regex_match(f[0],std::regex("SLICE_X[0-9]+Y[0-9]+"))&&ff(f[1])&&states.count(f[2]),"invalid certificate row");need(access.emplace(f[0]+"/"+f[1],f[2]).second,"duplicate access row");}
        auto base=table(cfg.at("Native repair input placement"));Table types;
        auto cells=lines(cfg.at("Native repair cells file"));need(cells.size()>1&&cells[0]=="cell\tprimitive\tactive","cell schema");
        for(std::size_t i=1;i<cells.size();++i){auto f=fields(cells[i]);need(f.size()==3&&(f[2]=="0"||f[2]=="1"),"cell row");if(f[2]=="1"){need(f[0].compare(0,rp.size()+1,rp+"/")==0,"wrong module cell");need(types.emplace(f[0],f[1]).second,"duplicate active cell");}}
        std::set<std::string> allowed,blocked;for(auto &s:lines(cfg.at("Native repair allowed sites")))allowed.insert(s);for(auto &s:lines(cfg.at("Native repair blocked sites")))blocked.insert(s);
        auto result=plan(base,types,allowed,blocked,access);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();need(seconds<=180.0,"repair exceeded 180-second ceiling");
        namespace fs=std::filesystem;const auto out=fs::path(cfg.at("Native repair output"));need(fs::create_directory(out),"output must be new");
        std::ofstream placement(out/"placement.tsv"),delta(out/"placement_delta.tsv"),checks(out/"verification.tsv");
        for(const auto &p:result.placement)placement<<p.first<<'\t'<<p.second<<'\n';
        delta<<"cell\tfrom\tto\n";for(const auto &p:result.from)delta<<p.first<<'\t'<<p.second<<'\t'<<result.to.at(p.first)<<'\n';
        checks<<"checked_cells\t"<<base.size()<<"\nchanged_cells\t"<<result.from.size()<<"\nrepair_seconds\t"<<seconds<<"\nsame_site_only\t1\nsame_slice_half_only\t1\nlive_backend_validation_required\t1\nfull_routing_verified\t0\n";
        placement.close();delta.close();checks.close();need(bool(placement)&&bool(delta)&&bool(checks),"output write failed");
    }
};
}
#endif
