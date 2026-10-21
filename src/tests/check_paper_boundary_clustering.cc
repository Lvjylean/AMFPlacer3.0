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
        const bool crTest=scenario.find("cr-")==0;
        if(crTest)scenario=scenario.substr(3);
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
        const bool stageModeTest = scenario == "stage-mode" || scenario == "stage-inactive";
        const bool deferInitialSLRSpreading = scenario.find("budget") == 0 || stageModeTest || crTest;
        PlacementTimingOptimizer timing(&p,cfg);
        if (scenario != "stage-inactive") timing.clusterCriticalPathsByPhysicalRegion(deferInitialSLRSpreading);
        auto &prefs=p.getRegionPreferences();size_t moved=0;
        if(crTest) {
            require(p.crBoundaryClusteringEnabled() && !prefs.empty(),"CR strategy failed to dispatch");
            const auto shared=prefs.begin()->second;
            const auto &r=model->getRegions().at(shared.region);
            const int expectedY=targetSLR==0?-1:(targetSLR==3?1:(shared.clusterMeanY<=(r.y0+r.y1)*.5?1:-1));
            // Independent oracle from actual device geometry, including all CR
            // rows of the target die, not the new selection helper.
            float expectedAnchorY=expectedY>0?1e10f:-1e10f;
            float expectedAnchorX=shared.pullX<0?-1e10f:1e10f;
            for(auto row:device.getClockRegions())for(auto cr:row)if(cr) {
                float cx=(cr->getLeft()+cr->getRight())*.5f,cy=(cr->getBottom()+cr->getTop())*.5f;
                if(cy<=r.y0||cy>=r.y1)continue;
                expectedAnchorY=expectedY>0?std::min(expectedAnchorY,cy):std::max(expectedAnchorY,cy);
                if(cx>r.x0&&cx<r.x1)
                    expectedAnchorX=shared.pullX<0?std::max(expectedAnchorX,cx):std::min(expectedAnchorX,cx);
            }
            for(auto entry:prefs) {
                const auto &q=entry.second;
                require(q.crEdgeAnchor&&q.cluster==shared.cluster&&q.clusterMeanY==shared.clusterMeanY,
                        "Cluster average/identity was changed per PU");
                require(q.pullY==expectedY&&q.crAnchorY==expectedAnchorY&&q.crAnchorRow==shared.crAnchorRow,
                        "Members chose different Y edges or region center");
                require(q.guideX==(scenario!="side-tie"),"CR mode changed side-majority policy");
                if(q.guideX)require(q.crAnchorX==expectedAnchorX,"HPIO anchor is not its nearest CR column");
            }
            WirelengthOptimizer optimizer(&p,cfg,false);
            // Exercise actual QP pseudo-nets before and after the shared anchor.
            // The report records the resulting nonzero X/Y solver terms.
            for(bool slr:{true,false}) {
                p.setPaperSLRStage(slr);
                for(int beyond:{-1,1}) {
                    for(auto entry:prefs) {
                        float x,y;require(p.regionAnchor(entry.first,entry.second,x,y),"Missing CR anchor");
                        entry.first->setAnchorLocationAndForgetTheOriginalOne(
                            entry.second.guideX?x+beyond*entry.second.pullX*10:x,
                            y+beyond*entry.second.pullY*10);
                    }
                    p.updateElementBinGrid();optimizer.updateB2BNetWeight(.005f);
                    require(!prefs.empty(),"QP dropped feasible CR preferences");
                    for(auto entry:prefs)require(entry.second.crAnchorY==shared.crAnchorY&&
                        entry.second.pullY==shared.pullY&&entry.second.clusterMeanY==shared.clusterMeanY,
                        "QP/refresh recomputed cluster edge after crossing the center");
                }
            }
            // Rebuild the same cluster on the other half of an interior SLR.
            if(targetSLR==1) {
                const double oldMean=shared.clusterMeanY;
                for(auto pu:p.getPlacementUnits())if(!pu->isFixed()&&!pu->isLocked())
                    pu->setAnchorLocationAndForgetTheOriginalOne(20,expectedY>0?r.y1-40:r.y0+40);
                p.updateElementBinGrid();timing.clusterCriticalPathsByPhysicalRegion(true);
                require(!prefs.empty(),"Rebuild lost cluster");
                require(prefs.begin()->second.pullY==-expectedY && prefs.begin()->second.clusterMeanY!=oldMean,
                        "Rebuild did not recompute the cluster-wide average and edge");
            }
            std::ofstream out(argv[3]);out<<"PASS CR cluster-wide anchor, real geometry, QP stop and rebuild\n";
            std::cout<<"PASS cr-"<<scenario<<'\n';return 0;
        }
        if (stageModeTest) {
            const bool active = scenario != "stage-inactive";
            const bool merged = p.paperBoundaryMergedStages();
            require(active == p.paperBoundaryClusteringEnabled(), "Fixture active-mode mismatch");
            for (auto entry:before) {
                require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,
                        "Deferred preparation moved a PU");
                if (entry.first->isFixed()) entry.first->setLocked();
            }
            GlobalPlacer placer(&p,cfg);
            placer.GlobalPlacement_fixedCLB(1,.0002f);
            for (auto entry:before)
                require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,
                        "Boundary preparation ran inside fixedCLB initialization");
            WirelengthOptimizer optimizer(&p,cfg,false);
            for (int generation=0; generation<3; ++generation) {
                if (generation && active) {
                    for (auto entry:before) if (!entry.first->isFixed() && !entry.first->isLocked())
                        entry.first->setAnchorLocationAndForgetTheOriginalOne(entry.second.first,entry.second.second);
                    p.updateElementBinGrid();timing.clusterCriticalPathsByPhysicalRegion();
                }
                std::cout << "MODE_GENERATION " << generation << '\n';
                const bool pending = p.paperSLRStage();
                placer.GlobalPlacement_CLBElements(0,true,2,false,true,200,&timing);
                require(p.paperSLRStage()==pending,"Zero budget consumed boundary preparation");
                if (generation==0) timing.spreadPaperBoundarySLR();
                if (active) {
                    require(!prefs.empty(),"Boundary preferences missing");
                    require(p.paperSLRStage()==!merged,"Wrong boundary phase after preparation");
                    // Put movable targets away from both centers to exercise
                    // actual X/Y pseudo-net assembly, not just phase labels.
                    for (auto entry:prefs) {
                        float x,y;require(p.regionAnchor(entry.first,entry.second,x,y),"Missing anchor");
                        entry.first->setAnchorLocationAndForgetTheOriginalOne(x-entry.second.pullX*10,y-10);
                    }
                    p.updateElementBinGrid();optimizer.updateB2BNetWeight(.005f);
                    std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> prepared;
                    for (auto pu:p.getPlacementUnits())prepared[pu]={pu->X(),pu->Y()};
                    if (merged) {
                        timing.spreadPaperBoundarySLR();timing.advancePaperBoundaryStage();
                        for (auto entry:prepared)require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,
                                                        "Merged preparation repeated without rebuild");
                    }
                } else require(!merged && prefs.empty(),"Inactive selector enabled boundary clustering");
                placer.GlobalPlacement_CLBElements(3,generation!=0,2,false,true,200,&timing);
                require(!p.paperSLRStage(),"SLR stage left active after GP");
                for (auto entry:prefs)checkFence(p,entry.first);
                optimizer.updateB2BNetWeight(.005f);
                placer.GlobalPlacement_CLBElements(1,true,2,false,true,200,&timing);
                for (auto entry:before)if(entry.first->isLocked())
                    require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,
                            "Locked static endpoint moved");
            }
            if (active) {
                // Both fixed-only and locked-only PUs remain in the preference
                // map, but neither may receive a pseudo-net or be projected.
                auto a=prefs.begin()->first;auto b=std::next(prefs.begin())->first;
                a->setFixed();b->setLocked();
                auto ax=a->X(),ay=a->Y(),bx=b->X(),by=b->Y();
                p.refreshRegionPreferences();require(prefs.count(a)&&prefs.count(b),"Fixed/locked preferences erased");
                require(!p.clipPaperRegionLocation(a)&&!p.clipPaperRegionLocation(b),"Fixed/locked projection attempted");
                optimizer.updateB2BNetWeight(.005f);
                require(a->X()==ax&&a->Y()==ay&&b->X()==bx&&b->Y()==by,"Fixed/locked PU moved");
                p.clearRegionPreferences();require(prefs.empty()&&!p.paperSLRStage(),"Stage reset failed");
            }
            std::ofstream out(argv[3]);out<<"pu\tx\ty\n";
            for(auto pu:p.getPlacementUnits())out<<pu->getName()<<'\t'<<pu->X()<<'\t'<<pu->Y()<<'\n';
            std::cout<<"PASS "<<scenario<<'\n';return 0;
        }
        for(auto entry:before) {
            auto pu=entry.first;
            require(pu->Y()==entry.second.second,"SLR room-making moved Y or ran HPIO spreading too early");
            require(model->getRegions()[model->regionAt(pu->X(),pu->Y())].slr==
                    model->getRegions()[model->regionAt(entry.second.first,entry.second.second)].slr,"SLR spreading crossed die");
            if(pu->isFixed())require(pu->X()==entry.second.first,"Fixed endpoint moved");
            if(deferInitialSLRSpreading)
                require(pu->X()==entry.second.first,"Deferred clustering changed X before initialization");
            moved+=pu->X()!=entry.second.first;
        }
        if(scenario=="slr-tie" || oversized)require(prefs.empty(),"Invalid majority/macro geometry accepted");
        else {
            require(prefs.size()==size_t(macro?63:64),"Long-path group lost movable units");
            require(p.paperSLRStage(),"Initial phase is not SLR");
            for(auto entry:prefs) {
                auto pref=entry.second;
                require(model->getRegions().at(pref.region).slr==targetSLR && pref.guideY,"SLR-first vote failed");
                require(pref.guideX==(scenario!="side-tie"),"Wrong HPIO tie handling");
                require(pref.pullX==(scenario=="side-tie"?0:(scenario=="right"?1:-1)),"Wrong fixed HPIO pull direction");
                require(pref.pullY==(targetSLR==0?-1:(targetSLR==3?1:0)),
                        "SLR rule depends on initial PU location rather than target geometry");
            }
            if(scenario.find("budget")==0) {
                // Exercise the real GP driver and solver, not a simulated
                // counter: every cluster generation must get two budgets.
                for(auto entry:before)if(entry.first->isFixed())entry.first->setLocked();
                GlobalPlacer placer(&p,cfg);
                const bool early=scenario!="budget";
                if(early)placer.setMacroLegalizationFixed();
                const int budget=early?5:2;
                for(int generation=0;generation<3;++generation) {
                    if(generation) {
                        for(auto entry:before)if(!entry.first->isLocked())
                            entry.first->setAnchorLocationAndForgetTheOriginalOne(entry.second.first,entry.second.second);
                        p.updateElementBinGrid();
                        timing.clusterCriticalPathsByPhysicalRegion();
                        size_t movedResidents=0;
                        for(auto entry:before)movedResidents+=entry.first->X()!=entry.second.first;
                        require(movedResidents>0,"Later generation lost immediate X room-making");
                    }
                    require(p.paperSLRStage() && !prefs.empty(),"Rebuild did not restart SLR");
                    std::cout<<"BUDGET_GENERATION "<<generation<<"\n";
                    placer.GlobalPlacement_CLBElements(0,true,2,false,true,200,&timing);
                    require(p.paperSLRStage(),"Zero-iteration call consumed SLR budget");
                    if(generation==0) {
                        placer.GlobalPlacement_fixedCLB(1,.0002f);
                        std::cout<<"INITIAL_FIXED_CLB_COMPLETED\n";
                        require(p.paperSLRStage(),"Fixed CLB pass consumed SLR budget");
                        // This fixture consists of LUT/FF PUs: all six real
                        // initialization QPs must retain the unspread positions.
                        for(auto entry:before)
                            require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,
                                    "X room-making occurred before initialization completed");
                        timing.spreadPaperBoundarySLR();
                        size_t movedResidents=0;
                        for(auto entry:before) {
                            require(entry.first->Y()==entry.second.second,"SLR X room-making changed Y");
                            if(entry.first->isLocked())require(entry.first->X()==entry.second.first,"Locked endpoint moved");
                            movedResidents+=entry.first->X()!=entry.second.first;
                        }
                        require(movedResidents>0,"Deferred X room-making did not execute at SLR entry");
                        std::cout<<"INITIAL_SLR_SPREAD_AFTER_FIXED_CLB moved="<<movedResidents<<"\n";
                    }
                    placer.GlobalPlacement_CLBElements(budget,generation!=0,2,false,
                                                       scenario!="budget-nonstrict",200,&timing);
                    require(!p.paperSLRStage(),"Normal GP did not finish the SLR budget");
                    require(!prefs.empty(),"GP erased valid preferences");
                    for(auto entry:prefs)checkFence(p,entry.first);
                    // With no rebuild, another GP call must remain HPIO-only.
                    placer.GlobalPlacement_CLBElements(1,true,2,false,true,200,&timing);
                    require(!p.paperSLRStage(),"Ordinary continuation restarted SLR");
                }
                p.clearRegionPreferences();cfg["BoundaryAwareClustering"]="false";
                cfg.erase("BoundaryClusteringStrategy");
                placer.GlobalPlacement_CLBElements(2,true,2,false,true,200,&timing);
                for(auto entry:before)if(entry.first->isFixed())
                    require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,"Fixed endpoint moved");
                std::ofstream out(argv[3]);out<<"scenario\tpassed\n"<<scenario<<"\t1\n";
                std::cout<<"PASS "<<scenario<<'\n';return 0;
            }
            WirelengthOptimizer optimizer(&p,cfg,false);
            optimizer.updateB2BNetWeight(.005f);
            const size_t originalSize=prefs.size();
            for(auto entry:prefs)entry.first->setFixed();
            p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
            require(prefs.size()==originalSize && p.paperSLRStage(),"Temporary fixation retired/advanced preferences");
            for(auto entry:prefs)entry.first->setUnfixed();
            timing.conductStaticTimingAnalysis();optimizer.updateB2BNetWeight(.005f);
            if(scenario=="qp")optimizer.GlobalPlacementQPSolve(.005f,true,false,false,true,false,1.0f,nullptr);
            // The production ordinary-spreading worker must restore SLR bounds.
            spreadOutward(p,false);
            // X is still free during phase 1 (including side-tie clusters).
            auto first=prefs.begin()->first;float oldY=first->Y();
            first->setAnchorLocationAndForgetTheOriginalOne(scenario=="right"?20:110,oldY);
            float freeX=first->X();p.clipPaperRegionLocation(first);
            require(first->X()==freeX,"SLR phase prematurely imposed an HPIO fence");
            p.updateElementBinGrid();
            optimizer.updateB2BNetWeight(.005f); // Clipped at die edge: Y anchors must remain active.
            // Verify the original distance/DSPCritical gates while SLR anchors
            // are active, then restore locations before HPIO room-making.
            std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> beforeThresholdProbe;
            for(auto entry:prefs)beforeThresholdProbe[entry.first]={entry.first->X(),entry.first->Y()};
            for(float offset : {-10.f,-6.f,-3.f,0.f,3.f,6.f,10.f}) {
                for(auto &entry:prefs) {
                    float x,y;require(p.regionAnchor(entry.first,entry.second,x,y),"Missing SLR probe anchor");
                    entry.first->setAnchorLocationAndForgetTheOriginalOne(entry.first->X(),y+offset);
                }
                p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
            }
            for(auto entry:beforeThresholdProbe)
                entry.first->setAnchorLocationAndForgetTheOriginalOne(entry.second.first,entry.second.second);
            p.updateElementBinGrid();
            timing.advancePaperBoundaryStage();require(!p.paperSLRStage(),"No HPIO phase transition");
            require(prefs.size()==originalSize,"Phase transition erased valid preferences");
            std::map<PlacementInfo::PlacementUnit *,std::pair<float,float>> afterTransition;
            for(auto entry:prefs)afterTransition[entry.first]={entry.first->X(),entry.first->Y()};
            timing.advancePaperBoundaryStage();
            for(auto entry:afterTransition)require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,"Repeated stage advance repeated expansion");
            optimizer.updateB2BNetWeight(.005f);
            if(scenario=="qp")optimizer.GlobalPlacementQPSolve(.005f,true,false,false,true,false,1.0f,nullptr);
            spreadOutward(p,true);spreadOutward(p,false);
            if(scenario=="side-tie") {
                first->setAnchorLocationAndForgetTheOriginalOne(110,first->Y());p.updateElementBinGrid();p.refreshRegionPreferences();p.clipPaperRegionLocation(first);
                require(prefs.size()==originalSize && first->X()==110,"Side tie acquired X restriction after phase transition");
            }
            // Move beyond each HPIO center in its original X direction.
            // X must not reverse; Y is exactly at its anchor for this check.
            for(auto &entry:prefs) {
                auto pu=entry.first;float x,y;require(p.regionAnchor(pu,entry.second,x,y),"Missing anchor");
                pu->setAnchorLocationAndForgetTheOriginalOne(entry.second.guideX?x+entry.second.pullX*2:pu->X(),y);
            }
            p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
            // No Y anchor may reappear after the HPIO transition, even when
            // moving beyond the old distance thresholds. X must not reverse.
            for(float offset : {-10.f,-6.f,-3.f,0.f,3.f,6.f,10.f}) {
                for(auto &entry:prefs) {
                    float x,y;require(p.regionAnchor(entry.first,entry.second,x,y),"Missing Y probe anchor");
                    entry.first->setAnchorLocationAndForgetTheOriginalOne(
                        entry.second.guideX?x+entry.second.pullX*2:entry.first->X(),y+offset);
                }
                p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
            }
            // Probe arrival and later re-entry at the macro-aware phase edge.
            // The outer die edge is avoided: use the edge toward another SLR.
            for (bool slrProbe : {true, false}) {
                p.setPaperSLRStage(slrProbe);
                for (float delta : {1.f, 0.f, -1.f, 1.f}) {
                    for (auto entry : prefs) {
                        float l,r,b,t;
                        require(p.paperRegionBounds(entry.first,entry.second,!slrProbe,l,r,b,t),"Missing entry bounds");
                        float x=(l+r)*.5f,y=(b+t)*.5f;
                        if (slrProbe) y=targetSLR==3 ? b-delta : t+delta;
                        else if (entry.second.guideX) x=entry.second.pullX<0 ? r+delta : l-delta;
                        entry.first->setAnchorLocationAndForgetTheOriginalOne(x,y);
                    }
                    p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
                }
            }
            RegionCapacityTracker capacity(&p);std::map<PlacementInfo::PlacementUnit *,int> targets;
            for(auto entry:prefs)targets[entry.first]=entry.second.region;
            require(capacity.assignTargets(targets,true),"Capacity reservation failed");auto usage=capacity.getUsage();
            require(!capacity.assignTargets(targets,true) && capacity.getUsage()==usage,"Duplicate reservation changed capacity");
            // No permanent fence survives the existing clear/rebuild lifecycle.
            p.clearRegionPreferences();require(!p.paperSLRStage(),"Clear left stale stage state");
            first->setAnchorLocationAndForgetTheOriginalOne(110,620);
            require(!p.clipPaperRegionLocation(first) && first->X()==110 && first->Y()==620,"Cleared preference still constrained PU");
        }
        for(auto entry:before)if(entry.first->isFixed())require(entry.first->X()==entry.second.first && entry.first->Y()==entry.second.second,"Fixed endpoint moved");
        if(scenario=="joint" || scenario=="side-tie" || scenario=="macro")require(moved>0,"SLR incoming group did not make room");
        if(macro) {
            std::string name="v1";
            require(macro->getCells().size()==2 && macro->getCellOffsetYInMacro(design.getCell(name))==(offset),"Macro offsets changed");
        }
        std::ofstream out(argv[3]);out<<"scenario\tmoved_residents\tpassed\n"<<scenario<<'\t'<<moved<<"\t1\n";
        std::cout<<"PASS "<<scenario<<'\n';return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}
