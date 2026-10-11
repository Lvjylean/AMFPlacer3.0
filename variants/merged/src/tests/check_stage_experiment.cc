#include "BoundaryAwareClusterer.h"
#include "WirelengthOptimizer.h"
#include "GeneralSpreader.h"
#include "GlobalPlacer.h"
#include "RegionCapacityTracker.h"
#include "simpleJSON.h"
#include <fstream>
#include <set>
#include <stdexcept>
#include <omp.h>
static void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void checkFence(PlacementInfo &p, PlacementInfo::PlacementUnit *pu)
{
    auto pref = p.getRegionPreferences().at(pu);
    float l,r,b,t; require(p.paperRegionBounds(pu,pref,!p.paperSLRStage(),l,r,b,t), "Macro no longer fits");
    require(pu->Y() >= b && pu->Y() <= t, "Whole macro escaped SLR after spreading");
    float legalX=pu->X(),legalY=pu->Y();p.legalizeXYInArea(pu,legalX,legalY);
    require(std::fabs(legalX-pu->X())+std::fabs(legalY-pu->Y())<1e-4,"Projection escaped device-area bounds");
    if (!p.paperSLRStage() && pref.guideX)
        require(pu->X() >= l && pu->X() <= r, "Whole macro escaped HPIO side after spreading");
}
// Exercise the production density-spreader worker, including forgetting and
// displacement limiting. Verify final cell coordinates, not just PU anchors.
static void spreadOutward(PlacementInfo &p, bool limited)
{
    std::set<PlacementInfo::PlacementUnit *> units;
    std::set<DesignInfo::DesignCell *> cells;
    for (auto entry : p.getRegionPreferences()) units.insert(entry.first);
    std::vector<PlacementInfo::PlacementUnit *> vec(units.begin(),units.end());
    for (auto pu : vec)
    {
        pu->recordSpreadLocatin();
        auto pref=p.getRegionPreferences().at(pu);
        float l,r,b,t; require(p.paperRegionBounds(pu,pref,true,l,r,b,t),"Missing bounds");
        // Test both die edges and both side edges; the macro's offsets can
        // cross even when its anchor would still be inside the die.
        float x=pref.pullX < 0 ? r+20 : l-20;
        float y=pu->getId()%2 ? b-25 : t+25;
        auto setCell=[&](DesignInfo::DesignCell *cell,float dx,float dy) {
            cells.insert(cell); auto &loc=p.getCellId2location().at(cell->getCellId()); loc.X=x+dx;loc.Y=y+dy;
        };
        if (auto c=dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(pu)) setCell(c->getCell(),0,0);
        else if (auto m=dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
            for(int i=0;i<m->getNumOfCells();++i) {
                float dx,dy;DesignInfo::DesignCellType type;m->getVirtualCellInfo(i,dx,dy,type);setCell(m->getCell(i),dx,dy);
            }
    }
    GeneralSpreader::updatePlacementUnitsWithSpreadedCellLocationsWorker(
        &p,units,cells,vec,.75f,limited?20.0f:-10.0f,0,vec.size());
    for(auto pu:vec) {
        checkFence(p,pu);
        if(auto m=dynamic_cast<PlacementInfo::PlacementMacro *>(pu))
            for(int i=0;i<m->getNumOfCells();++i) {
                float dx,dy;DesignInfo::DesignCellType type;m->getVirtualCellInfo(i,dx,dy,type);
                auto loc=p.getCellId2location().at(m->getCell(i)->getCellId());
                require(loc.X==pu->X()+dx && loc.Y==pu->Y()+dy,"Spread split macro or left stale cell/bin location");
            }
    }
}
int main(int argc, char **argv)
{
    try {
        require(argc == 4,"Usage: checkPaperBoundaryClustering config scenario result");
        auto cfg=parseJSONFile(argv[1]);std::string scenario=argv[2];omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
        PlacementInfo::PlacementMacro *macro=nullptr;
        bool oversized=scenario=="oversized";
        const int targetSLR=scenario.find("bottom")==0?0:(scenario.find("top")==0?3:(scenario=="middle-upper"?2:1));
        const float offset=scenario=="negative-offset" || scenario=="bottom-macro" ? -8 : (oversized?400:8);
        if(scenario=="macro" || scenario=="negative-offset" || scenario=="bottom-macro" || scenario=="top-macro" || oversized) {
            std::string a="v0",b="v1";
            macro=new PlacementInfo::PlacementMacro("pair",0,PlacementInfo::PlacementMacro::PlacementMacroType_LUTLUTSeires);
            macro->addCell(design.getCell(a),DesignInfo::CellType_LUT1,0,0);
            macro->addCell(design.getCell(b),DesignInfo::CellType_LUT1,0,offset);
            macro->setWeight(2);macro->setAnchorLocationAndForgetTheOriginalOne(20,targetSLR*240+80);
            p.getPlacementUnits().push_back(macro);p.getPlacementMacros().push_back(macro);
            for(auto c:macro->getCells())p.getCellId2PlacementUnit()[c->getCellId()]=macro;
        }
        for(auto c:design.getCells()) {
            if(macro && (c->getName()=="v0" || c->getName()=="v1"))continue;
            auto pu=new PlacementInfo::PlacementUnpackedCell(c->getName(),p.getPlacementUnits().size(),c);pu->setWeight(1);
            bool endpoint=c->getName()=="source" || c->getName()=="sink";
            int index=endpoint?64:std::stoi(c->getName().substr(1));
            float x=110+index%5,y=150+index%7;
            if(scenario=="slr-tie")y=endpoint?620:(index<32?150:320)+index%7;
            else if(scenario=="inside") {x=20+index%5;y=320+index%7;}
            else if(scenario=="inside-above") {x=20+index%5;y=410+index%7;}
            else if(index<40) {
                int leftCount=scenario=="side-tie"?20:30;
                x=(index<leftCount?20:110)+index%5;y=320+index%7;
            } else if(scenario=="upper" || (scenario=="both-seams" && index>=52))y=620+index%7;
            if(targetSLR!=1)y=index<40?targetSLR*240+80+index%7:(targetSLR==0?320:(targetSLR==2?320:620))+index%7;
            if(scenario=="right")x=150-x;
            pu->setAnchorLocationAndForgetTheOriginalOne(x,y);if(endpoint)pu->setFixed();
            p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
            p.getCellId2PlacementUnit()[c->getCellId()]=pu;
        }
        p.updateCells2PlacementUnits();p.reloadNets();p.getCompatiblePlacementTable()->setBELTypeForCells(&design);
        design.updateFFControlSets();p.calculateNetNumDistributionOfPUs();p.createGridBins(5,5);p.updateElementBinGrid();p.buildSimpleTimingGraph();
        auto model=device.getPhysicalBoundaryModel();
        std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> before;
        for(auto pu:p.getPlacementUnits())before[pu]={pu->X(),pu->Y()};

        constexpr bool merged=true;
        PlacementTimingOptimizer timing(&p,cfg);timing.clusterCriticalPathsByPhysicalRegion(true);
        auto &prefs=p.getRegionPreferences();
        for(auto e:before)require(e.first->X()==e.second.first && e.first->Y()==e.second.second,"Deferred clustering moved PU");
        bool invalid=scenario=="slr-tie" || oversized;
        require(invalid?prefs.empty():prefs.size()==size_t(macro?63:64),"Wrong preference count");
        for(auto e:prefs) {
            require(model->getRegions().at(e.second.region).slr==targetSLR && e.second.guideY,"SLR vote failed");
            require(e.second.guideX==(scenario!="side-tie"),"Wrong HPIO tie");
            require(e.second.pullY==(targetSLR==0?-1:(targetSLR==3?1:0)),"Wrong Y direction");
        }
        if(scenario.find("budget")==0) {
            for(auto e:before)if(e.first->isFixed())e.first->setLocked();
            GlobalPlacer placer(&p,cfg);if(scenario!="budget")placer.setMacroLegalizationFixed();
            placer.GlobalPlacement_CLBElements(0,true,2,false,true,200,&timing);
            require(p.paperSLRStage(),"Zero GP consumed preparation");
            placer.GlobalPlacement_fixedCLB(1,.0002f);std::cout<<"INITIAL_FIXED_CLB_COMPLETED\n";
            for(auto e:before)require(e.first->X()==e.second.first && e.first->Y()==e.second.second,"Room-making before initialization");
            timing.spreadPaperBoundarySLR();
            for(int generation=0;generation<3;++generation) {
                if(generation) {
                    for(auto e:before)if(!e.first->isLocked())e.first->setAnchorLocationAndForgetTheOriginalOne(e.second.first,e.second.second);
                    p.updateElementBinGrid();timing.clusterCriticalPathsByPhysicalRegion();
                }
                require(p.paperSLRStage()!=merged,"Wrong stage after preparation");
                std::cout<<"BUDGET_GENERATION "<<generation<<"\n";
                placer.GlobalPlacement_CLBElements(5,generation!=0,2,false,scenario!="budget-nonstrict",200,&timing);
                require(!p.paperSLRStage() && !prefs.empty(),"GP left stale stage or lost preferences");
                for(auto e:prefs)checkFence(p,e.first);
                placer.GlobalPlacement_CLBElements(1,true,2,false,true,200,&timing);
            }
            p.clearRegionPreferences();cfg["BoundaryAwareClustering"]="false";cfg.erase("BoundaryClusteringStrategy");
            placer.GlobalPlacement_CLBElements(2,true,2,false,true,200,&timing);
        } else {
            timing.spreadPaperBoundarySLR();
            if(!invalid) {
                require(p.paperSLRStage()!=merged,"Wrong prepared phase");
                if(!merged)for(auto e:before)require(e.first->Y()==e.second.second,"SLR room-making moved Y");
                if(merged)for(auto e:prefs)checkFence(p,e.first);
                WirelengthOptimizer optimizer(&p,cfg,false);
                std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> prepared;
                for(auto e:prefs)prepared[e.first]={e.first->X(),e.first->Y()};
                auto probe=[&]() {
                    for(float d:{-10.f,-6.f,-3.f,0.f,3.f,6.f,10.f}) {
                        for(auto e:prefs) {
                            float x,y;require(p.regionAnchor(e.first,e.second,x,y),"Missing anchor");
                            x=e.second.guideX?x-e.second.pullX*d:e.first->X();
                            y=e.second.pullY?y-e.second.pullY*d:y+d;
                            e.first->setAnchorLocationAndForgetTheOriginalOne(x,y);
                        }
                        p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
                    }
                };
                probe();
                for(auto e:prefs)e.first->setFixed();optimizer.updateB2BNetWeight(.005f);
                for(auto e:prefs)e.first->setUnfixed();
                for(auto e:prepared)e.first->setAnchorLocationAndForgetTheOriginalOne(e.second.first,e.second.second);
                p.updateElementBinGrid();timing.advancePaperBoundaryStage();
                require(!p.paperSLRStage(),"Transition failed");
                p.clipPaperRegionLocations();for(auto e:prefs)checkFence(p,e.first);
                std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> after;
                for(auto e:prefs)after[e.first]={e.first->X(),e.first->Y()};
                timing.advancePaperBoundaryStage();timing.spreadPaperBoundarySLR();
                for(auto e:after)require(e.first->X()==e.second.first && e.first->Y()==e.second.second,"Repeated preparation moved PU");
                probe();
                if(scenario=="qp") {
                    optimizer.GlobalPlacementQPSolve(.005f,true,false,false,true,false,1.f,nullptr);
                    p.clipPaperRegionLocations();for(auto e:prefs)checkFence(p,e.first);
                }
                spreadOutward(p,true);spreadOutward(p,false);
                auto first=prefs.begin()->first;
                if(scenario=="side-tie") {
                    first->setAnchorLocationAndForgetTheOriginalOne(110,first->Y());
                    p.clipPaperRegionLocation(first);require(first->X()==110,"Tie acquired X fence");
                }
                p.clearRegionPreferences();first->setAnchorLocationAndForgetTheOriginalOne(110,620);
                require(!p.clipPaperRegionLocation(first),"Cleared preference still fenced");
            }
        }
        for(auto e:before)if(e.first->isFixed())require(e.first->X()==e.second.first && e.first->Y()==e.second.second,"Fixed endpoint moved");
        if(macro) {std::string name="v1";require(macro->getCells().size()==2 && macro->getCellOffsetYInMacro(design.getCell(name))==offset,"Macro offsets changed");}
        std::ofstream out(argv[3]);out<<"scenario\tpassed\n"<<scenario<<"\t1\n";
        std::cout<<"PASS "<<scenario<<'\n';return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}
