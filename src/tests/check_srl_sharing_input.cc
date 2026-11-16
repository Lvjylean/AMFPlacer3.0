#include "InitialPacker.h"
#include "IncrementalBELPacker.h"
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
        const unsigned limit=srl_site_sharing::siteLimit(legalizer.JSONCfg,placement.getDeviceInfo()->getDeviceName());
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
            if (members.size()>1 && limit!=8 && count>limit) throw std::runtime_error("Shared group exceeds configured limit");
            if (limit==8 && count>16) throw std::runtime_error("More SRLs than two per physical row");
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
                packed.mapLUTRAMRelatedCellsToSlots(macro,limit);packed.addLUTRAMMacro();
                expected.insert(macro->getCells().begin(),macro->getCells().end());
            }
            packed.prepareMemoryFFPacking(true,true);
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
// Use only the named cells of check_srl_physical_rows.py. Assign the two
// owners in both orders to exercise packing independently of legalization.
static void checkDualRows(PlacementInfo &pi, DesignInfo &design, DeviceInfo::DeviceSite *target)
{
    using Site=ParallelCLBPacker::PackingCLBSite;
    using Macro=PlacementInfo::PlacementMacro;
    auto lookup=[&](std::string name){return design.getCell(name);};
    auto owner=[&](std::string name,const std::string &bel) {
        auto cell=lookup(name);
        auto m=dynamic_cast<Macro*>(pi.getPlacementUnitByCell(cell));
        m->getFixedCellInfoVec().clear();
        m->addFixedCellInfo(cell,"SLICEM",bel);
        return m;
    };
    std::vector<Site*> unused;
    for (bool reverse : {false,true}) {
        Site packed(&pi,target,3,10,1,2,4,10,.4,.1,unused);
        auto primary=owner("full16_0","H6LUT");
        auto secondary=owner("full16_1","H5LUT");
        packed.mapLUTRAMRelatedCellsToSlots(reverse?secondary:primary,8);
        packed.mapLUTRAMRelatedCellsToSlots(reverse?primary:secondary,8);
        packed.prepareMemoryFFPacking(true,true);
        for (unsigned lane=0;lane<2;++lane) {
            packed.ffQBlockedMask=0xffffu ^ (1u<<(11+lane*4));
            for (int i=0;i<3;++i) {
                Site::PackingCLBCluster candidate(&packed);
                auto ff=pi.getPlacementUnitByCell(lookup("rowff"+std::to_string(i)));
                bool fits=candidate.tryAddPU(ff);
                if (fits!=(static_cast<unsigned>(i)==lane)) throw std::runtime_error("incorrect dual SRL FF lane");
                if (fits) {
                    packed.setDeterminedClusterInSite(new Site::PackingCLBCluster(&candidate));
                    ff->setFixed();
                    packed.finalMapToSlots();
                    ff->setUnfixed();
                    if (packed.getSlotMapping().FFs[1][lane][3]!=lookup("rowff"+std::to_string(i)))
                        throw std::runtime_error("final FF BEL differs from candidate");
                    delete packed.getDeterminedClusterInSite();
                    packed.setDeterminedClusterInSite(nullptr);
                }
            }
        }
        packed.ffQBlockedMask=0;
        Site::PackingCLBCluster together(&packed);
        for (unsigned i=0;i<3;++i) {
            auto ff=lookup("rowff"+std::to_string(i));
            if (!together.tryAddPU(pi.getPlacementUnitByCell(ff)))
                throw std::runtime_error("Both SRL loads and one independent FF should fit");
        }
        packed.setDeterminedClusterInSite(new Site::PackingCLBCluster(&together));
        packed.finalMapToSlots();
        for (unsigned lane=0;lane<2;++lane)
            if (packed.getSlotMapping().FFs[1][lane][3]!=lookup("rowff"+std::to_string(lane)))
                throw std::runtime_error("Distinct SRL outputs should use both matching lanes at once");
    }
    // Deliberately forge the handoff and check that a refusal leaves no owner behind.
    for (auto name : {"distinct_1","invert_1","full32_0","missing_1"}) {
        Site packed(&pi,target,3,10,1,2,4,10,.4,.1,unused);
        std::string other=name;
        std::string primary=other=="invert_1"?"invert_0":other=="distinct_1"?"distinct_0":
                            other=="missing_1"?"missing_0":"full16_0";
        packed.mapLUTRAMRelatedCellsToSlots(owner(primary,"H6LUT"),8);
        bool rejected=false;
        try { packed.mapLUTRAMRelatedCellsToSlots(owner(other,"H5LUT"),8); }
        catch (const std::runtime_error &) { rejected=true; }
        if (!rejected || packed.getLUTRAMMacros().size()!=1) throw std::runtime_error("invalid SRL pair accepted");
    }
}
static void checkEarlyPairing(PlacementInfo &placement, DesignInfo &design, DeviceInfo &device,
                              std::map<std::string, std::string> &cfg)
{
    auto owner = [&](std::string name) { return placement.getPlacementUnitByCell(design.getCell(name)); };
    auto checkFree = [&]() {
        for (std::string name : {"rowff0", "rowff1", "fanoutFF", "srl32FF", "singleCE", "multiCE0", "multiCE1"})
            if (!dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(owner(name)))
                throw std::runtime_error("SRL Q to D load was bound early: " + name);
    };
    auto checkTogether = [&](const std::string &left, const std::string &right) {
        if (owner(left) != owner(right)) throw std::runtime_error("Missing expected pair: " + left + " / " + right);
    };
    checkFree();
    checkTogether("viaLogic", "dataLUT");
    placement.calculateNetNumDistributionOfPUs();
    placement.createGridBins(5, 5);
    placement.updateElementBinGrid();
    IncrementalBELPacker incremental(&design, &device, &placement, cfg);
    incremental.LUTFFPairing(4.0);
    checkFree();
    incremental.FFPairing(4.0);
    checkFree();
    checkTogether("sameRAM0", "sameRAM1");
    for (std::string name : {"otherRAM0", "otherRAM1", "ramOrdinary0", "ramOrdinary1",
                             "ordinaryRam0", "ordinaryRam1"})
        if (!dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(owner(name)))
            throw std::runtime_error("Unexpected FF partner for " + name);
    std::cout << "PASS RAM32M16 same-instance pair formed; different-instance and mixed pairs stayed free\n";
    checkTogether("ordinary0", "ordinary1");
    checkTogether("q31FF0", "q31FF1");
    checkTogether("ceOnly0", "ceOnly1");
    std::cout << "PASS seven direct FFs remain free through all early pairing passes; "
                 "ordinary, Q31-only, CE-only and indirect data controls still pair\n";
}

int main(int argc,char**argv) {
    try {
        if(argc!=3 && argc!=4)throw std::runtime_error("config.json packing.tsv required");
        auto cfg=parseJSONFile(argv[1]);omp_set_num_threads(1);
        DeviceInfo device(cfg,cfg["device"]);DesignInfo design(cfg,&device);PlacementInfo placement(&design,&device,cfg);
        InitialPacker packer(&design,&device,&placement,cfg);packer.pack();
        std::string slicem="SLICEM";
        auto site=device.getSitesInType(slicem).at(100);
        // Synthetic co-location only for compatibility testing, never a full
        // experiment's starting placement. Grouping leaves these PUs unchanged.
        for(auto pu:placement.getPlacementUnits()) {
            float offset=pu->isMCLB()?0.0f:1.0f;
            pu->setAnchorLocationAndForgetTheOriginalOne(site->X()+offset,site->Y()+offset);
        }
        placement.updateB2BAndGetTotalHPWL();
        if (argc == 4 && std::string(argv[3]) == "verify-ff-pairing") {
            checkEarlyPairing(placement, design, device, cfg);
            placement.buildSimpleTimingGraph();
            checkDualRows(placement, design, site);
            std::cout << "PASS formerly pairable direct loads compete separately and occupy HFF and HFF2\n";
            return 0;
        }
        std::vector<std::string> types{"SLICEM"};CLBLegalizer legalizer("SRL-sharing-probe",&placement,&device,types,cfg);
        SRLSharingProbe::run(legalizer,placement,argv[2]);
        if (argc==4) {
            if (std::string(argv[3])!="verify-dual-rows") throw std::runtime_error("unknown probe");
            CLBLegalizer fresh("fresh-SRL-probe",&placement,&device,types,cfg);
            SRLSharingProbe::run(fresh,placement,std::string(argv[2])+".fresh");
            placement.buildSimpleTimingGraph();
            checkDualRows(placement,design,site);
        }
        return 0;
    } catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
