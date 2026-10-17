#ifndef AMF_HARD_REGION_CONSTRAINTS_H
#define AMF_HARD_REGION_CONSTRAINTS_H
#include "IncrementalContext.h"
#include "../../designInfo/native/BoundarySitePolicy.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

// Exact site ownership is the legality authority. Bounding boxes only guide
// continuous placement; they never authorize a site absent from the mask.
class HardRegionConstraints
{
  public:
    struct Box {
        float lx=std::numeric_limits<float>::max(), ly=lx;
        float hx=-std::numeric_limits<float>::max(), hy=hx;
        void add(float x,float y) {lx=std::min(lx,x);hx=std::max(hx,x);ly=std::min(ly,y);hy=std::max(hy,y);}
    };
    struct Region {std::string name, prefix; Box box; size_t modeledSites=0;};
    bool enabled=false, partitionFloorplan=false;
    std::set<std::string> boundaryBlocked;
    std::map<std::string,std::set<std::string>> regionClockRegions;
    std::vector<Region> regions;
    std::unordered_map<std::string,int> siteOwners;

    explicit HardRegionConstraints(const std::map<std::string,std::string> &cfg) {
        auto feature=cfg.find("DFX partition floorplan");
        if(feature!=cfg.end() && feature->second!="true" && feature->second!="false")
            throw std::runtime_error("DFX partition floorplan must be true or false");
        partitionFloorplan=feature!=cfg.end()&&feature->second=="true";
        auto mode=cfg.find("DFX placement mode");
        if(partitionFloorplan && (mode==cfg.end()||mode->second!="full"))
            throw std::runtime_error("DFX partition floorplan requires full hard-region mode");
        if(mode==cfg.end()) return;
        if(mode->second!="full" && mode->second!="incremental") throw std::runtime_error("Unknown DFX placement mode");
        if(mode->second!="full") return;
        if(IncrementalContext::required(cfg,"IncremenralCompilation")!="true")
            throw std::runtime_error("Full DFX requires IncremenralCompilation=true");
        enabled=true;
        std::ifstream in(IncrementalContext::required(cfg,"DFX regions file"));std::string line;
        if(!std::getline(in,line)||line!="region\tinstance")throw std::runtime_error("Invalid DFX region header");
        std::map<std::string,int> ids;
        while(std::getline(in,line)) {
            auto f=IncrementalContext::fields(line);
            if(f.size()!=2||f[0].empty()||f[1].empty()||ids.count(f[0]))throw std::runtime_error("Invalid DFX region row");
            if(regions.empty() ? f[1]!="*" : f[1]=="*")throw std::runtime_error("First DFX region must be static (*) exactly once");
            if(f[1]!="*" && (f[1].back()=='/' || f[1].find_first_of(" \r\n")!=std::string::npos))throw std::runtime_error("Invalid RP hierarchy");
            for(size_t i=1;i<regions.size();++i)
                if(under(f[1],regions[i].prefix)||under(regions[i].prefix,f[1]))throw std::runtime_error("Nested/overlapping RPs are unsupported");
            ids[f[0]]=regions.size();regions.push_back({f[0],f[1],{},0});
        }
        if(regions.size()<2)throw std::runtime_error("Full DFX requires static and dynamic regions");
        std::ifstream sites(IncrementalContext::required(cfg,"DFX region sites file"));
        if(!std::getline(sites,line)||line!="region\tsite")throw std::runtime_error("Invalid DFX region-site header");
        while(std::getline(sites,line)) {
            auto f=IncrementalContext::fields(line);
            if(f.size()!=2||!ids.count(f[0])||f[1].empty()||!siteOwners.emplace(f[1],ids.at(f[0])).second)
                throw std::runtime_error("Unknown region or duplicate/overlapping site in DFX mask: "+line);
        }
        if(partitionFloorplan) {
            std::map<std::string,std::set<std::string>> allowed;
            for(size_t i=1;i<regions.size();++i)allowed[regions[i].name];
            for(const auto &entry:siteOwners)if(entry.second>0)allowed[regions[entry.second].name].insert(entry.first);
            const auto geometry=IncrementalContext::required(cfg,"DFX resource geometry file");
            const auto policy=amf_native::BoundarySitePolicy::derive(
                IncrementalContext::required(cfg,"DFX resource inventory file"),geometry,allowed,"interior-prohibit");
            regionClockRegions=amf_native::BoundarySitePolicy::clockRegions(geometry,allowed);
            for(const auto &group:policy.prohibited)boundaryBlocked.insert(group.second.begin(),group.second.end());
            for(const auto &group:policy.margin)boundaryBlocked.insert(group.second.begin(),group.second.end());
            std::ofstream audit(IncrementalContext::required(cfg,"DFX boundary audit file"));
            audit<<policy.audit;audit.close();
            if(!audit)throw std::runtime_error("Cannot write full DFX boundary audit");
            std::ofstream mask(IncrementalContext::required(cfg,"DFX effective sites file"));
            mask<<"region\tsite\n";
            std::map<std::string,int> ordered(siteOwners.begin(),siteOwners.end());
            for(const auto &entry:ordered)if(!boundaryBlocked.count(entry.first))
                mask<<regions[entry.second].name<<'\t'<<entry.first<<'\n';
            mask.close();if(!mask)throw std::runtime_error("Cannot write effective DFX placement mask");
        }
    }
    static bool under(const std::string &name,const std::string &prefix) {
        return name==prefix || name.compare(0,prefix.size()+1,prefix+"/")==0;
    }
    int cellRegion(const std::string &name) const {
        if(!enabled)return 0;
        for(size_t i=1;i<regions.size();++i)if(under(name,regions[i].prefix))return i;
        return 0;
    }
    bool allows(int region,const std::string &site) const {
        if(!enabled)return true;
        auto it=siteOwners.find(site);return it!=siteOwners.end()&&it->second==region&&!boundaryBlocked.count(site);
    }
    void addModeledSite(const std::string &name,float x,float y) {
        auto it=siteOwners.find(name);if(it==siteOwners.end())return;
        auto &r=regions.at(it->second);r.box.add(x,y);++r.modeledSites;
    }
    void finishModel() {
        for(auto &r:regions) {
            if(!r.modeledSites)throw std::runtime_error("DFX region has no modeled sites: "+r.name);
            r.box.lx-=0.001f;r.box.hx+=0.001f;r.box.ly-=0.001f;r.box.hy+=0.001f;
        }
        size_t total=0;for(const auto&r:regions)total+=r.modeledSites;
        if(total!=siteOwners.size())throw std::runtime_error("DFX mask references unmodeled sites");
    }
    // Project the complete PU envelope, preserving relative macro offsets.
    // Static logic is guided out of RP bounding boxes; exact mask checks in
    // matching and packing additionally exclude snapped routing-footprint sites.
    void project(int region,float left,float right,float bottom,float top,float &x,float &y) const {
        if(!enabled)return;
        const auto &b=regions.at(region).box;
        const float lx=b.lx-left,hx=b.hx-right,ly=b.ly-bottom,hy=b.hy-top;
        if(lx>hx||ly>hy)throw std::runtime_error("PU envelope cannot fit its hard DFX region");
        x=std::max(lx,std::min(hx,x));y=std::max(ly,std::min(hy,y));
        if(region!=0)return;
        auto legal=[&](float xx,float yy) {
            for(size_t i=1;i<regions.size();++i) {
                const auto &q=regions[i].box;
                if(xx+right>=q.lx && xx+left<=q.hx && yy+top>=q.ly && yy+bottom<=q.hy)return false;
            }
            return true;
        };
        if(legal(x,y))return;
        std::vector<float> xs{x,lx,hx},ys{y,ly,hy};
        for(size_t i=1;i<regions.size();++i) {
            const auto &q=regions[i].box;
            xs.push_back(q.lx-right-0.002f);xs.push_back(q.hx-left+0.002f);
            ys.push_back(q.ly-top-0.002f);ys.push_back(q.hy-bottom+0.002f);
        }
        float best=std::numeric_limits<float>::max(),bx=x,by=y;
        for(float xx:xs)for(float yy:ys)if(xx>=lx&&xx<=hx&&yy>=ly&&yy<=hy&&legal(xx,yy)) {
            float d=(xx-x)*(xx-x)+(yy-y)*(yy-y);
            if(d<best){best=d;bx=xx;by=yy;}
        }
        if(best==std::numeric_limits<float>::max())throw std::runtime_error("No continuous static area for PU envelope");
        x=bx;y=by;
    }
};
#endif
