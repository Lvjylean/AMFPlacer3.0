#include "GeneralSpreader.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
using PI=PlacementInfo;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static std::vector<std::map<std::string,std::string>> rows(std::filesystem::path path){
    std::ifstream f(path);check(bool(f),"missing spread diagnostic");std::string line;std::vector<std::string> hdr;
    std::vector<std::map<std::string,std::string>> out;bool first=true;
    while(std::getline(f,line)){std::istringstream in(line);std::string col;std::vector<std::string> v;
        while(std::getline(in,col,'\t'))v.push_back(col);if(first){hdr=v;first=false;continue;}
        check(v.size()==hdr.size(),"spread diagnostic column mismatch");std::map<std::string,std::string> r;
        for(size_t i=0;i<v.size();++i)r[hdr[i]]=v[i];out.push_back(r);}
    return out;
}
int main(int argc,char **argv){try{
    check(argc==3||argc==4,"usage: checkDieFirstSpreadingIntegration fixture.json NEW_OUTPUT [macro]");
    bool macroMode=argc==4;
    auto root=std::filesystem::path(argv[2]);check(std::filesystem::create_directory(root),"refuse output overwrite");
    auto cfg=parseJSONFile(argv[1]);cfg["jobs"]="2";cfg["SllReportDirectory"]=(root/"local").string();
    DeviceInfo dev(cfg,cfg["device"]);DesignInfo design(cfg,&dev);PI pi(&design,&dev,cfg);
    std::vector<float> seams;for(auto &b:dev.getPhysicalBoundaryModel()->getBoundaries())if(b.active&&b.kind=="SLR"&&b.orientation=='Y')seams.push_back(b.coordinate);
    std::sort(seams.begin(),seams.end());check(!seams.empty(),"multi-die fixture required");float seam=seams[0];
    auto cells=design.getCells();check(cells.size()==4,"four-cell synthetic fixture required");
    size_t begin=0;
    if(macroMode){
        auto m=new PI::PlacementMacro("synthetic-pair",0,PI::PlacementMacro::PlacementMacroType_LUTLUTSeires);
        for(int i=0;i<2;++i){m->addCell(cells[i],cells[i]->getCellType(),0,float(i));pi.getCellId2PlacementUnit()[cells[i]->getCellId()]=m;}
        m->setAnchorLocationAndForgetTheOriginalOne(50,seam+5);m->recordSpreadLocatin();
        m->setAnchorLocationAndForgetTheOriginalOne(50,seam-1.5f);
        pi.getPlacementUnits().push_back(m);pi.getPlacementMacros().push_back(m);begin=2;
    }
    for(size_t i=begin;i<cells.size();++i){auto c=cells[i];
        auto u=new PI::PlacementUnpackedCell(c->getName(),pi.getPlacementUnits().size(),c);
        u->setAnchorLocationAndForgetTheOriginalOne(50,seam+5);u->recordSpreadLocatin();
        u->setAnchorLocationAndForgetTheOriginalOne(50,seam-.5f);
        pi.getPlacementUnits().push_back(u);pi.getPlacementUnpackedCells().push_back(u);pi.getCellId2PlacementUnit()[c->getCellId()]=u;
    }
    check(pi.getPlacementUnits().size()==(macroMode?3:4),"synthetic PU count");
    pi.updateCells2PlacementUnits();pi.reloadNets();pi.setBELTypeForCells(&design);
    // Synthetic demand forces a small local overflow without running a placer flow.
    for(auto &v:pi.getCompatiblePlacementTable()->getcellId2Occupation())v=100;
    pi.createGridBins(5,5);pi.updateElementBinGrid(false);
    std::string type="SLICEL_LUT";
    {GeneralSpreader spread(&pi,cfg,type,0,.8,false);spread.spreadPlacementUnits(.2,false,-10,200);}
    auto local=rows(root/"local/spreading-regions.tsv");check(!local.empty(),"fixture must exercise spreading");
    size_t localCount=0;
    for(auto&r:local){check(std::stoul(r["bins"])<=200&&std::stoul(r["local_bins"])<=200,"production region budget");
        if(r["phase"]=="die_local"){++localCount;check(r["crossing_cells"]=="0","production local writeback crossed");}}
    check(localCount>0,"no production local writeback");
    auto pass=rows(root/"local/spreading-passes.tsv");check(pass.size()<=10,"inner iteration budget changed");
    // Immovable synthetic load: capacity search may succeed, but actual local
    // writeback cannot repair the occupied bin. Must first try the wider local
    // region, then log the bounded-writeback reason rather than inventing a lack
    // of total die resources. This also checks fixed objects never move.
    cfg["SllReportDirectory"]=(root/"fixed").string();
    for(auto pu:pi.getPlacementUnits()){pu->setAnchorLocationAndForgetTheOriginalOne(50,seam-(dynamic_cast<PI::PlacementMacro*>(pu)?1.5f:.5f));pu->setFixed();}
    pi.updateElementBinGrid(false);
    {GeneralSpreader spread(&pi,cfg,type,0,.8,false);spread.spreadPlacementUnits(1,false,-10,200);}
    auto fixed=rows(root/"fixed/spreading-regions.tsv");bool wider=false,fallback=false;
    for(auto&r:fixed){wider|=r["reason"]=="wider_local_writeback_trial";fallback|=r["reason"]=="local_writeback_overflow_after_bounded_search";
        check(r["moved_pus"]=="0","fixed synthetic PUs moved");check(std::stoul(r["bins"])<=200,"fixed fallback budget");}
    check(wider&&fallback,"fixed geometry did not exercise bounded local fallback");
    for(auto pu:pi.getPlacementUnits())check(pu->X()==50&&pu->Y()==seam-(dynamic_cast<PI::PlacementMacro*>(pu)?1.5f:.5f),"fixed anchor changed");
    auto fixedPass=rows(root/"fixed/spreading-passes.tsv");check(fixedPass.size()==10,"unresolved overflow must retain original inner budget");
    check(std::stod(fixedPass.back()["remaining_overflow"])>0,"residual falsely marked solved");
    std::cout<<"PASS production spread/writeback integration; local_regions="<<localCount<<" macro="<<macroMode<<" fixed_passes="<<fixedPass.size()<<"; four synthetic cells, no AMF flow\n";
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
