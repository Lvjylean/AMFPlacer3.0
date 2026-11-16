#ifndef AMF_PACKING_DIE_SCOPE_H
#define AMF_PACKING_DIE_SCOPE_H
#include "../placementInfo/PlacementInfo.h"
#include <limits>
#include <unordered_map>
// Read-only entry snapshot for the main packing and fallback phase only.
class PackingDieScope {
    struct Bounds { int die=-1; float x0=0,x1=0,y0=0,y1=0; };
    PlacementInfo *pi;
    std::vector<Bounds> bounds;
    std::unordered_map<const DeviceInfo::DeviceSite *,int> siteDies;
    int dieAt(float x,float y) const {
        auto m=pi->getDeviceInfo()->getPhysicalBoundaryModel();int region=m->regionAt(x,y);
        if(region<0)throw std::runtime_error("Packing entry coordinate has no physical die");
        return m->getRegions().at(region).slr;
    }
public:
    explicit PackingDieScope(PlacementInfo *p):pi(p) {
        auto m=pi->getDeviceInfo()->getPhysicalBoundaryModel();
        if(!m)throw std::runtime_error("Packing die lock requires physical boundary metadata");
        std::map<int,Bounds> dies;
        for(const auto&r:m->getRegions()) {
            auto it=dies.find(r.slr);
            if(it==dies.end())dies.emplace(r.slr,Bounds{r.slr,r.x0,r.x1,r.y0,r.y1});
            else {auto&b=it->second;b.x0=std::min(b.x0,r.x0);b.x1=std::max(b.x1,r.x1);b.y0=std::min(b.y0,r.y0);b.y1=std::max(b.y1,r.y1);}
        }
        bounds.resize(pi->getPlacementUnits().size());
        for(auto pu:pi->getPlacementUnits()) {
            auto b=dies.at(dieAt(pu->X(),pu->Y()));
            // Constrain the complete macro footprint, not only its anchor.
            if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu)) {
                float left=0,right=0,bottom=0,top=0;
                for(auto c:macro->getCells())if(!c->isVirtualCell()) {
                    float x=macro->getCellOffsetXInMacro(c),y=macro->getCellOffsetYInMacro(c);
                    if(!pu->isFixed() && dieAt(pu->X()+x,pu->Y()+y)!=b.die)
                        throw std::runtime_error("Movable macro straddles packing entry dies: "+pu->getName());
                    left=std::min(left,x);right=std::max(right,x);bottom=std::min(bottom,y);top=std::max(top,y);
                }
                b.x0-=left;b.x1-=right;b.y0-=bottom;b.y1-=top;
            }
            bounds.at(pu->getId())=b;
        }
        for(auto site:pi->getDeviceInfo()->getSites())if(site->getSiteType()=="SLICEL"||site->getSiteType()=="SLICEM")
            siteDies.emplace(site,dieAt(site->X(),site->Y()));
    }
    size_t size()const{return bounds.size();}
    bool allows(PlacementInfo::PlacementUnit *pu,DeviceInfo::DeviceSite *site)const {
        const auto&b=bounds.at(pu->getId());auto it=siteDies.find(site);
        return it!=siteDies.end() && it->second==b.die && site->X()>=b.x0 && site->X()<b.x1 && site->Y()>b.y0 && site->Y()<=b.y1;
    }
    size_t project()const {
        size_t changed=0;
        for(auto pu:pi->getPlacementUnits())if(!pu->isFixed()&&!pu->isLocked()) {
            const auto&b=bounds.at(pu->getId());
            float x=std::max(b.x0+.001f,std::min(b.x1-.001f,pu->X()));
            float y=std::max(b.y0+.001f,std::min(b.y1-.001f,pu->Y()));
            if(x!=pu->X()||y!=pu->Y()) {pu->setAnchorLocationAndForgetTheOriginalOne(x,y);++changed;}
        }
        return changed;
    }
};
#endif
