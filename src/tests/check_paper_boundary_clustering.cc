#include "BoundaryAwareClusterer.h"
#include "WirelengthOptimizer.h"
#include "GeneralSpreader.h"
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
        const int targetSLR=scenario.find("bottom")==0?0:(scenario.find("top")==0?3:1);
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
            else if(index<40) {
                int leftCount=scenario=="side-tie"?20:30;
                x=(index<leftCount?20:110)+index%5;y=320+index%7;
            } else if(scenario=="upper" || (scenario=="both-seams" && index>=52))y=620+index%7;
            if(targetSLR!=1)y=index<40?targetSLR*240+80+index%7:(targetSLR==0?320:620)+index%7;
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
        PlacementTimingOptimizer timing(&p,cfg);timing.clusterCriticalPathsByPhysicalRegion();
        auto &prefs=p.getRegionPreferences();size_t moved=0;
        for(auto entry:before) {
            auto pu=entry.first;
            require(pu->Y()==entry.second.second,"SLR room-making moved Y or ran HPIO spreading too early");
            require(model->getRegions()[model->regionAt(pu->X(),pu->Y())].slr==
                    model->getRegions()[model->regionAt(entry.second.first,entry.second.second)].slr,"SLR spreading crossed die");
            if(pu->isFixed())require(pu->X()==entry.second.first,"Fixed endpoint moved");
            moved+=pu->X()!=entry.second.first;
        }
        if(scenario=="slr-tie" || oversized)require(prefs.empty(),"Invalid majority/macro geometry accepted");
        else {
            require(prefs.size()==size_t(macro?63:64),"Long-path group lost movable units");
            require(p.paperSLRStage(),"Initial phase is not SLR");
            int up=0,down=0;
            for(auto entry:prefs) {
                auto pref=entry.second;auto pu=entry.first;
                require(model->getRegions().at(pref.region).slr==targetSLR && pref.guideY,"SLR-first vote failed");
                require(pref.guideX==(scenario!="side-tie"),"Wrong HPIO tie handling");
                require(pref.pullX==(scenario=="side-tie"?0:(scenario=="right"?1:-1)),"Wrong fixed HPIO pull direction");
                up+=pref.pullY>0;down+=pref.pullY<0;
                if(model->getRegions().at(model->regionAt(before.at(pu).first,before.at(pu).second)).slr==targetSLR)require(pref.pullY==0,"SLR residents acquired center attraction");
            }
            if(scenario=="both-seams")require(up>0 && down>0,"Middle die cannot receive from both seams");
            if(scenario=="upper")require(down>0 && up==0,"Upper-die pull direction reversed");
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
            // Move beyond every target center in its original pull direction.
            // A QP refresh must not reverse that direction and pull it back.
            for(auto &entry:prefs) {
                auto pu=entry.first;float x,y;require(p.regionAnchor(pu,entry.second,x,y),"Missing anchor");
                pu->setAnchorLocationAndForgetTheOriginalOne(entry.second.guideX?x+entry.second.pullX*2:pu->X(),y);
            }
            p.updateElementBinGrid();p.refreshRegionPreferences();optimizer.updateB2BNetWeight(.005f);
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
