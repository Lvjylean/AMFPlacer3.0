#ifndef AMF_NATIVE_PLACEMENT_REPAIR_H
#define AMF_NATIVE_PLACEMENT_REPAIR_H
#include "StaticDesignCache.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace amf_native {
// Recovery of a certified placement, not a second global placement run.
// The physical pin-access certificate is scoped to the diagnosed input group.
// Full device routability is still certified by the joint Vivado backend.
struct PlacementRepair {
    using Table=std::map<std::string,std::string>;
    struct Access {std::string cell,site,pin,net,type;std::set<std::string> resources;};
    using AccessMap=std::map<std::string,Access>;
    struct Result {Table placement;std::size_t originalMatched=0,repairedMatched=0;};
    static std::vector<std::string> fields(const std::string &s) {
        std::vector<std::string> v;std::istringstream f(s);std::string x;
        while(std::getline(f,x,'\t'))v.push_back(x);
        return v;
    }
    static void require(bool ok,const std::string &why) {if(!ok)throw std::runtime_error("Native placement repair: "+why);}
    static std::vector<std::string> lines(const std::string &p) {
        std::ifstream f(p);require(bool(f),"cannot open "+p);std::vector<std::string> v;std::string s;
        while(std::getline(f,s)){require(s.find('\r')==std::string::npos,"CR in input");if(!s.empty())v.push_back(s);}return v;
    }
    static Table table(const std::string &p) {
        Table out;for(const auto &s:lines(p)){auto v=fields(s);require(v.size()==2&&out.emplace(v[0],v[1]).second,"invalid/duplicate placement row");}
        require(!out.empty(),"empty placement");return out;
    }
    static std::size_t match(const AccessMap &pins) {
        // One physical mux can carry one net value, including a shared GND.
        // For repeated pins of the same net use their intersection; this is a
        // conservative sufficient test, never a claim of global routability.
        std::map<std::string,std::set<std::string>> choices;
        for(const auto &kv:pins){const auto &a=kv.second;auto n=choices.find(a.net);
            if(n==choices.end())choices.emplace(a.net,a.resources);
            else {std::set<std::string> both;std::set_intersection(n->second.begin(),n->second.end(),a.resources.begin(),a.resources.end(),std::inserter(both,both.end()));n->second=both;}}
        std::map<std::string,std::string> owner;
        std::function<bool(const std::string&,std::set<std::string>&)> visit=[&](const std::string &net,std::set<std::string> &seen){
            for(const auto &r:choices.at(net))if(seen.insert(r).second){auto it=owner.find(r);
                if(it==owner.end()||visit(it->second,seen)){owner[r]=net;return true;}}
            return false;};
        std::size_t count=0;for(const auto &n:choices){std::set<std::string> seen;if(visit(n.first,seen))++count;}return count;
    }
    static Result plan(const Table &base,const Table &types,const std::set<std::string> &allowed,
                       const std::set<std::string> &blocked,const std::set<std::string> &cascade,
                       const std::string &cell,const std::string &from,const std::string &to,
                       const AccessMap &original,const AccessMap &repaired) {
        require(base.size()==types.size(),"active coverage mismatch");
        for(const auto &kv:base)require(types.count(kv.first),"unknown active cell");
        require(types.count(cell)&&types.at(cell)=="DSP48E2","only a DSP48E2 may be repaired");
        require(base.at(cell)==from&&from!=to,"stale origin or no coordinate change");
        require(from.find("DSP48E2_")==0&&to.find("DSP48E2_")==0&&
                from.substr(from.find('/'))=="/DSP_ALU"&&to.substr(to.find('/'))=="/DSP_ALU","DSP BEL/type changed");
        require(!cascade.count(cell),"dedicated cascade member cannot move independently");
        auto target=to.substr(0,to.find('/'));
        require(allowed.count(target)&&!blocked.count(target),"target outside module or static occupied");
        for(const auto &kv:base)if(kv.first!=cell)require(kv.second.substr(0,kv.second.find('/'))!=target,"target occupied by another active cell");
        require(!original.empty()&&original.size()==repaired.size(),"missing pin-access evidence");
        std::set<std::string> nets;bool movingSeen=false;
        for(const auto &kv:original){auto n=repaired.find(kv.first);require(n!=repaired.end(),"changed certificate pin set");
            const auto &a=kv.second;const auto &b=n->second;
            require(a.cell==b.cell&&a.pin==b.pin&&a.net==b.net&&a.type==b.type,"pin/net semantics changed");
            require(a.type=="SIGNAL"||a.type=="GROUND","unsupported pin-access net type");
            require(base.count(a.cell),"certificate cell missing from active placement");
            require(base.at(a.cell).substr(0,base.at(a.cell).find('/'))==a.site,"certificate source site mismatch");
            require(b.site==(a.cell==cell?target:a.site),"certificate changes another site");
            require(!a.resources.empty()&&!b.resources.empty(),"empty pin-access choices");
            for(const auto *v:{&a,&b})for(const auto &r:v->resources)require(r.find("/VCC_WIRE")==std::string::npos,"VCC cannot carry signal or GND");
            nets.insert(a.net);movingSeen|=a.cell==cell;
        }
        require(movingSeen,"certificate does not cover moving DSP");
        Result result{base,match(original),match(repaired)};
        require(result.originalMatched<nets.size(),"original access group did not reproduce deficit");
        require(result.repairedMatched==nets.size(),"candidate retains pin-access deficit");
        result.placement.at(cell)=to;return result;
    }
    static void run(const Config &cfg,const StaticDesignCache &cache) {
        require(cache.isSealed(),"unsealed static cache");
        for(const auto &kv:Table{{"identity","Native static identity"},{"device","device"},{"clock_period","ClockPeriod"},{"static_shell_sha256","Native static shell sha256"}})
            require(cache.identity().at(kv.first)==cfg.at(kv.second),"static cache identity mismatch: "+kv.first);
        const auto &mod=cache.moduleTable().at(cfg.at("Native active module"));
        auto base=table(cfg.at("Native repair input placement"));Table types;
        auto cs=lines(cfg.at("Native repair cells file"));require(cs.at(0)=="cell\tprimitive\tactive","cell schema mismatch");
        for(std::size_t i=1;i<cs.size();++i){auto v=fields(cs[i]);require(v.size()==3&&(v[2]=="0"||v[2]=="1"),"invalid cell metadata");
            if(v[2]=="1"){require(v[0].compare(0,mod.instance.size()+1,mod.instance+"/")==0,"active cell belongs to other RP");require(types.emplace(v[0],v[1]).second,"duplicate active cell");}}
        std::set<std::string> blocked,cascade;
        for(const auto &s:lines(cfg.at("Native repair blocked sites")))blocked.insert(s);
        for(const auto &c:cache.staticCells())blocked.insert(c.site);
        for(const auto &s:lines(cfg.at("Native repair cascades"))){auto v=fields(s);require(v.size()==3,"invalid cascade row");cascade.insert(v[0]);cascade.insert(v[1]);}
        std::map<std::string,AccessMap> scenarios;
        auto es=lines(cfg.at("Native repair pin access"));require(es.at(0)=="scenario\tcell\tsite\tsite_pin\tnet\ttype\tresource","pin-access schema mismatch");
        for(std::size_t i=1;i<es.size();++i){auto v=fields(es[i]);require(v.size()==7&&(v[0]=="original"||v[0]=="repaired"),"invalid pin-access row");
            const auto key=v[1]+"/"+v[3];auto &map=scenarios[v[0]];auto it=map.find(key);
            if(it==map.end())it=map.emplace(key,Access{v[1],v[2],v[3],v[4],v[5],{}}).first;
            auto &a=it->second;require(a.site==v[2]&&a.net==v[4]&&a.type==v[5]&&a.resources.insert(v[6]).second,"inconsistent/duplicate access row");}
        const auto &cell=cfg.at("Native repair cell"),&from=cfg.at("Native repair from"),&to=cfg.at("Native repair to");
        auto result=plan(base,types,mod.sites,blocked,cascade,cell,from,to,scenarios.at("original"),scenarios.at("repaired"));
        namespace fs=std::filesystem;auto out=fs::path(cfg.at("Native repair output"));require(fs::create_directory(out),"output must be new");
        std::ofstream placement(out/"placement.tsv"),delta(out/"placement_delta.tsv"),checks(out/"verification.tsv");
        for(const auto &kv:result.placement)placement<<kv.first<<'\t'<<kv.second<<'\n';
        delta<<"cell\tfrom\tto\n"<<cell<<'\t'<<from<<'\t'<<to<<'\n';
        checks<<"changed_cells\t1\nunchanged_cells\t"<<base.size()-1<<"\noriginal_entry_matching\t"<<result.originalMatched
              <<"\nrepaired_entry_matching\t"<<result.repairedMatched<<"\ndedicated_cascade_member\t0\nfull_routing_verified\t0\n";
        placement.close();delta.close();checks.close();require(bool(placement)&&bool(delta)&&bool(checks),"output write failed");
    }
};
}
#endif
