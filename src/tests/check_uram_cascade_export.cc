#include "PlacementInfo.h"
#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "PlacementTimingOptimizer.h"
#include "simpleJSON.h"
#include <filesystem>
#include <iostream>
#include <omp.h>
static void need(bool v,const char *s){if(!v)throw std::runtime_error(s);}
int main(int argc,char **argv){try{
    need(argc==3,"Expected two-URAM fixture config and output directory");omp_set_num_threads(1);
    auto cfg=parseJSONFile(argv[1]);cfg["dumpDirectory"]=argv[2];
    DeviceInfo device(cfg,cfg.at("device"));DesignInfo design(cfg,&device);PlacementInfo pi(&design,&device,cfg);
    InitialPacker packer(&design,&device,&pi,cfg);packer.pack(true);
    need(pi.getPlacementMacros().size()==1,"one URAM chain");auto macro=pi.getPlacementMacros().at(0);
    need(macro->getURAMNum()==2,"two URAM members");
    std::string an="URAM288_X0Y10",bn="URAM288_X0Y11";
    auto a=device.getSite(an),b=device.getSite(bn);need(a&&b,"real URAM sites");
    pi.getPULegalSite()[macro]={a,b};macro->setAnchorLocationAndForgetTheOriginalOne(a->X(),a->Y());
    pi.createGridBins(5,5);pi.updateElementBinGrid();pi.buildSimpleTimingGraph();
    PlacementTimingOptimizer timing(&pi,cfg);
    ParallelCLBPacker finalPacker(&design,&device,&pi,cfg,3,10,.25,.5,6,10,.02,"uram-export-test",&timing);
    finalPacker.addNonCLBPackingSites();
    auto anchors=finalPacker.getNonCLBPackedAnchors();
    need(anchors.count(macro->getId())==1,"chain members retained in non-CLB map");
    macro->setAnchorLocationAndForgetTheOriginalOne(a->X()+3,a->Y()+9);
    finalPacker.setPULocationToPackedSite();
    need(macro->X()==a->X()&&macro->Y()==a->Y(),"whole-chain anchor restored");
    auto path=std::filesystem::path(argv[2])/"production-uram-export.tcl";
    std::ofstream out(path);finalPacker.dumpDSPBRAMPlacementTcl(out);out.close();
    std::ifstream input(path);std::string text((std::istreambuf_iterator<char>(input)),{});
    need(text.find("a URAM288_X0Y10/URAM_288K_INST")!=std::string::npos,"first member exported");
    need(text.find("b URAM288_X0Y11/URAM_288K_INST")!=std::string::npos,"second member exported");
    need(DesignInfo::getFFSRType(DesignInfo::CellType_AND2B1L)==3 && DesignInfo::getFFSRType(DesignInfo::CellType_LDCE)==2,
         "special control types retain their integer identity");
    std::cout<<"PASS URAM non-CLB site registration, whole-chain anchor restore, production export and special control modes\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
