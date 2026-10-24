#include "GeneralSpreader.h"
#include "simpleJSON.h"
#include <iostream>
#include <memory>
#include <filesystem>
using Bin=die_spread::Bin;
using Region=GeneralSpreader::SpreadRegion;
static size_t checks=0;
static void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
struct Grid {
    std::unique_ptr<DeviceInfo::DeviceTile> tile;
    std::vector<std::unique_ptr<Bin>> storage;
    std::vector<std::unique_ptr<DeviceInfo::DeviceSite>> sites;
    die_spread::Grid rows;
    Grid(int h,int w,PlacementInfo::CompatiblePlacementTable *table,float pitch=1):rows(h) {
        std::string tn="TEST_X0Y0",tt="TEST";tile.reset(new DeviceInfo::DeviceTile(tn,tt,nullptr,0));
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            storage.emplace_back(new Bin("SLICEL_CARRY8",x,x+1,y*pitch,(y+1)*pitch,y,x,table));
            rows[y].push_back(storage.back().get());
            std::string n="SLICE_X"+std::to_string(x)+"Y"+std::to_string(y),t="SLICEL";
            sites.emplace_back(new DeviceInfo::DeviceSite(n,t,tile.get(),x+.5f,(y+.5f)*pitch,0,0,y*w+x));
            rows[y].back()->addSiteIntoBin(sites.back().get());
        }
    }
};
static Region *select(Bin *seed,Grid &g,std::set<Bin*> &covered,int lo,int hi,
                      die_spread::Failure failure={},bool simple=false,double demand=0,double capacity=10000) {
    return GeneralSpreader::selectDieFirstRegion(seed,nullptr,g.rows,.8f,200,covered,lo,hi,0,
                                                failure,{demand,capacity,capacity},simple);
}
int main(int argc,char **argv){try{
    check(argc==2,"usage: checkDieFirstSpreading fixture.json");
    auto cfg=parseJSONFile(argv[1]);DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);
    PlacementInfo::CompatiblePlacementTable table(cfg["cellType2fixedAmo file"],cfg["cellType2sharedCellType file"],
                                                cfg["sharedCellType2BELtype file"],&design,&device);
    auto cell=design.getCells().at(0);
    for(bool simple:{false,true})for(unsigned seed=0;seed<40;++seed){
        srandom(seed);Grid g(20,5,&table);auto b=g.rows[9][2];b->addCell(cell,2);std::set<Bin*> covered;
        std::unique_ptr<Region> r(select(b,g,covered,0,9,{},simple));
        check(r->decision.local,"crossed despite sufficient die-local supply");
        check(r->top()<=9&&r->bottom()>=0,"local region crossed row domain");
        check(r->getBinsInRegion().size()<=200,"local budget");
    }
    for(bool simple:{false,true}){
        Grid g(450,1,&table);auto b=g.rows[199][0];b->addCell(cell,10000);std::set<Bin*> covered;
        std::unique_ptr<Region> r(select(b,g,covered,0,199,{},simple,10000,200));
        check(!r->decision.local,"capacity shortage must unlock cross search");
        check(r->decision.localBins==200,"must first exhaust 200 local bins");
        check(r->decision.reason=="die_resource_capacity_shortage","nominal shortage reason");
        check(r->getBinsInRegion().size()<=200,"fallback appended beyond budget");
        check(covered.size()<=r->getBinsInRegion().size(),"failed local claims leaked into fallback");
        for(auto c:covered)check(r->isRegionOverlap(c->Y(),c->Y(),c->X(),c->X()),"covered bin outside fallback rectangle");
        check(r->getOverflowRatio()>.8,"residual overflow should remain permitted");
    }
    {Grid g(20,2,&table);auto b=g.rows[9][0];b->addCell(cell,100);std::set<Bin*> covered;
        std::unique_ptr<Region> r(select(b,g,covered,0,9,{},false,100,10000));
        check(r->decision.localBins<200&&!r->decision.local,"die exhausted before 200 must terminate");
        check(r->decision.reason=="local_capacity_search_exhausted","do not claim whole-die shortage");
    }
    {Grid g(20,2,&table);auto b=g.rows[9][0];b->addCell(cell,2);std::set<Bin*> covered;
        std::unique_ptr<Region> wider(select(b,g,covered,0,9,{true,false}));
        check(wider->decision.local&&wider->decision.boundedLocal,"residual first requires wider local writeback trial");
        check(wider->decision.localBins==20,"wider local must reach domain end");
        covered.clear();std::unique_ptr<Region> fallback(select(b,g,covered,0,9,{true,true}));
        check(!fallback->decision.local&&fallback->decision.reason=="local_writeback_overflow_after_bounded_search",
              "confirmed bounded-writeback residual must unlock fallback");
    }
    // Split a physical seam through a bin: capacity and demand must not leak
    // across it; an exact boundary point belongs to the lower die.
    {Grid g(2,1,&table,5);auto a=design.getCells().at(0),b=design.getCells().at(1);
        g.rows[0][0]->addCell(a,1);g.rows[0][0]->addCell(b,1);
        std::vector<PlacementInfo::Location> loc(design.getCells().size());loc[a->getCellId()]={.5f,2.5f};loc[b->getCellId()]={.5f,3};
        die_spread::GridView view(g.rows,&table,{2.5f},loc.size(),[&](int id){return loc[id];},[](int){return 1;});
        check(view.rows.size()==3,"seam bin not split");
        check(view.rowDie==std::vector<int>({0,1,1}),"split row die labels");
        check(view.rows[0][0]->contains(a)&&view.rows[1][0]->contains(b),"boundary cell membership");
        auto supply=view.supply();check(supply[0].nominal==1&&supply[1].nominal==1,"site supply double counted or leaked");
        check(supply[0].demand==1&&supply[1].demand==1,"split occupancy mismatch");
        view.update(b,.5,8);check(view.rows[2][0]->contains(b)&&view.rows[1][0]->getUtilization()==0,"view move update");
        check(g.rows[0][0]->getUtilization()==2,"private view modified global bins");
    }
    for(float offset:{-3.f,0.f,3.f}){
        die_spread::Range r;r.includeMember({10,20},9,offset);
        check(r.contains(9-offset)&&!r.contains(11-offset),"local macro offset guard");
    }
    {die_spread::Range r;r.includeMember({10,20},9,0);r.includeMember({10,20},12,3);
        check(r.contains(9)&&!r.contains(11)&&!r.contains(6),"existing multi-die macro membership must be preserved individually");}
    std::cout<<"PASS die-first region/view checks="<<checks<<"\n";
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
