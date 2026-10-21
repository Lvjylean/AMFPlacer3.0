// Link against the frozen production PlacementInfo/DeviceInfo libraries.
#include "PlacementInfo.h"
#include "simpleJSON.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
static void require(bool yes,const char *why) {if(!yes)throw std::runtime_error(why);}
int main(int argc,char **argv) {try {
    require(argc==2,"Expected a small real-device fixture config");
    auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg.at("device"));
    DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
    auto cell=design.getCells().at(0);
    auto pu=new PlacementInfo::PlacementUnpackedCell(cell->getName(),0,cell);pu->setWeight(1);
    p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
    p.getCellId2PlacementUnit()[cell->getCellId()]=pu;
    const auto &regions=device.getPhysicalBoundaryModel()->getRegions();
    require(regions.size()>2,"Real device must have distinct physical regions");
    const auto &r=regions.front();float cx=(r.x0+r.x1)/2,cy=(r.y0+r.y1)/2;
    auto &hard=const_cast<HardRegionConstraints &>(p.getHardRegions());
    hard.enabled=true;
    HardRegionConstraints::Region whole;whole.name="static";whole.prefix="*";
    for(const auto &region:regions){whole.box.add(region.x0,region.y0);whole.box.add(region.x1,region.y1);}
    HardRegionConstraints::Region rp;rp.name="a";rp.prefix=cell->getName();
    rp.box.add(cx-5,cy-5);rp.box.add(cx+5,cy+5);hard.regions={whole,rp};
    PlacementInfo::RegionPreference pref;pref.region=0;pref.cluster=0;pref.guideX=true;pref.guideY=true;
    if(p.crBoundaryClusteringEnabled()) {
        pref.pullX=-1;
        require(p.initializeCRBoundaryAnchor(pref,cy),"Missing CR device geometry");
    }
    p.getRegionPreferences()[pu]=pref;p.setPaperSLRStage(false);
    pu->setAnchorLocationAndForgetTheOriginalOne(cx,cy);
    float l,rr,b,t;require(p.paperRegionBounds(pu,pref,true,l,rr,b,t),"Valid RP intersection rejected");
    require(l>=rp.box.lx && rr<=rp.box.hx && b>=rp.box.ly && t<=rp.box.hy,"Paper bounds ignore hard RP");
    float x,y;require(p.regionAnchor(pu,pref,x,y),"Feasible anchor rejected");
    require(p.hardRegionLocationLegal(pu,x,y),"Anchor outside hard RP");
    auto incompatible=pref;incompatible.region=regions.size()-1;
    require(!p.paperRegionBounds(pu,incompatible,true,l,rr,b,t),"Disjoint region accepted");
    pu->setAnchorLocationAndForgetTheOriginalOne(cx+25,cy+25);
    const bool clipping = !cfg.count("BoundaryRegionClipping") || cfg.at("BoundaryRegionClipping") == "true";
    if (clipping) require(p.clipPaperRegionLocation(pu),"Hard RP clipping did not run");
    else {
        const auto before = p.getRegionPreferences().at(pu);
        require(!p.clipPaperRegionLocation(pu),"Disabled clipping moved the PU");
        require(pu->X()==cx+25 && pu->Y()==cy+25,"Disabled clipping changed coordinates");
        require(p.getRegionPreferences().at(pu).region==before.region,"Disabled clipping removed preference");
        require(p.regionAnchor(pu,pref,x,y),"Disabled clipping removed anchor");
        // Normal hard-area projection remains active when soft clipping is off.
        x=pu->X();y=pu->Y();p.legalizeXYInArea(pu,x,y);
        pu->setAnchorLocationAndForgetTheOriginalOne(x,y);
    }
    require(p.hardRegionLocationLegal(pu,pu->X(),pu->Y()),"Clip undid hard RP projection");
    pu->setFixed();float fixedX=pu->X(),fixedY=pu->Y();
    require(!p.clipPaperRegionLocation(pu)&&pu->X()==fixedX&&pu->Y()==fixedY,"Fixed PU moved");
    // A separate movable fixture checks incremental RP bounds without changing
    // any existing fixed/static placement.
    auto other=new PlacementInfo::PlacementUnpackedCell(cell->getName(),1,cell);
    other->setAnchorLocationAndForgetTheOriginalOne(cx,cy);
    hard.enabled=false;auto &inc=const_cast<IncrementalContext &>(p.getIncrementalContext());
    inc.enabled=true;inc.minX=cx-3;inc.maxX=cx+3;inc.minY=cy-3;inc.maxY=cy+3;
    require(p.paperRegionBounds(other,pref,true,l,rr,b,t),"Incremental intersection rejected");
    require(l>=inc.minX&&rr<=inc.maxX&&b>=inc.minY&&t<=inc.maxY,"Incremental bounds ignored");
    inc.enabled=false;
    require(p.paperRegionBounds(other,pref,true,l,rr,b,t)&&l<cx-3&&rr>cx+3,"Ordinary region bounds changed");
    delete other;
    std::cout<<"PAPER_HARD_RP_INTEROP_PASS full/incremental/disjoint/fixed/ordinary clipping="<<clipping<<"\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
