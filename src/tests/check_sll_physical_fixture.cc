#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include "WirelengthOptimizer.h"
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){try {
    if(argc!=4)throw std::runtime_error("usage: checkSllPhysicalFixture config.json cells.tsv NEW_OUTPUT");
    std::filesystem::path root=argv[3];if(!std::filesystem::create_directory(root))throw std::runtime_error("refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["SllPlanningReportDirectory"]=(root/"diagnostics").string();cfg["SllPlanningAllowSplit"]="false";
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PlacementInfo pi(&design,&dev,cfg);
    std::map<std::string,std::string> sites;std::ifstream f(argv[2]);std::string row;std::getline(f,row);
    while(std::getline(f,row)){auto i=row.find('\t');if(i==std::string::npos)throw std::runtime_error("bad cell site TSV");sites[row.substr(0,i)]=row.substr(i+1);}
    std::map<std::string,DeviceInfo::DeviceSite*> inventory;for(auto site:dev.getSites())inventory[site->getName()]=site;
    for(auto cell:design.getCells())if(!cell->isVirtualCell()) {
        auto u=new PlacementInfo::PlacementUnpackedCell(cell->getName(),pi.getPlacementUnits().size(),cell);
        auto site=inventory.at(sites.at(cell->getName()));u->setAnchorLocationAndForgetTheOriginalOne(site->X(),site->Y());u->setFixed();
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[cell->getCellId()]=u;
    }
    pi.updateCells2PlacementUnits();pi.reloadNets();
    if(cfg.count("SllFixtureQpSteps"))pi.getTimingInfo()->buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);
    if(cfg.count("SllFixtureQpSteps")){
        // Fixed real endpoints isolate movement of the new auxiliary variables.
        WirelengthOptimizer wl(&pi,cfg,false);
        const int steps=std::stoi(cfg.at("SllFixtureQpSteps"));
        for(int i=0;i<steps;++i)wl.GlobalPlacementQPSolve(.02,i==0,true,false,true,false,-10,nullptr,false);
    }
    pi.sllPlanner->exportPlan((root/"sll-plan.tcl").string());
    const auto&m=pi.sllPlanner->model();std::ofstream out(root/"predicted.tsv");out<<"driver\tsink\tsll_count\tdelay_ns\n";
    size_t crossings=0;
    for(const auto&p:m.plans){crossings+=p.crossings.size();for(const auto&s:p.net.sinks)out<<p.net.source.name<<'\t'<<s.name<<'\t'<<p.paths.at(s.id).size()<<'\t'<<m.pathDelay(p,s.id,p.net.source.p,s.p)<<'\n';}
    std::cout<<"PASS physical fixture plan exported: nets="<<m.plans.size()<<" virtual_cells="<<crossings<<" actual_cells="<<design.getCells().size()<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
