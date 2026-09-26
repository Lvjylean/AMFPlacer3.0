#include "RegionCapacityTracker.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using M = PhysicalBoundaryModel;
RegionCapacityTracker::RegionCapacityTracker(const std::vector<Resources> &caps,
                                             const std::vector<Resources> &occupied)
    : capacity(caps), used(occupied)
{
    if (capacity.size()!=used.size()) throw std::runtime_error("Region budget size mismatch");
}
RegionCapacityTracker::RegionCapacityTracker(PlacementInfo *p) : placement(p), model(p->getDeviceInfo()->getPhysicalBoundaryModel())
{
    if (!model) throw std::runtime_error("Region budget requires physical model");
    for (const auto &r:model->getRegions()) capacity.push_back(r.capacity);
    used.resize(capacity.size());
    for (auto pu:placement->getPlacementUnits())
    {
        auto demand=currentDemand(pu);
        for (size_t r=0;r<used.size();++r)
            for (size_t k=0;k<M::ResourceCount;++k) used[r][k]+=demand[r][k];
    }
}
RegionCapacityTracker::Resources RegionCapacityTracker::cellDemand(DesignInfo::DesignCell *cell,bool memory) const
{
    Resources d{};
    // InitialPacker represents a real RAMB36 plus a virtual RAMB18 upper half.
    // The real 36K demand already consumes both 18K slots in fits().
    if (cell->isVirtualCell() && cell->isBRAM()) return d;
    auto table=placement->getCompatiblePlacementTable();
    auto &occupation=table->getcellId2Occupation();
    double slots=cell->getCellId()<int(occupation.size()) ? occupation[cell->getCellId()] : table->getOccupation(cell->getCellType());
    if (cell->isLUT())
    {
        d[M::LUT]=std::max(cell->isLUT6()?1.0:0.5,slots/2);
        if (memory || cell->originallyIsLUTRAM() || cell->originallyIsShifter()) d[M::MLUT]=d[M::LUT];
    }
    else if (cell->isLUTRAM() || cell->isShifter())
        d[M::LUT]=d[M::MLUT]=std::max(0.5,slots/2);
    else if (cell->isFF()) d[M::FF]=std::max(1.0,slots);
    else if (cell->isCarry()) d[M::CARRY]=1;
    else if (cell->getCellType()==DesignInfo::CellType_MUXF7) d[M::MUX7]=1;
    else if (cell->getCellType()==DesignInfo::CellType_MUXF8) d[M::MUX8]=1;
    else if (cell->isDSP()) d[M::DSP]=1;
    else if (cell->isURAM()) d[M::URAM]=1;
    else if (cell->isBRAM())
    {
        if (cell->getCellType()==DesignInfo::CellType_RAMB36E2 || cell->getCellType()==DesignInfo::CellType_FIFO36E2)
            d[M::BRAM36]=1;
        else d[M::BRAM18]=1;
    }
    return d;
}
std::vector<RegionCapacityTracker::Resources> RegionCapacityTracker::currentDemand(PU *pu) const
{
    std::vector<Resources> result(capacity.size());
    auto add=[&](DesignInfo::DesignCell *cell,float x,float y) {
        int region=model->regionAt(x,y);
        if(region<0) throw std::runtime_error("Non-finite PU location in region budget");
        auto d=cellDemand(cell,pu->checkHasLUTRAM());
        for(size_t k=0;k<M::ResourceCount;++k) result[region][k]+=d[k];
    };
    if(auto cell=dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu))
        add(cell->getCell(),pu->X(),pu->Y());
    else if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
        for(int i=0;i<macro->getNumOfCells();++i)
        {
            float x,y; DesignInfo::DesignCellType type;
            macro->getVirtualCellInfo(i,x,y,type);
            add(macro->getCell(i),pu->X()+x,pu->Y()+y);
        }
    return result;
}
bool RegionCapacityTracker::fits(const Resources &u,const Resources &c)
{
    for(size_t k=0;k<M::ResourceCount;++k)
        if(!std::isfinite(u[k]) || u[k]<-1e-4 || u[k]>c[k]+1e-4) return false;
    // RAMB36 is an alternative view of two 18K slots.
    return u[M::BRAM18]+2*u[M::BRAM36]<=c[M::BRAM18]+1e-4;
}
bool RegionCapacityTracker::reserve(int key,const std::vector<Resources> &delta,bool commit,std::string *reason)
{
    if(delta.size()!=used.size() || reservations.count(key))
    { if(reason)*reason="duplicate-reservation-or-invalid-size"; return false; }
    for(size_t r=0;r<used.size();++r)
    {
        auto candidate=used[r];
        bool increases=false;
        for(size_t k=0;k<M::ResourceCount;++k)
        {
            candidate[k]+=delta[r][k]; increases|=delta[r][k]>1e-4;
            if(!std::isfinite(candidate[k]) || candidate[k]<-1e-4)
            {if(reason)*reason="negative-or-nonfinite-usage";return false;}
        }
        // Initial placement may be overloaded: a pure removal must remain possible.
        if(increases && !fits(candidate,capacity[r]))
        { if(reason)*reason="region-capacity-"+std::to_string(r); return false; }
    }
    if(commit)
    {
        reservations[key]=delta;
        for(size_t r=0;r<used.size();++r)
            for(size_t k=0;k<M::ResourceCount;++k) used[r][k]+=delta[r][k];
    }
    return true;
}
void RegionCapacityTracker::release(int key)
{
    auto it=reservations.find(key);
    if(it==reservations.end())return;
    for(size_t r=0;r<used.size();++r)
        for(size_t k=0;k<M::ResourceCount;++k) used[r][k]-=it->second[r][k];
    reservations.erase(it);
}
bool RegionCapacityTracker::assign(const std::vector<PU *> &units,int target,bool commit,std::string *reason)
{
    if(target<0 || target>=int(used.size()) || units.empty()) return false;
    std::vector<Resources> delta(used.size());
    for(auto pu:units)
    {
        if(reservations.count(pu->getId()))
        {if(reason)*reason="unit-already-reserved";return false;}
        auto current=currentDemand(pu);
        for(size_t r=0;r<used.size();++r)
            for(size_t k=0;k<M::ResourceCount;++k)
            {delta[r][k]-=current[r][k];delta[target][k]+=current[r][k];}
    }
    // Negative aggregate keys are distinct from non-negative PU IDs.
    if(!reserve(-1,delta,false,reason)) return false;
    if(commit)
    {
        for(auto pu:units)
        {
            auto current=currentDemand(pu);
            std::vector<Resources> individual(used.size());
            for(size_t r=0;r<used.size();++r)
                for(size_t k=0;k<M::ResourceCount;++k)
                {individual[r][k]-=current[r][k];individual[target][k]+=current[r][k];}
            reservations[pu->getId()]=individual;
        }
        for(size_t r=0;r<used.size();++r)
            for(size_t k=0;k<M::ResourceCount;++k)used[r][k]+=delta[r][k];
    }
    return true;
}

