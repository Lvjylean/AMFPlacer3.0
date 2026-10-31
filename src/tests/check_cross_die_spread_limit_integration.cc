#include "GeneralSpreader.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <thread>

using PI = PlacementInfo;
using PU = PI::PlacementUnit;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }

struct CrossDieSpreadLimitTestAccess {
    static void activate(GeneralSpreader &s, PI &pi, const std::string &type, float x, float y, bool local) {
        auto bin = s.dieView->at(x,y);
        auto region = new GeneralSpreader::SpreadRegion(bin,&pi,s.binGrid,.8,200);
        region->decision.local=local;
        s.expandedRegions.push_back(region);
        s.activateCrossDieLimits();
        delete region; s.expandedRegions.clear();
    }
    static void write(GeneralSpreader &s, PI &pi, CrossDieSpreadLimit &limit,
                      float x, float y, bool parallel) {
        auto units=pi.getPlacementUnits(); std::set<PU*> involved(units.begin(),units.end());
        std::set<DesignInfo::DesignCell*> cells;
        std::map<int,PI::Location> before;
        for(auto pu:units) before[pu->getId()]={pu->X(),pu->Y()};
        for(auto c:pi.getDesignInfo()->getCells()) {
            cells.insert(c); auto pu=pi.getPlacementUnitByCell(c); float ox=0,oy=0;
            if(auto m=dynamic_cast<PI::PlacementMacro*>(pu)) {
                ox=m->getCellOffsetXInMacro(c);oy=m->getCellOffsetYInMacro(c);
            }
            pi.getCellId2location()[c->getCellId()]={x+ox,y+oy};
        }
        if(parallel) {
            const int middle=units.size()/2;
            std::thread a([&]{GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocationsWorker(
                &pi,involved,cells,units,1,10,0,middle,&limit);});
            std::thread b([&]{GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocationsWorker(
                &pi,involved,cells,units,1,10,middle,units.size(),&limit);});
            a.join();b.join();
        } else s.updatePlacementUnitsWithSpreadedCellLocations(involved,cells,units,1,false,10);
        s.constrainLocalWrites(units,before);
        s.constrainCrossDieWrites(units,before);
        s.finishDieFirstIteration(0,1,units,before);
    }
    static void checkView(GeneralSpreader &s, PI &pi) {
        std::set<int> seen;
        for(auto &row:s.dieView->rows) for(auto bin:row) for(auto c:bin->getCells()) {
            auto loc=pi.getCellId2location().at(c->getCellId());
            check(bin==s.dieView->at(loc.X,loc.Y),"private density view stale");
            check(seen.insert(c->getCellId()).second,"duplicate private density member");
        }
        check(seen.size()==pi.getDesignInfo()->getCells().size(),"private density member lost");
    }
};

static double total(PI &pi,const std::string &type) {
    double n=0;for(auto &row:pi.getBinGrid(pi.getSharedBELTypeId(type)))
        for(auto bin:row)n+=bin->getUtilization();return n;
}
static void checkCells(PI &pi) {
    for(auto c:pi.getDesignInfo()->getCells()) {
        auto pu=pi.getPlacementUnitByCell(c);float ox=0,oy=0;
        if(auto m=dynamic_cast<PI::PlacementMacro*>(pu)) {
            ox=m->getCellOffsetXInMacro(c);oy=m->getCellOffsetYInMacro(c);
        }
        auto loc=pi.getCellId2location().at(c->getCellId());
        check(std::abs(loc.X-pu->X()-ox)<1e-4 && std::abs(loc.Y-pu->Y()-oy)<1e-4,"macro/cell coordinates split");
        int bx,by;pi.getGridXY(loc.X,loc.Y,bx,by);
        const auto &actual=pi.getCellId2CellBinInfo().at(c->getCellId());
        check(actual.X==bx && actual.Y==by,"physical capacity bin stale");
    }
}

int main(int argc,char **argv) { try {
    check(argc==3,"usage: checkCrossDieSpreadLimitIntegration fixture.json NEW_OUTPUT");
    auto root=std::filesystem::path(argv[2]);check(std::filesystem::create_directory(root),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["jobs"]="2";cfg["DetailedDiagnosticsEnable"]="false";
    cfg["CrossDieSpreadMaxBins"]="20";cfg["BoundaryRegionClipping"]="false";
    for(const auto &key:{"SllReportDirectory","BoundaryReportDirectory","SllPlanningReportDirectory","dumpDirectory"})
        cfg[key]=(root/key).string();
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PI pi(&design,&dev,cfg);
    std::vector<float> seams;
    for(auto &b:dev.getPhysicalBoundaryModel()->getBoundaries())
        if(b.active&&b.kind=="SLR"&&b.orientation=='Y')seams.push_back(b.coordinate);
    std::sort(seams.begin(),seams.end());check(!seams.empty(),"SLR fixture needed");
    const float y0=seams[0]-2;
    auto cells=design.getCells();check(cells.size()==4,"four FDRE fixture required");
    auto macro=new PI::PlacementMacro("pair",0,PI::PlacementMacro::PlacementMacroType_LUTLUTSeires);
    for(int i=0;i<2;++i) {
        macro->addCell(cells[i],cells[i]->getCellType(),0,float(i));
        pi.getCellId2PlacementUnit()[cells[i]->getCellId()]=macro;
    }
    pi.getPlacementUnits().push_back(macro);pi.getPlacementMacros().push_back(macro);
    for(size_t i=2;i<cells.size();++i) {
        auto u=new PI::PlacementUnpackedCell(cells[i]->getName(),pi.getPlacementUnits().size(),cells[i]);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);
        pi.getCellId2PlacementUnit()[cells[i]->getCellId()]=u;
    }
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);
    auto units=pi.getPlacementUnits();auto fixed=units.back();fixed->setFixed();
    std::string type="SLICEL_FF";
    for(bool parallel:{false,true})for(float bin:{5.f,2.5f,2.f}) {
        for(auto pu:units) {
            pu->setAnchorLocationAndForgetTheOriginalOne(5,y0-60);pu->recordSpreadLocatin();
            pu->setAnchorLocationAndForgetTheOriginalOne(50,y0);
        }
        pi.createGridBins(bin,bin);pi.updateElementBinGrid(false);
        const double beforeLoad=total(pi,type);
        CrossDieSpreadLimit limit;GeneralSpreader::initializeRoundLimit(&pi,cfg,limit);
        {
            GeneralSpreader spread(&pi,cfg,type,0,.8,false,&limit);
            CrossDieSpreadLimitTestAccess::activate(spread,pi,type,50,y0,true);
            check(limit.activeUnits==0,"die-local region unexpectedly activated limit");
            CrossDieSpreadLimitTestAccess::activate(spread,pi,type,50,y0,false);
            check(limit.active(0) && !limit.active(fixed->getId()),"cross region activation / fixed exclusion");
            // A legitimate >10-unit move must survive the old timing cap.
            CrossDieSpreadLimitTestAccess::write(spread,pi,limit,68,y0+24,parallel);
            check(std::abs(macro->X()-68)<1e-4 && std::abs(macro->Y()-y0-24)<1e-4,"old 10-unit cap still masks new budget");
            CrossDieSpreadLimitTestAccess::write(spread,pi,limit,110,y0+80,parallel);
            check(limit.contains(0,{macro->X(),macro->Y()}),"round radius exceeded");
            check(std::abs(macro->X()-(50+12*bin))<1e-3 && std::abs(macro->Y()-(y0+16*bin))<1e-3,"diagonal endpoint");
            spread.recordSpreadedCellLocations();
            checkCells(pi);CrossDieSpreadLimitTestAccess::checkView(spread,pi);
        }
        // A new resource-pass object shares the exact same post-QP reference.
        {
            GeneralSpreader next(&pi,cfg,type,0,.8,false,&limit);
            CrossDieSpreadLimitTestAccess::write(next,pi,limit,170,y0+160,parallel);
            check(limit.contains(0,{macro->X(),macro->Y()}),"second pass exceeded shared radius");
            check(limit.origin(0).x==50 && limit.origin(0).y==y0,"reference reset between passes");
            checkCells(pi);CrossDieSpreadLimitTestAccess::checkView(next,pi);
            check(std::abs(total(pi,type)-beforeLoad)<1e-5,"capacity demand lost after clipping");
            check(fixed->X()==50 && fixed->Y()==y0,"fixed PU moved");
        }
        std::cout<<"PASS bin="<<bin<<" parallel="<<parallel<<" clipped="<<limit.limitedWrites<<'\n';
    }
    // Explicit zero retains the legacy history-based 10-unit behavior.
    cfg["CrossDieSpreadMaxBins"]="0";
    for(auto pu:units){pu->setAnchorLocationAndForgetTheOriginalOne(50,y0);pu->recordSpreadLocatin();}
    pi.updateElementBinGrid(false);CrossDieSpreadLimit off;GeneralSpreader::initializeRoundLimit(&pi,cfg,off);
    {GeneralSpreader spread(&pi,cfg,type,0,.8,false,&off);
     CrossDieSpreadLimitTestAccess::write(spread,pi,off,80,y0+40,false);
     check(std::abs(macro->X()-56)<1e-4 && std::abs(macro->Y()-y0-8)<1e-4,"disabled mode changed legacy behavior");}
    std::cout<<"PASS real U250 single/parallel writeback, macro offsets, fixed objects, occupancy, cross/local activation, legacy fallback\n";
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
