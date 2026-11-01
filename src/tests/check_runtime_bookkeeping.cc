#include "PlacementInfo.h"
#include "BoundaryAuditCounts.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <omp.h>
static void need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
struct Pin { int id; int getElementIdInType() const {return id;} };
struct Edge { Pin a,b; const Pin *getSourcePin()const{return &a;} const Pin *getSinkPin()const{return &b;} };
static void counts(const PhysicalBoundaryModel &model) {
    std::vector<PlacementInfo::Location> pins{{100,237},{100,243},{100,239.5},{158,100},{170,100},{-10,-10},{-10,100},{150,237},{170,800}};
    std::mt19937 gen(1234);
    for(int i=0;i<2000;++i)pins.push_back({float(gen()%30000)/100,float(gen()%96000)/100});
    std::vector<Edge> storage;storage.reserve(30000);
    for(int i=0;i<30000;++i)storage.push_back({{int(gen()%pins.size())},{int(gen()%pins.size())}});
    for(int a=0;a<9;++a)for(int b=0;b<9;++b)storage.push_back({{a},{b}});
    std::vector<Edge*> edges;for(auto &e:storage)edges.push_back(&e);
    for(size_t n:{size_t(0),size_t(30),edges.size()}) {
        std::vector<Edge*> subset(edges.begin(),edges.begin()+n);
        boundary_audit::Counts expected;
        for(auto e:subset) {
            auto a=pins[e->a.id],b=pins[e->b.id];
            if((a.X<-5&&a.Y<-5)||(b.X<-5&&b.Y<-5))continue;
            int s=model.crossingCount(a.X,a.Y,b.X,b.Y,"SLR"),i=model.crossingCount(a.X,a.Y,b.X,b.Y,"IO");
            expected.slr+=s;expected.io+=i;expected.crossed+=(s+i)>0;++expected.valid;
        }
        for(int threads:{1,12}) {
            omp_set_num_threads(threads);const auto actual=boundary_audit::count(subset,pins,model);
            need(actual.slr==expected.slr&&actual.io==expected.io&&actual.crossed==expected.crossed&&actual.valid==expected.valid,"serial/parallel audit mismatch");
        }
    }
    std::cout<<"PASS boundary counts: empty/small/30081 edges, invalid pins, seam endpoints, multiseam; 1/12 threads exact\n";
}
int main(int argc,char **argv){try{
    need(argc==3,"usage: fixture.json NEW_OUTPUT");auto out=std::filesystem::path(argv[2]);need(std::filesystem::create_directory(out),"refuse overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["jobs"]="12";cfg["SllPlanningEnable"]="false";cfg["SllTimingCorrectionEnable"]="false";cfg["SllRouteConstraintEnable"]="false";
    cfg["dumpDirectory"]=out.string();cfg["DetailedDiagnosticsEnable"]="false";
    DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo pi(&design,&device,cfg);
    auto cells=design.getCells();need(cells.size()==4,"four FF fixture");
    auto macro=new PlacementInfo::PlacementMacro("pair",0,PlacementInfo::PlacementMacro::PlacementMacroType_LUTLUTSeires);
    for(int i=0;i<2;++i){macro->addCell(cells[i],cells[i]->getCellType(),float(i)*.125f,float(i)*.375f);pi.getCellId2PlacementUnit()[cells[i]->getCellId()]=macro;}
    pi.getPlacementUnits().push_back(macro);pi.getPlacementMacros().push_back(macro);
    for(size_t i=2;i<cells.size();++i){auto u=new PlacementInfo::PlacementUnpackedCell(cells[i]->getName(),pi.getPlacementUnits().size(),cells[i]);pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[cells[i]->getCellId()]=u;}
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);
    int checks=0;
    for(float bin:{5.f,2.5f,2.f}) {
        for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50,230);
        pi.createGridBins(bin,bin);
        for(int round=0;round<12;++round) {
            for(auto u:pi.getPlacementUnits())u->setAnchorLocationAndForgetTheOriginalOne(50.f+round*2.125f,230.f+round*.625f+u->getId()*2.5f);
            for(bool full:{true,false})for(bool bypass:{false,true}) {
                // The unchanged stable-bucket updater is an independent oracle.
                pi.updateElementBinGridBatched(full,bypass);
                pi.verifyElementBinGrid(full,bypass);++checks;
            }
        }
    }
    need(checks==144,"all grid cases");
    // Locked and exclusive insertion must preserve integer truncation and members.
    auto bin=pi.getGlobalBinGrid().at(0).at(0);bin->reset();
    bin->addCell(cells[0],1.75f);bin->addCell(cells[1],2.25f);auto locked=bin->gridAuditState();bin->reset();
    bin->addGridCellExclusive(cells[0],1.75f,false);bin->addGridCellExclusive(cells[1],2.25f,false);
    need(locked==bin->gridAuditState(),"exclusive insert/truncation");
    counts(*device.getPhysicalBoundaryModel());
    std::cout<<"PASS 144 grid comparisons: all bin counters/members, cell coordinates/assignments, full/bypass, 5/2.5/2 grids, macro offsets; legacy allocation order\n";
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
