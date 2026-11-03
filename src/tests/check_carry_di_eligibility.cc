#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "CLBSiteLegality.h"
#include "HardResourceUtils.h"
#include "simpleJSON.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <omp.h>

static void require(bool value, const std::string &message)
{ if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv)
{
    try {
        require(argc == 3, "usage: checkCarryDIEligibility config.json report-prefix");
        oriTime = std::chrono::steady_clock::now();
        auto cfg = parseJSONFile(argv[1]);
        omp_set_num_threads(std::stoi(cfg.at("jobs")));
        DeviceInfo device(cfg, cfg.at("device"));
        DesignInfo design(cfg, &device);
        PlacementInfo placement(&design, &device, cfg);
        InitialPacker initial(&design, &device, &placement, cfg);
        initial.pack();
        HardResourceUtils::validateCascadeMacros(&placement);
        placement.buildSimpleTimingGraph();
        using Site = ParallelCLBPacker::PackingCLBSite;
        DeviceInfo::DeviceSite *probe = nullptr;
        for (auto s : device.getSites())
            if (s->getSiteType() == "SLICEL" && !s->isOccupied()) { probe = s; break; }
        require(probe != nullptr, "no unoccupied SLICEL");
        std::vector<Site *> unused(placement.getPlacementUnits().size(), nullptr);
        std::ofstream out(std::string(argv[2]) + ".slots.tsv");
        out << "carry\toffset\tbit\tside\tcell_id\tcell\tvirtual\n";
        size_t macros = 0, sites = 0, pairs = 0, lut62 = 0, verifiedCells = 0;
        double mappingSeconds = 0, addSeconds = 0, finalSeconds = 0;
        std::set<std::string> fixtureChecked;
        for (auto macro : placement.getPlacementMacros()) {
            if (macro->getMacroType() != PlacementInfo::PlacementMacro::PlacementMacroType_CARRY) continue;
            ++macros;
            for (auto carry : macro->getCells()) {
                if (!carry->isCarry()) continue;
                ++sites;
                const float offset = macro->getCellOffsetYInMacro(carry);
                Site site(&placement, probe, 3, 10, .25, .5, 6, 10, .4, .02, unused);
                auto start = std::chrono::steady_clock::now();
                site.mapCarryRelatedCellsToSlots(macro, offset);
                mappingSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                const auto before = site.getSlotMapping();
                std::set<DesignInfo::DesignCell *> seen, expected, paired;
                for (auto cell : macro->getCells())
                    if (cell->isLUT() && std::fabs(macro->getCellOffsetYInMacro(cell)-offset) < 0.01) expected.insert(cell);
                for (int bit=0; bit<8; ++bit) for (int side=0; side<2; ++side) {
                    auto cell=before.LUTs[bit/4][side][bit%4]; if(!cell) continue;
                    require(seen.insert(cell).second, "duplicate LUT slot: " + cell->getName());
                    if(cell->getOriCellType()==DesignInfo::CellType_LUT6_2) {
                        ++lut62; require(side==0, "LUT6_2 assigned a DI-side BEL");
                        require(before.LUTs[bit/4][1][bit%4]==nullptr, "LUT6_2 paired with another cell");
                    }
                    out << carry->getName() << '\t' << offset << '\t' << bit << '\t' << side << '\t'
                        << cell->getCellId() << '\t' << cell->getName() << '\t' << cell->isVirtualCell() << '\n';
                }
                require(seen==expected, "missing or foreign macro LUT: " + carry->getName());
                verifiedCells += seen.size();
                for(auto pair:site.getFixedPairedLUTs()) {
                    ++pairs; require(pair.first!=pair.second,"self-paired LUT");
                    require(CLBSiteLegality::lutPairCompatible(pair.first,pair.second),"illegal independent LUT pair");
                    require(paired.insert(pair.first).second && paired.insert(pair.second).second,"multiply paired LUT");
                }
                for(auto cell:site.getConflictLUTs()) require(!paired.count(cell),"single/paired overlap");
                if (cfg.count("CarryDIFixture") && cfg.at("CarryDIFixture")=="true") {
                    const std::string n=carry->getName();
                    auto named=[&](int bit,int side,const std::string &name){
                        auto cell=before.LUTs[bit/4][side][bit%4];return cell&&cell->getName()==name;
                    };
                    if(n=="pair/c") require(named(0,0,"pair/s")&&named(0,1,"pair/d"),"ordinary pair changed");
                    else if(n=="di_only/c") require(!before.LUTs[0][0][0]&&named(0,1,"di_only/d"),"DI-only LUT lost");
                    else if(n=="dual_same/c") require(named(0,0,"dual_same/a")&&!before.LUTs[0][1][0],"same-bit LUT6_2 occupancy");
                    else if(n=="dual_cross/c"||n=="reverse/c") {
                        auto prefix=n.substr(0,n.find('/'));
                        require(named(0,0,prefix+"/a")&&named(1,0,prefix+"/b")&&!before.LUTs[0][1][1],"cross-bit LUT6_2 occupancy");
                    } else if(n=="fanout/c") require(named(0,0,"fanout/s")&&!before.LUTs[0][1][0],"multi-fanout DI was absorbed");
                    else if(n=="six_di/c") require(seen.empty(),"LUT6 DI-only absorbed");
                    else if(n=="chain/c0") require(named(0,0,"chain/a"),"chain S owner lost");
                    else if(n=="chain/c1") require(named(1,0,"chain/b")&&!before.LUTs[0][1][1],"cross-site duplicate DI");
                    else throw std::runtime_error("unexpected fixture carry: "+n);
                    fixtureChecked.insert(n);
                }
                start = std::chrono::steady_clock::now();
                site.addCarry();
                addSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                start = std::chrono::steady_clock::now();
                site.finalMapToSlots();
                finalSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                require(CLBSiteLegality::checkControlsAndLUTs(site.getSlotMapping()),"export slot legality: "+carry->getName());
                for(int bit=0;bit<8;++bit)for(int side=0;side<2;++side)
                    require(before.LUTs[bit/4][side][bit%4]==site.getSlotMapping().LUTs[bit/4][side][bit%4],"final mapping moved reserved carry LUT");
            }
        }
        if(cfg.count("CarryDIFixture")) require(fixtureChecked.size()==9,"fixture coverage incomplete");
        require(bool(out), "cannot write slot report");
        std::ofstream summary(std::string(argv[2])+".summary.json");
        summary << "{\"carry_macros\":"<<macros<<",\"carry_sites\":"<<sites<<",\"valid_fixed_pairs\":"<<pairs
                <<",\"lut6_2_unique_owners\":"<<lut62<<",\"covered_lut_members\":"<<verifiedCells
                <<",\"duplicate_slots\":0,\"set_overlaps\":0,\"mapping_seconds\":"<<mappingSeconds
                <<",\"add_carry_seconds\":"<<addSeconds<<",\"final_slot_mapping_seconds\":"<<finalSeconds
                <<",\"fixture_carries_checked\":"<<fixtureChecked.size()<<",\"full_placement_executed\":false,\"routing_executed\":false}\n";
        require(bool(summary),"cannot write summary");
        std::cout<<"PASS carry_sites="<<sites<<" LUT6_2="<<lut62<<" fixed_pairs="<<pairs
                 <<" mapping_seconds="<<mappingSeconds<<" add_seconds="<<addSeconds<<" final_seconds="<<finalSeconds<<std::endl;
        return 0;
    } catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
