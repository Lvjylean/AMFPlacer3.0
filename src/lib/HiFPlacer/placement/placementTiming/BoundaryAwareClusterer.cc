#include "BoundaryAwareClusterer.h"
#include "RegionCapacityTracker.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
std::vector<DesignInfo::DesignCell *> puCells(BoundaryAwareClusterer::PU *pu)
{
    if(auto cell=dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) return {cell->getCell()};
    if(auto macro=dynamic_cast<PlacementInfo::PlacementMacro *>(pu)) return macro->getCells();
    return {};
}
float number(std::map<std::string,std::string> &config,const std::string &key,float fallback)
{
    auto it=config.find(key); if(it==config.end())return fallback;
    size_t parsed=0; float value=std::stof(it->second,&parsed);
    if(parsed!=it->second.size() || !std::isfinite(value) || value<0)
        throw std::runtime_error("Invalid physical clustering parameter: "+key);
    return value;
}
}
BoundaryAwareClusterer::BoundaryAwareClusterer(PlacementInfo *p,PlacementTimingOptimizer *t,
                                               std::map<std::string,std::string> &cfg)
    : placement(p),timing(t),model(p->getDeviceInfo()->getPhysicalBoundaryModel()),config(cfg)
{
    if(!model)throw std::runtime_error("Boundary clustering requires a physical model");
    y2xRatio=number(cfg,"y2xRatio",0.4f);
    maxClusters=int(number(cfg,"BoundaryMaxClusters",512));
    maxPathCells=int(number(cfg,"BoundaryMaxPathCells",128));
    maxEdges=int(number(cfg,"BoundaryMaxAffectedEdges",8192));
    nearCriticalFraction=number(cfg,"BoundaryNearCriticalFraction",0.15f);
    maxDisplacement=number(cfg,"BoundaryMaxDisplacement",280);
    minGain=number(cfg,"BoundaryMinGainNs",0.05f);
    displacementCost=number(cfg,"BoundaryDisplacementCost",0.0001f);
    if(maxClusters<1 || maxClusters>10000 || maxPathCells<2 || maxPathCells>1024 || maxEdges<1 || maxDisplacement<=0)
        throw std::runtime_error("Physical clustering search bounds are invalid");
}
std::vector<BoundaryAwareClusterer::Edge *> BoundaryAwareClusterer::affectedEdges(const std::vector<PU *> &units) const
{
    auto graph=placement->getTimingInfo()->getSimplePlacementTimingGraph();
    std::map<int,Edge *> unique;
    for(auto pu:units)for(auto cell:puCells(pu))
    {
        if(cell->getCellId()>=int(graph->getNodes().size()))continue;
        auto node=graph->getNodes()[cell->getCellId()];
        for(auto e:node->getInEdges())unique[e->getId()]=e;
        for(auto e:node->getOutEdges())unique[e->getId()]=e;
        if(unique.size()>size_t(maxEdges))return {};
    }
    std::vector<Edge *> result;
    for(auto pair:unique)result.push_back(pair.second);
    return result;
}
bool BoundaryAwareClusterer::targets(const std::vector<PU *> &units,int region,
                                     std::map<PU *,std::pair<float,float>> &positions) const
{
    positions.clear();
    for(auto pu:units)
    {
        float x,y;
        if(!placement->regionTarget(pu,region,x,y) || std::fabs(x-pu->X())+y2xRatio*std::fabs(y-pu->Y())>maxDisplacement)
            return false;
        positions[pu]={x,y};
    }
    return true;
}
BoundaryAwareClusterer::Score BoundaryAwareClusterer::score(const std::vector<Edge *> &edges,
                                       const std::map<PU *,std::pair<float,float>> &positions) const
{
    Score s;
    const auto &pins=placement->getPinId2location();
    auto moved=[&](DesignInfo::DesignPin *pin) {
        auto loc=pins.at(pin->getElementIdInType());
        auto pu=placement->getPlacementUnitByCellId(pin->getCell()->getCellId());
        auto it=positions.find(pu);
        if(it!=positions.end()){loc.X+=it->second.first-pu->X();loc.Y+=it->second.second-pu->Y();}
        return loc;
    };
    for(auto edge:edges)
    {
        auto a=moved(edge->getSourcePin()),b=moved(edge->getSinkPin());
        if((a.X<-5 && a.Y<-5)||(b.X<-5 && b.Y<-5))continue;
        float delay=timing->getDelayByModel(a.X,a.Y,b.X,b.Y);
        float required=edge->getSink()->getRequiredArrivalTime();
        float arrival=edge->getSource()->getLatestOutputArrival();
        float period=std::max(0.1f,edge->getSink()->getClockPeriod());
        float oldSlack=required-arrival-edge->getDelay();
        double weight=1.0+std::min(20.0f,std::max(0.0f,1.0f-oldSlack/period)*4);
        s.weighted+=weight*delay;
        s.worst=std::max(s.worst,double(arrival+delay-required));
    }
    for(auto entry:positions)
        s.displacement+=std::fabs(entry.first->X()-entry.second.first)+y2xRatio*std::fabs(entry.first->Y()-entry.second.second);
    return s;
}
void BoundaryAwareClusterer::run()
{
    placement->clearRegionPreferences();
    RegionCapacityTracker budget(placement);
    auto graph=placement->getTimingInfo()->getSimplePlacementTimingGraph();
    graph->sortedEndpointByDelay();
    auto endpoints=graph->getSortedTimingEndpoints();
    std::sort(endpoints.begin(),endpoints.end(),[](Node *a,Node *b){
        float sa=a->getRequiredArrivalTime()-a->getLatestInputArrival();
        float sb=b->getRequiredArrivalTime()-b->getLatestInputArrival();
        return sa==sb ? a->getId()<b->getId() : sa<sb;
    });
    std::ofstream report;
    if(!config["BoundaryReportDirectory"].empty())
        report.open(config["BoundaryReportDirectory"]+"/clusters.tsv",std::ios::app);
    if(report.tellp()==0)report<<"endpoint\tcluster\tunits\ttarget_region\told_weighted_ns\tnew_weighted_ns\told_worst_ns\tnew_worst_ns\tdisplacement\treason\n";
    std::set<int> claimed;
    int accepted=0,examined=0;
    const auto &regions=model->getRegions();
    for(auto endpoint:endpoints)
    {
        float slack=endpoint->getRequiredArrivalTime()-endpoint->getLatestInputArrival();
        if(slack>nearCriticalFraction*std::max(0.1f,endpoint->getClockPeriod()))continue;
        if(examined++>=maxClusters*4 || accepted>=maxClusters)break;
        auto path=graph->backTraceDelayLongestPathFromNode(endpoint->getId());
        // Long paths are split into bounded segments, retaining hard endpoints
        // through affected-edge scoring rather than discarding DSP/URAM paths.
        for(size_t start=0;start<path.size() && accepted<maxClusters;start+=maxPathCells)
        {
            std::map<int,PU *> byId;
            for(size_t i=start;i<std::min(path.size(),start+size_t(maxPathCells));++i)
            {
                auto pu=placement->getPlacementUnitByCellId(path[i]);
                if(pu && !pu->isFixed() && !pu->isLocked() && !pu->checkHasDSP() && !pu->checkHasBRAM() &&
                    !pu->checkHasURAM() && !claimed.count(pu->getId()) &&
                    (pu->checkHasLUT() || pu->checkHasFF() || pu->checkHasCARRY() || pu->checkHasLUTRAM() || pu->checkHasMUX()))
                    byId[pu->getId()]=pu;
            }
            std::vector<PU *> units;
            for(auto item:byId)units.push_back(item.second);
            if(units.empty())continue;
            auto edges=affectedEdges(units);
            if(edges.empty()){if(report)report<<endpoint->getId()<<"\t-1\t"<<units.size()<<"\t-1\t0\t0\t0\t0\t0\tedge-limit-or-empty\n";continue;}
            Score before=score(edges,{}),bestScore=before;
            double bestCost=before.weighted;
            int bestRegion=-1;
            std::string reason="no-positive-gain";
            std::map<int,int> occupancy;
            for(auto pu:units)++occupancy[model->regionAt(pu->X(),pu->Y())];
            int dominant=occupancy.begin()->first;
            for(auto entry:occupancy)if(entry.second>occupancy[dominant])dominant=entry.first;
            for(const auto &r:regions)
            {
                // SLR order is geometric, not the numeric SLR ID.
                const auto &source=regions[dominant];
                bool same=source.slr==r.slr;
                bool adjacent=std::fabs(source.y1-r.y0)<1e-4f || std::fabs(r.y1-source.y0)<1e-4f;
                if(!same && !adjacent)continue;
                if(!budget.assign(units,r.id,false,&reason))continue;
                std::map<PU *,std::pair<float,float>> positions;
                if(!targets(units,r.id,positions)){reason="macro-fit-or-displacement";continue;}
                auto candidate=score(edges,positions);
                double cost=candidate.weighted+displacementCost*candidate.displacement;
                // All affected fan-in and fan-out edges are considered, including
                // fixed and hard-resource endpoints outside the moved segment.
                if(candidate.worst<=before.worst+0.05 && cost+minGain<bestCost)
                {bestRegion=r.id;bestScore=candidate;bestCost=cost;}
            }
            if(bestRegion>=0 && budget.assign(units,bestRegion,true,&reason))
            {
                for(auto pu:units)
                {
                    placement->getRegionPreferences()[pu]={bestRegion,accepted,1.0f};
                    claimed.insert(pu->getId());
                }
                reason="accepted";
                ++accepted;
            }
            if(report)report<<endpoint->getId()<<'\t'<<(bestRegion>=0?accepted-1:-1)<<'\t'<<units.size()<<'\t'<<bestRegion
                <<'\t'<<before.weighted<<'\t'<<bestScore.weighted<<'\t'<<before.worst<<'\t'<<bestScore.worst
                <<'\t'<<bestScore.displacement<<'\t'<<reason<<'\n';
        }
    }
    if(!config["BoundaryReportDirectory"].empty())
    {
        std::ofstream f(config["BoundaryReportDirectory"]+"/region_capacity.tsv",std::ios::app);
        f<<"round_regions\t"<<regions.size()<<"\taccepted_clusters\t"<<accepted<<'\n';
        for(size_t r=0;r<regions.size();++r)
        {
            f<<r;
            for(size_t k=0;k<PhysicalBoundaryModel::ResourceCount;++k)
                f<<'\t'<<budget.getUsage()[r][k]<<'/'<<budget.getCapacity()[r][k];
            f<<'\n';
        }
    }
    print_info("Boundary clustering accepted "+std::to_string(accepted)+" segments / "+std::to_string(claimed.size())+
               " PUs; examined "+std::to_string(examined)+" timing endpoints");
}
void BoundaryAwareClusterer::refresh()
{
    placement->refreshRegionPreferences();
    std::map<int,std::vector<PU *>> groups;
    for(auto entry:placement->getRegionPreferences())groups[entry.second.cluster].push_back(entry.first);
    int released=0;
    for(auto &group:groups)
    {
        auto &units=group.second;
        int region=placement->getRegionPreferences().at(units.front()).region;
        auto edges=affectedEdges(units);
        std::map<PU *,std::pair<float,float>> positions;
        if(edges.empty() || !targets(units,region,positions))
        {for(auto pu:units)released+=placement->getRegionPreferences().erase(pu);continue;}
        auto before=score(edges,{}),after=score(edges,positions);
        // A fulfilled preference may stay dormant. Release a now harmful move.
        if(after.weighted+displacementCost*after.displacement>before.weighted+minGain ||
           after.worst>before.worst+0.05)
            for(auto pu:units)released+=placement->getRegionPreferences().erase(pu);
    }
    if(released)print_info("Boundary preferences released after timing refresh: "+std::to_string(released));
}
void BoundaryAwareClusterer::audit(const std::string &stage)
{
    if(config["BoundaryReportDirectory"].empty())return;
    auto graph=placement->getTimingInfo()->getSimplePlacementTimingGraph();
    const auto &pins=placement->getPinId2location();
    long long slr=0,io=0,crossed=0,valid=0;
    for(auto edge:graph->getEdges())
    {
        auto a=pins[edge->getSourcePin()->getElementIdInType()],b=pins[edge->getSinkPin()->getElementIdInType()];
        if((a.X<-5 && a.Y<-5)||(b.X<-5 && b.Y<-5))continue;
        int s=model->crossingCount(a.X,a.Y,b.X,b.Y,"SLR"),i=model->crossingCount(a.X,a.Y,b.X,b.Y,"IO");
        slr+=s;io+=i;crossed+=(s+i)>0;++valid;
    }
    std::ofstream f(config["BoundaryReportDirectory"]+"/amf_boundaries.tsv",std::ios::app);
    if(f.tellp()==0)f<<"stage\tdriver_sink_edges\tslr_crossings\tio_crossings\tcrossing_edges\tregion_preferences\n";
    f<<stage<<'\t'<<valid<<'\t'<<slr<<'\t'<<io<<'\t'<<crossed<<'\t'<<placement->getRegionPreferences().size()<<'\n';
}

