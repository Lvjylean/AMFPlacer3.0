#pragma once
// Opt-in diagnostics. The production region construction, kernel and PU writeback are unchanged.
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <omp.h>

namespace amf_spread_capture {
using Clock = std::chrono::steady_clock;
struct Task { double seconds=0; int worker=-1, team=0, level=0, active=0; };
struct Capture {
    std::string directory, prefix, type;
    int call=0, inner=0, iteration=0, jobs=0;
    size_t covered=0;
    PlacementInfo *placement;
    std::vector<GeneralSpreader::SpreadRegion*> &regions;
    std::vector<Task> tasks;
    std::vector<int> ids;
    Clock::time_point started;
    double collectionSeconds=0;
    bool enabled=false;
    Capture(PlacementInfo *p, std::vector<GeneralSpreader::SpreadRegion*> &r,
            std::vector<std::vector<PlacementInfo::PlacementBinInfo*>> &grid,
            const std::string &t, int outer, int loop, int iter, int n, size_t bins, float shrink,
            float forget, bool crAware, float displacement)
        : type(t),call(outer),inner(loop),iteration(iter),jobs(n),covered(bins),placement(p),regions(r),tasks(r.size()) {
        const char *env=std::getenv("AMF_SPREAD_REPLAY_DIR");
        if (!env) return;
        enabled=true; directory=env;
        bool selected=(call==1 || call==8 || call==20 || call==32 || call==37) && (inner==1 || inner==4);
        if (!selected) return;
        AMF_PROFILE_SCOPE("diagnostics", "spread_snapshot_input");
        std::string safe=type;for(char &c:safe)if(!std::isalnum(static_cast<unsigned char>(c)))c='_';
        prefix=directory+"/spread-"+safe+"-"+std::to_string(call)+"-"+std::to_string(inner);
        std::ofstream out(prefix+".input"); if(!out)throw std::runtime_error("Cannot create spreading snapshot");
        out<<std::setprecision(std::numeric_limits<float>::max_digits10);
        out<<"AMF_SPREAD_V1 "<<grid.size()<<' '<<grid.at(0).size()<<' '<<p->getCellId2location().size()<<' '
           <<jobs<<' '<<covered<<' '<<shrink<<' '<<p->getBinGridW()<<' '<<p->getBinGridH()<<' '
           <<iteration<<' '<<call<<' '<<inner<<' '<<forget<<' '<<crAware<<' '<<displacement<<'\n';
        for(auto &row:grid)for(auto b:row)out<<b->getCapacity()<<' '<<b->left()<<' '<<b->right()<<' '<<b->bottom()<<' '<<b->top()<<'\n';
        out<<regions.size()<<'\n';
        std::set<int> unique;
        for(auto region:regions){
            out<<region->top()<<' '<<region->bottom()<<' '<<region->left()<<' '<<region->right()<<' '<<region->getCells().size();
            for(auto cell:region->getCells()){
                int id=cell->getCellId();if(!unique.insert(id).second)throw std::runtime_error("Overlapping spreading cell ownership");
                out<<' '<<id;
            }
            out<<'\n';
        }
        ids.assign(unique.begin(),unique.end());out<<ids.size()<<'\n';
        for(int id:ids){auto loc=p->getCellId2location()[id];out<<id<<' '<<loc.X<<' '<<loc.Y<<' '<<p->getActualOccupationByCellId(id)<<'\n';}
        if(!out)throw std::runtime_error("Spreading snapshot write failed");
    }
    void begin(){started=Clock::now();}
    void afterKernel(){
        if(!enabled)return;
        double wall=std::chrono::duration<double>(Clock::now()-started).count();
        AMF_PROFILE_SCOPE("diagnostics", "spread_snapshot_output");
        std::ofstream trace(directory+"/tasks.tsv",std::ios::app);
        trace<<std::setprecision(12);
        for(size_t i=0;i<regions.size();++i){auto&r=tasks[i];auto b=regions[i];
            trace<<type<<'\t'<<call<<'\t'<<inner<<'\t'<<iteration<<'\t'<<i<<'\t'<<b->getCells().size()<<'\t'
                 <<b->getBinsInRegion().size()<<'\t'<<r.worker<<'\t'<<r.team<<'\t'<<r.level<<'\t'<<r.active<<'\t'
                 <<r.seconds<<'\t'<<wall<<'\t'<<collectionSeconds<<'\n';
        }
        if(prefix.empty())return;
        std::ofstream out(prefix+".expected");out<<std::setprecision(std::numeric_limits<float>::max_digits10);
        for(int id:ids){auto loc=placement->getCellId2location()[id];out<<id<<' '<<loc.X<<' '<<loc.Y<<'\n';}
    }
    void puState(const std::vector<PlacementInfo::PlacementUnit*> &pus,const std::string &stage){
        if(prefix.empty())return;
        AMF_PROFILE_SCOPE("diagnostics", "spread_pu_snapshot");
        std::ofstream out(prefix+".pu-"+stage+".tsv");out<<std::setprecision(std::numeric_limits<float>::max_digits10);
        for(auto pu:pus)out<<pu->getId()<<'\t'<<pu->X()<<'\t'<<pu->Y()<<'\t'<<pu->isFixed()<<'\t'<<pu->getWeight()<<'\n';
    }
};
}
