#ifndef AMF_LUT_PIN_ASSIGNMENT_H
#define AMF_LUT_PIN_ASSIGNMENT_H
#include "LocalPinAccess.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>

namespace amf_native {
// Shared LUT inputs belong to a physical row, not independently to its 5LUT
// and 6LUT BELs. Preserve logical pin multiplicity within each LUT while
// allowing the same net on the two LUTs to share a physical input.
class LutPinAssignment {
public:
    struct Input {
        unsigned cell=0;
        std::string logical;
        LocalPinAccess::Demand demand;
        std::map<unsigned,std::vector<std::vector<unsigned>>> physical;
        unsigned row=0;
        bool checkEntrance=true;
    };
    struct Result {
        bool feasible=false;
        std::map<unsigned,std::map<std::string,unsigned>> lockPins;
        LocalPinAccess::Solution entranceAssignment;
        std::size_t explored=0;
    };
    static Result solve(const LocalPinAccess &access,const std::vector<Input> &inputs,
                        const std::vector<LocalPinAccess::Demand> &other={},std::size_t limit=100000) {
        static const char *diagnostics=std::getenv("AMF_LOCAL_ACCESS_DIAGNOSTICS_DIR");
        if(!diagnostics)return solveImpl(access,inputs,other,limit);
        const auto started=std::chrono::steady_clock::now();
        auto result=solveImpl(access,inputs,other,limit);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        if(seconds>=.001) {
            static std::atomic<unsigned> sequence{0};unsigned id=sequence.fetch_add(1);
            if(id<16) {
                std::ofstream f(std::string(diagnostics)+"/local-lut-case-"+std::to_string(id)+".tsv");
                auto hex=[](const std::string&s){std::string out;const char*d="0123456789abcdef";for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;};
                auto ports=[&](const std::set<std::string>&ps){std::string out;for(const auto&p:ps){if(!out.empty())out+=',';out+=hex(p);}return out;};
                std::set<unsigned> used;
                auto paths=[&](const std::vector<std::vector<unsigned>>&ps){bool first=true;for(const auto&p:ps){if(!first)f<<';';first=false;for(unsigned k=0;k<p.size();++k){if(k)f<<',';f<<p[k];used.insert(p[k]);}}};
                auto demand=[&](const LocalPinAccess::Demand&d){f<<hex(d.pin)<<'\t'<<hex(d.net)<<'\t'<<d.constant<<'\t'<<ports(d.ports)<<'\t';};
                f<<"M\t"<<seconds<<'\t'<<result.feasible<<'\t'<<result.explored<<'\n';
                for(unsigned i=0;i<other.size();++i){f<<"O\t"<<i<<'\t';demand(other[i]);paths(other[i].entrances);f<<'\n';}
                for(unsigned i=0;i<inputs.size();++i){const auto&in=inputs[i];
                    for(const auto&p:in.physical){f<<"I\t"<<i<<'\t'<<in.cell<<'\t'<<in.row<<'\t'<<in.checkEntrance<<'\t'<<hex(in.logical)<<'\t';demand(in.demand);f<<p.first<<'\t';paths(p.second);f<<'\n';}}
                for(auto r:used){const auto&v=access.resources[r];f<<"R\t"<<r<<'\t'<<v.occupied<<'\t'<<v.bad<<'\t'<<v.tie<<'\t'<<v.ownerConstant<<'\t'<<ports(v.ownerPorts)<<'\n';}
                if(!f.good())throw std::runtime_error("Local LUT diagnostic write failed");
            }
        }
        return result;
    }
    static Result solveImpl(const LocalPinAccess &access,const std::vector<Input> &inputs,
                            const std::vector<LocalPinAccess::Demand> &other,std::size_t limit) {
        Result result;
        std::vector<Input> candidates=inputs;
        std::vector<bool> assigned(inputs.size(),false);
        std::map<unsigned,unsigned> usedByCell;
        std::map<unsigned,std::array<std::pair<std::string,unsigned>,7>> shared;
        std::vector<LocalPinAccess::Demand> demands=other;
        // An independently blocked output cannot be repaired by permuting
        // LUT inputs. Reject it before exploring otherwise equivalent maps.
        if(!access.solve(other,limit).feasible)return result;
        for(auto &input:candidates) {
            for(const auto &p:input.physical)
                if(p.first<1||p.first>6)throw std::runtime_error("LUT physical input outside A1..A6");
            if(input.checkEntrance)for(auto it=input.physical.begin();it!=input.physical.end();) {
                auto &paths=it->second;
                paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const std::vector<unsigned>&path){
                    if(path.empty())throw std::runtime_error("Empty LUT entrance path");
                    return !std::all_of(path.begin(),path.end(),[&](unsigned r){return access.available(r,input.demand);});
                }),paths.end());
                if(paths.empty())it=input.physical.erase(it);else ++it;
            }
            if(input.physical.empty())return result;
        }
        auto usable=[&](const Input &input,unsigned pin) {
            if(usedByCell[input.cell]&(1U<<pin))return false;
            const auto &entry=shared[input.row][pin];
            return !entry.second||entry.first==input.demand.net;
        };
        std::function<bool(unsigned)> visit=[&](unsigned remaining) {
            if(++result.explored>limit)throw std::runtime_error("LUT pin-assignment budget exhausted; feasibility unresolved");
            if(!remaining) {
                result.entranceAssignment=access.solve(demands,limit);
                return result.entranceAssignment.feasible;
            }
            // Check all unresolved inputs, not only the already chosen prefix.
            // Unioning their legal physical alternatives is a relaxation: an
            // impossible union proves failure, while success is still checked
            // against exact per-cell and shared-row pin choices below.
            auto relaxed=demands;
            unsigned selected=candidates.size(),smallest=7;
            std::map<unsigned,std::vector<unsigned>> domainsByCell;
            for(unsigned i=0;i<candidates.size();++i)if(!assigned[i]) {
                const auto &in=candidates[i];unsigned count=0,mask=0;auto demand=in.demand;demand.entrances.clear();
                for(const auto &p:in.physical)if(usable(in,p.first)) {
                    ++count;mask|=1U<<p.first;demand.entrances.insert(demand.entrances.end(),p.second.begin(),p.second.end());
                }
                if(!count)return false;
                domainsByCell[in.cell].push_back(mask);
                if(count<smallest){selected=i;smallest=count;}
                if(in.checkEntrance)relaxed.push_back(std::move(demand));
            }
            // Hall feasibility for each LUT's at-most-six distinct pins. This
            // catches a blocked later row before permuting independent rows.
            for(const auto &cell:domainsByCell) {
                std::array<int,7> owner;owner.fill(-1);
                std::function<bool(unsigned,unsigned&)> augment=[&](unsigned i,unsigned &seen){
                    for(unsigned p=1;p<=6;++p)if((cell.second[i]&(1U<<p))&&!(seen&(1U<<p))) {
                        seen|=1U<<p;
                        if(owner[p]<0||augment(owner[p],seen)){owner[p]=i;return true;}
                    }
                    return false;
                };
                for(unsigned i=0;i<cell.second.size();++i){unsigned seen=0;if(!augment(i,seen))return false;}
            }
            if(!access.solve(relaxed,limit).feasible)return false;
            const auto &input=candidates[selected];assigned[selected]=true;
            for(const auto &p:input.physical) {
                unsigned bit=1U<<p.first;
                if(!usable(input,p.first))continue;
                auto &entry=shared[input.row][p.first];
                auto demand=input.demand;demand.entrances=p.second;
                entry.first=demand.net;++entry.second;usedByCell[input.cell]|=bit;
                result.lockPins[input.cell][input.logical]=p.first;
                if(input.checkEntrance)demands.push_back(std::move(demand));
                if(visit(remaining-1))return true;
                if(input.checkEntrance)demands.pop_back();result.lockPins[input.cell].erase(input.logical);
                usedByCell[input.cell]&=~bit;--entry.second;
            }
            assigned[selected]=false;
            return false;
        };
        result.feasible=visit(candidates.size());
        if(!result.feasible)result.lockPins.clear();
        return result;
    }
};
}
#endif
