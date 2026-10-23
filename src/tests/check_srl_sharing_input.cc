#include "InitialPacker.h"
#include "CLBLegalizer.h"
#include "ParallelCLBPacker.h"
#include "simpleJSON.h"
#include <fstream>
#include <stdexcept>
#include <omp.h>

class SRLSharingProbe {
public:
    static void run(CLBLegalizer &legalizer, PlacementInfo &placement, const std::string &path) {
        const auto original=placement.getPlacementUnits();
        std::vector<std::pair<unsigned,size_t>> identity;
        for(auto pu:original) identity.emplace_back(pu->getId(),pu->getWeight());
        legalizer.setIntitialParameters(20.0,20,2);
        legalizer.getPUsToLegalize();
        if(legalizer.sharedSRLGroups.empty())throw std::runtime_error("No shared SRL groups in probe");
        assert(original==placement.getPlacementUnits());
        for(size_t i=0;i<original.size();++i)assert(identity[i]==std::make_pair(original[i]->getId(),static_cast<size_t>(original[i]->getWeight())));
        std::set<PlacementInfo::PlacementUnit*> followers;
        for(const auto &g:legalizer.sharedSRLGroups) for(auto pu:g.second) if(pu!=g.first)followers.insert(pu);
        std::ofstream out(path);out<<"group\tcell\ttype\tbel\n";
        size_t shared=0,units=0,cells=0;
        for(auto pu:legalizer.initialPUsToLegalize) {
            std::vector<PlacementInfo::PlacementUnit*> members{pu};
            if(legalizer.sharedSRLGroups.count(pu)) {members=legalizer.sharedSRLGroups.at(pu);++shared;}
            unsigned count=0;std::set<std::string> bels;
            for(auto member:members) {
                auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(member);++units;
                for(auto cell:macro->getCells())if(cell->originallyIsShifter())++count;
                for(const auto &fixed:macro->getFixedCellInfoVec()) {
                    assert(bels.insert(fixed.BELName).second);
                    out<<pu->getId()<<'\t'<<fixed.cell->getName()<<'\t'<<fixed.cell->getOriCellType()<<'\t'<<fixed.BELName<<'\n';++cells;
                }
                if(macro->getFixedCellInfoVec().empty()) {
                    assert(macro->getCells().size()==1);
                    out<<pu->getId()<<'\t'<<macro->getCells()[0]->getName()<<'\t'<<macro->getCells()[0]->getOriCellType()<<"\tH6LUT\n";++cells;
                }
            }
            assert(count<=4);
        }
        out.close();
        std::cout<<"PASS SRL input sharing groups="<<shared<<" original_pus="<<units<<" cells="<<cells<<" total_design_pus="<<original.size()<<'\n';
        // Exercise complete target expansion and displacement accounting.
        for(auto representative:legalizer.initialPUsToLegalize) {
            legalizer.PU2X[representative]=100;legalizer.PU2Y[representative]=100;
        }
        legalizer.expandSharedSRLTargets();
        assert(legalizer.PU2X.size()==units);
        for(auto pu:followers)assert(legalizer.PU2X.at(pu)==100 && legalizer.PU2Y.at(pu)==100);
        const auto groupsBefore=legalizer.sharedSRLGroups;
        std::map<PlacementInfo::PlacementUnit*,std::vector<std::string>> belsBefore;
        for(const auto &g:groupsBefore)for(auto pu:g.second)
            for(const auto &f:dynamic_cast<PlacementInfo::PlacementMacro*>(pu)->getFixedCellInfoVec())belsBefore[pu].push_back(f.BELName);
        legalizer.getPUsToLegalize();
        assert(groupsBefore==legalizer.sharedSRLGroups);
        for(const auto &entry:belsBefore) {
            std::vector<std::string> after;
            for(const auto &f:dynamic_cast<PlacementInfo::PlacementMacro*>(entry.first)->getFixedCellInfoVec())after.push_back(f.BELName);
            assert(entry.second==after);
        }
        // Check the actual final packer's multi-owner storage/cell enumeration.
        std::vector<ParallelCLBPacker::PackingCLBSite*> mapped(original.size(),nullptr);
        size_t packedGroups=0,costChecks=0;
        std::string siteType="SLICEM";
        auto site=placement.getDeviceInfo()->getSitesInType(siteType).at(100);
        for(const auto &g:groupsBefore) {
            ParallelCLBPacker::PackingCLBSite packed(&placement,site,3,10,1,2,4,10,0.4f,0.1f,mapped);
            std::set<DesignInfo::DesignCell*> expected;
            for(auto pu:g.second) {
                auto macro=dynamic_cast<PlacementInfo::PlacementMacro*>(pu);
                packed.mapLUTRAMRelatedCellsToSlots(macro);packed.addLUTRAMMacro();
                expected.insert(macro->getCells().begin(),macro->getCells().end());
            }
            assert(packed.getLUTRAMMacros().size()==g.second.size());
            assert(packed.getDeterminedClusterInSite()->getCellSet()==expected);++packedGroups;
            if(costChecks++<100) {
                const float x=site->X()+2,y=site->Y()+3;
                const float actual=legalizer.sharingHPWLChange(g.first,x,y);
                std::map<int,PlacementInfo::PlacementNet*> nets;
                std::map<PlacementInfo::PlacementUnit*,std::pair<float,float>> positions;
                for(auto pu:g.second) {
                    positions[pu]={pu->X(),pu->Y()};
                    for(auto net:placement.getPlacementUnitId2Nets()[pu->getId()])nets[net->getId()]=net;
                    pu->setAnchorLocationAndForgetTheOriginalOne(x,y);
                }
                float expectedCost=0;
                for(const auto &entry:nets)if(entry.second->getDesignNet()->getPins().size()<=10000)
                    expectedCost+=entry.second->getNewHPWLByTrying(g.first,x,y,0.4f)-entry.second->getHPWL(0.4f);
                for(const auto &entry:positions)entry.first->setAnchorLocationAndForgetTheOriginalOne(entry.second.first,entry.second.second);
                assert(std::fabs(actual-expectedCost)<1e-3f);
            }
        }
        std::cout<<"PASS rebuild stability, final packer groups="<<packedGroups<<", union-net HPWL oracle=100\n";
    }
};
int main(int argc,char**argv) {
    try {
        if(argc!=3)throw std::runtime_error("config.json packing.tsv required");
        auto cfg=parseJSONFile(argv[1]);omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo placement(&design,&device,cfg);
        InitialPacker packer(&design,&device,&placement,cfg);packer.pack();
        std::string slicem="SLICEM";
        auto site=device.getSitesInType(slicem).at(100);
        // Synthetic co-location only for compatibility testing, never a full
        // experiment's starting placement. Grouping leaves these PUs unchanged.
        for(auto pu:placement.getPlacementUnits())if(pu->isMCLB())pu->setAnchorLocationAndForgetTheOriginalOne(site->X(),site->Y());
        std::vector<std::string> types{"SLICEM"};CLBLegalizer legalizer("SRL-sharing-probe",&placement,&device,types,cfg);
        SRLSharingProbe::run(legalizer,placement,argv[2]);return 0;
    } catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
