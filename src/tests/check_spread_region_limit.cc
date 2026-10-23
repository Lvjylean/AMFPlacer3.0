#include "GeneralSpreader.h"
#include "simpleJSON.h"
#include <memory>
#include <stdexcept>
#include <iostream>
using Bin = PlacementInfo::PlacementBinInfo;
using Region = GeneralSpreader::SpreadRegion;
static void require(bool ok, const char *msg) { if (!ok) throw std::runtime_error(msg); }
struct Grid {
    std::unique_ptr<DeviceInfo::DeviceTile> tile;
    std::vector<std::unique_ptr<Bin>> bins;
    std::vector<std::unique_ptr<DeviceInfo::DeviceSite>> sites;
    std::vector<std::vector<Bin *>> rows;
    Grid(int h, int w, PlacementInfo::CompatiblePlacementTable *table, bool gap = false) : rows(h) {
        std::string tileName="TEST_X0Y0",tileType="TEST";
        tile.reset(new DeviceInfo::DeviceTile(tileName,tileType,nullptr,0));
        for (int y=0;y<h;++y) for(int x=0;x<w;++x) {
            bins.emplace_back(new Bin("SLICEL_CARRY8",x,x+1,y,y+1,y,x,table));
            rows[y].push_back(bins.back().get());
            if (!gap || x==0 || x==3) {
                std::string name="SLICE_X"+std::to_string(x)+"Y"+std::to_string(y), type="SLICEL";
                sites.emplace_back(new DeviceInfo::DeviceSite(name,type,tile.get(),x+.5f,y+.5f,0,0,y*w+x));
                bins.back()->addSiteIntoBin(sites.back().get());
                require(bins.back()->getCapacity()>0,"fixture lacks capacity");
            }
        }
    }
};
static size_t expand(Region &r, std::set<Bin *> &covered, unsigned cap, bool simple=false) {
    unsigned iterations=0;
    while (r.getOverflowRatio()>.8f && (simple?r.simpleFindExpandDirection(covered):r.smartFindExpandDirection(covered))) {
        auto before=r.getBinsInRegion().size();
        if(simple)r.simpleExpand(covered);else r.smartExpand(covered);
        require(r.getBinsInRegion().size()>before,"expansion made no progress");
        require(r.getBinsInRegion().size()<=cap,"expansion exceeded cap");
        require(++iterations<=cap,"expansion did not terminate");
    }
    return r.getBinsInRegion().size();
}
int main(int argc,char **argv) {
    try {
        require(argc==2,"usage: checkSpreadRegionLimit synthetic-config.json");
        auto cfg=parseJSONFile(argv[1]);
        DeviceInfo device(cfg,cfg["device"]); DesignInfo design(cfg,&device);
        PlacementInfo::CompatiblePlacementTable table(cfg["cellType2fixedAmo file"],
            cfg["cellType2sharedCellType file"],cfg["sharedCellType2BELtype file"],&design,&device);
        auto cell=design.getCells().at(0);
        unsigned checks=0; size_t maxSmart=0,maxSimple=0;
        for(bool simple:{false,true}) {
            for(unsigned cap:{1u,2u,199u,200u,201u}) {
                Grid g(1,250,&table);auto seed=g.rows[0][125];seed->addCell(cell,100000);
                Region r(seed,nullptr,g.rows,.8f,cap);std::set<Bin*> covered;
                srandom(17);require(expand(r,covered,cap,simple)==cap,"one-dimensional cap not reached exactly");++checks;
            }
            for(unsigned seedId=0;seedId<32;++seedId) {
                Grid g(25,25,&table);auto seed=g.rows[seedId%25][(seedId*7)%25];seed->addCell(cell,100000);
                Region r(seed,nullptr,g.rows,.8f,200);std::set<Bin*> covered;srandom(seedId);
                auto n=expand(r,covered,200,simple);require(n>1,"unexpectedly stopped at seed");
                (simple?maxSimple:maxSmart)=std::max(simple?maxSimple:maxSmart,n);++checks;
            }
        }
        { // Adding a complete ten-bin strip must not turn 200 into 210 (or 190 into 209).
            Grid g(30,30,&table);auto seed=g.rows[0][0];seed->addCell(cell,100000);
            Region r(seed,nullptr,g.rows,.8f,205);std::set<Bin*> covered;
            r.addBinRegion(0,0,1,9,covered);r.addBinRegion(18,1,0,9,covered);
            require(r.getBinsInRegion().size()==190,"bad strip fixture");
            require(expand(r,covered,205)==200,"whole-strip budget violated");++checks;
        }
        for(unsigned cap:{3u,4u}) { // Smart expansion skips two empty columns: they still count.
            Grid g(1,5,&table,true);auto seed=g.rows[0][0];seed->addCell(cell,100000);
            Region r(seed,nullptr,g.rows,.8f,cap);std::set<Bin*> covered;
            require(expand(r,covered,cap)==(cap==3?1u:4u),"empty-column jump ignored limit");++checks;
        }
        for(bool simple:{false,true}) {
            Grid g(1,10,&table);auto seed=g.rows[0][0];seed->addCell(cell,100000);
            Region r(seed,nullptr,g.rows,.8f,200);std::set<Bin*> covered{g.rows[0][1]};
            require(expand(r,covered,200,simple)==1,"crossed covered region");++checks;
        }
        {
            Grid g(1,5,&table);auto seed=g.rows[0][0];seed->addCell(cell,100000);
            Region r(seed,nullptr,g.rows,.8f,2);std::set<Bin*> covered;bool rejected=false;
            try { r.addBinRegion(0,0,1,3,covered); } catch(const std::logic_error &) { rejected=true; }
            require(rejected && r.getBinsInRegion().size()==1 && r.right()==0 && covered.empty(),
                    "oversize add did not fail atomically");++checks;
            rejected=false;try { Region bad(seed,nullptr,g.rows,.8f,0); } catch(const std::invalid_argument &) { rejected=true; }
            require(rejected,"zero limit accepted");++checks;
        }
        { // Stopping when capacity is sufficient still precedes the upper limit.
            Grid g(1,10,&table);auto seed=g.rows[0][0];seed->addCell(cell,1);
            Region r(seed,nullptr,g.rows,.8f,200);std::set<Bin*> covered;
            require(expand(r,covered,200)==2,"capacity-based stop changed");++checks;
        }
        for(bool simple:{false,true}) {
            Grid g(10,10,&table);auto seed=g.rows[5][5];seed->addCell(cell,100000);
            Region r(seed,nullptr,g.rows,.8f,1000000);std::set<Bin*> covered;
            require(expand(r,covered,1000000,simple)==100,"nonbinding cap prevented filling grid");++checks;
        }
        std::cout<<"PASS spread-region-limit checks="<<checks<<" max_smart_2d="<<maxSmart
                 <<" max_simple_2d="<<maxSimple<<" exact_200_1d=passed\n";
    } catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
