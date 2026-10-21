#include "DevicePlacementPolicy.h"
#include <iostream>
#include <stdexcept>
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main()
{
    using namespace amf_device;
    try {
        std::map<std::string,std::string> cfg;
        const auto v=resolve(cfg,"VCU108"), alias=resolve(cfg,"VU095"), u=resolve(cfg,"U250");
        check(v.kind==alias.kind && canonicalDeviceName("VU095")=="VCU108","VU095 alias");
        check(v.shouldStretch(false,false) && !u.shouldStretch(false,false),"Device defaults");
        for (const auto device : {"VCU108","VU095","U250"})
            for (const auto stretch : {"true","false"})
                for (const auto strategy : {"slr-hierarchical","cr-slr-hierarchical"})
                    for (const auto stage : {"independent-max2","merged"})
                        for (const auto incremental : {"true","false"}) {
                            cfg={{"ClockRegionColumnStretch",stretch},{"BoundaryClusteringStrategy",strategy},
                                 {"BoundaryStageMode",stage},{"IncrementalCompilation",incremental}};
                            auto before=cfg;auto p=resolve(cfg,device);
                            check(cfg==before,"Policy must not rewrite algorithm/cache config");
                            check(p.shouldStretch(false,false)==(std::string(stretch)=="true"),"Explicit switch");
                            check(!p.shouldStretch(true,false) && !p.shouldStretch(false,true),"New/disabled legacy gate");
                            check(p.legacySideConstraint(true,false) && !p.legacySideConstraint(true,true) &&
                                  !p.legacySideConstraint(false,false),"Existing side-limit gates");
                        }
        check(!v.selectedColumnAttraction(false) && !v.selectedColumnAttraction(true),"VU095 legacy rule");
        check(!u.selectedColumnAttraction(false) && u.selectedColumnAttraction(true),"U250 compatibility rule");
        for (auto invalid : {"VHK158","U25O",""}) {
            bool threw=false;try{resolve({},invalid);}catch(const std::invalid_argument&){threw=true;}
            check(threw,"Unimplemented/unknown device silently accepted");
        }
        cfg={{"VHK158LutFfPilot","true"},{"noBlockAware","true"},
             {"BoundaryAwareClustering","false"},{"PhysicalBoundaryMode","false"},
             {"clock region geometry file","fixture.json"}};
        auto pilot=resolve(cfg,"VHK158");
        check(pilot.kind==Kind::VHK158 && !pilot.shouldStretch(false,false) &&
              !pilot.legacySideConstraint(true,false) && !pilot.selectedColumnAttraction(true),
              "VHK pilot must not inherit UltraScale heuristics");
        for(const auto key : {"ClockRegionColumnStretch","IncrementalCompilation",
                             "IncremenralCompilation","ProPrimitiveTiming","ProLegacyIOBoundary",
                             "BoundaryAwareClustering","PhysicalBoundaryMode"}) {
            auto invalid=cfg; invalid[key]="true";
            bool threw=false;try{resolve(invalid,"VHK158");}
            catch(const std::invalid_argument&){threw=true;}
            check(threw,"VHK unsupported policy silently accepted");
        }
        for(auto invalid : {"yes","1","FALSE",""}) {
            bool threw=false;try{resolve({{"ClockRegionColumnStretch",invalid}},"U250");}
            catch(const std::invalid_argument&){threw=true;}
            check(threw,"Invalid stretching value silently accepted");
        }
        std::cout<<"Device defaults, overrides, aliases, unsupported device and orthogonal mode matrix passed\n";
        return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
