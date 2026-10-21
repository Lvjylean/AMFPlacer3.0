#ifndef AMF_LOCAL_ACCESS_DATABASE_H
#define AMF_LOCAL_ACCESS_DATABASE_H
#include "LocalPinAccess.h"
#include <sstream>
#include <tuple>
#include <zlib.h>
#include <array>

namespace amf_native {
class LocalAccessDatabase {
public:
    static constexpr const char *version="fixed-static-local-entrance-v2";
    using Paths=std::vector<std::vector<unsigned>>;
    LocalPinAccess access;
    std::set<std::string> sites;
    std::map<std::tuple<std::string,std::string,std::string>,Paths> pins;
    std::map<std::string,std::set<std::string>> aliases;
    static std::vector<std::string> split(const std::string &s,char delimiter) {
        std::vector<std::string> v;std::size_t start=0;
        for(;;){auto p=s.find(delimiter,start);v.push_back(s.substr(start,p-start));if(p==s.npos)break;start=p+1;}
        return v;
    }
    static unsigned integer(const std::string &s) {
        if(s.empty()||s.find_first_not_of("0123456789")!=s.npos)throw std::runtime_error("Invalid local access integer");
        auto value=std::stoull(s);if(value>10000000)throw std::runtime_error("Local access integer outside bound");return value;
    }
    static std::string decodeBlob(const std::string &bytes) {
        if(bytes.size()<2||static_cast<unsigned char>(bytes[0])!=0x1f||static_cast<unsigned char>(bytes[1])!=0x8b)return bytes;
        if(bytes.size()>64*1024*1024)throw std::runtime_error("Compressed policy exceeds bound");
        z_stream z{};z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));z.avail_in=bytes.size();
        if(inflateInit2(&z,16+MAX_WBITS)!=Z_OK)throw std::runtime_error("Policy decompressor init");
        std::array<char,1024*1024> block;std::string out;int code=Z_OK;
        while(code==Z_OK){z.next_out=reinterpret_cast<Bytef*>(block.data());z.avail_out=block.size();code=inflate(&z,Z_NO_FLUSH);
            auto n=block.size()-z.avail_out;
            if(out.size()+n>256ULL*1024*1024){inflateEnd(&z);throw std::runtime_error("Decoded policy exceeds bound");}
            out.append(block.data(),n);}
        const bool complete=code==Z_STREAM_END&&z.avail_in==0;inflateEnd(&z);
        if(!complete)throw std::runtime_error("Invalid or trailing compressed policy data");
        return out;
    }
    void load(const std::string &text,const std::string &module,const std::string &portAliases) {
        if(!sites.empty()||!access.resources.empty())throw std::runtime_error("Local access database already loaded");
        std::istringstream f(text);std::string row;
        if(!std::getline(f,row)||row!="AMF_LOCAL_ACCESS\t2")throw std::runtime_error("Unknown local access schema");
        while(std::getline(f,row)) {
            auto v=split(row,'\t');
            if(v[0]=="N") {
                if(v.size()!=8||integer(v[1])!=access.resources.size())throw std::runtime_error("Local access resource order");
                LocalPinAccess::Resource r;r.name=v[2];
                if((v[3]!="-1"&&v[3]!="0"&&v[3]!="1")||(v[6]!="-1"&&v[6]!="0"&&v[6]!="1"))throw std::runtime_error("Invalid constant polarity");
                r.tie=std::stoi(v[3]);r.bad=integer(v[4]);r.occupied=integer(v[5]);r.ownerConstant=std::stoi(v[6]);
                if(!v[7].empty())for(auto &p:split(v[7],'|'))r.ownerPorts.insert(p);
                access.resources.push_back(std::move(r));
            } else if(v[0]=="S") {
                if(v.size()!=3)throw std::runtime_error("Malformed local access site");
                if(v[1]==module&&!sites.insert(v[2]).second)throw std::runtime_error("Duplicate local access site");
            } else if(v[0]=="P") {
                if(v.size()!=7)throw std::runtime_error("Malformed local access physical pin");
                if(v[1]!=module)continue;
                if(!sites.count(v[2])||(v[5]!="IN"&&v[5]!="OUT"))throw std::runtime_error("Local access pin outside scope");
                Paths paths;
                if(!v[6].empty())for(const auto &p:split(v[6],';')) {
                    auto e=split(p,',');if(e.size()!=2)throw std::runtime_error("Local entrance must be exactly one hop");
                    unsigned a=integer(e[0]),b=integer(e[1]);
                    if(a>=access.resources.size()||b>=access.resources.size())throw std::runtime_error("Unknown entrance resource");
                    paths.push_back({a,b});
                }
                if(!pins.emplace(std::make_tuple(v[2],v[3],v[4]),std::move(paths)).second)throw std::runtime_error("Duplicate local access physical pin");
            } else throw std::runtime_error("Unknown local access record");
        }
        if(sites.empty())throw std::runtime_error("Empty local access module scope");
        std::istringstream a(portAliases);
        while(std::getline(a,row)) {
            auto v=split(row,'\t');if(v.size()!=2||v[0].empty()||v[1].empty())throw std::runtime_error("Malformed stable port alias");
            aliases[v[0]].insert(v[1]);
        }
    }
    const Paths *lookup(const std::string &site,const std::string &bel,const std::string &pin) const {
        if(!sites.count(site))return nullptr;
        auto it=pins.find(std::make_tuple(site,bel,pin));
        if(it==pins.end())throw std::runtime_error("Unmodeled local access pin: "+site+"/"+bel+"/"+pin);
        return &it->second;
    }
    LocalPinAccess::Demand demand(const std::string &pin,const std::string &net,const Paths &paths) const {
        LocalPinAccess::Demand d;d.pin=pin;d.net=net;d.entrances=paths;
        d.constant=net=="<const0>"?0:(net=="<const1>"?1:-1);
        auto it=aliases.find(net);if(it!=aliases.end())d.ports=it->second;
        return d;
    }
};
}
#endif
