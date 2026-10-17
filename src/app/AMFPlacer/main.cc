/**
 * @file main.cc
 * @author Tingyuan Liang (tliang@connect.ust.hk)
 * @brief AMF-Placer Main file which directly pass the arguments to the AMFPacer workflow
 * @version 0.1
 * @date 2021-05-31
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "AMFPlacer.h"
#include "../../lib/utils/RuntimeProfiler.h"
#include "../../lib/HiFPlacer/designInfo/native/PlacementRepair.h"
#include "../../lib/HiFPlacer/designInfo/native/FfPinAccessRepair.h"

int main(int argc, const char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--capabilities")
    {
        std::cout << "{\"schema\":\"amf-capabilities-v1\",\"clock_resource_capacity_schema\":1,\"external_floorplan_initialization_schema\":1,\"paper_hierarchical_boundaries_schema\":2,\"global_placement_inspection_schema\":1,\"amf2pro_integration_schema\":1,\"dfx_incremental_schema\":1,\"dfx_full_hard_regions_schema\":1,\"dfx_partition_floorplan_schema\":1,\"native_static_cache_schema\":2,\"native_placement_repair_schema\":1,\"native_ff_pin_access_repair_schema\":1,\"native_pin_cache_packing_schema\":1,\"native_dfx_edge_policy_schema\":1}\n";
        return 0;
    }
    amf_profile::Session profileSession;
    AMF_PROFILE_FUNCTION("process_control");
    if(argc==3&&std::string(argv[1])=="--native-ff-pin-repair") {
        try {
            AMF_PROFILE_SCOPE("native_ff_pin_repair", "data-driven same-site FF recovery");
            amf_native::FfPinAccessRepair::run(parseJSONFile(argv[2]));
            std::cout<<"AMF_NATIVE_FF_PIN_REPAIR_COMPLETED\n";return 0;
        }catch(const std::exception &e){std::cerr<<"AMF_ERROR: "<<e.what()<<"\n";return 2;}
    }
    if(argc==3&&std::string(argv[1])=="--native-placement-repair") {
        try {
            AMF_PROFILE_SCOPE("native_placement_repair", "certified one-DSP recovery");
            auto cfg=parseJSONFile(argv[2]);
            auto cache=amf_native::StaticDesignCache::load(cfg.at("Native static cache read"));
            amf_native::PlacementRepair::run(cfg,cache);
            std::cout<<"AMF_NATIVE_PLACEMENT_REPAIR_COMPLETED changed_cells=1\n";return 0;
        }catch(const std::exception &e){std::cerr<<"AMF_ERROR: "<<e.what()<<"\n";return 2;}
    }
    if(argc==3&&std::string(argv[1])=="--native-cache") {
        try {
            auto cfg=parseJSONFile(argv[2]);const auto action=cfg.at("Native cache action");
            if(action=="bootstrap-existing-full") {
                // Migration for an already completed full placement only. New
                // full runs use the ordinary DesignInfo input/export hooks.
                AMF_PROFILE_SCOPE("input_native_cache", "bootstrap historical full input");
                amf_native::StaticDesignCache cache(cfg);
                std::ifstream f(cfg.at("Native historical full netlist"));
                if(!f)throw std::runtime_error("Missing historical full netlist");
                for(const auto &c:amf_native::StaticDesignCache::readNetlist(f)) {
                    cache.observeCell(c.name,c.type);for(const auto &p:c.pins)cache.observePin(p);
                }
                cache.finishCapture();cache.savePlacement(cfg.at("Native historical placement Tcl"),cfg);
            }else if(action=="certify-pin-access") {
                auto cache=amf_native::StaticDesignCache::load(cfg.at("Native static cache proposal"));
                cache.certifyPinAccess(cfg);
                cache.write(cfg.at("Native static cache sealed output"));
            }else if(action=="audit-spec") {
                auto cache=amf_native::StaticDesignCache::load(cfg.at("Native static cache proposal"));
                cache.writeContextAuditSpec(cfg.at("Native context audit specification"));
            }else if(action=="seal"||action=="certify-resources") {
                auto cache=amf_native::StaticDesignCache::load(cfg.at("Native static cache proposal"));
                amf_native::Config contract{{"identity",cfg.at("Native static identity")},{"device",cfg.at("device")},
                    {"clock_period",cfg.at("ClockPeriod")},{"static_shell_sha256",cfg.at("Native static shell sha256")},
                    {"static_connectivity_verified",cfg.at("Native static connectivity verified")},
                    {"interfaces_verified",cfg.at("Native interfaces verified")}};
                if(cfg.count("Native partition pins file"))contract["partition_pins_file"]=cfg.at("Native partition pins file");
                if(cfg.count("Native resource inventory file")) {
                    contract["resource_inventory_file"]=cfg.at("Native resource inventory file");
                    contract["resource_geometry_file"]=cfg.at("Native resource geometry file");
                    contract["resource_inventory_sha256"]=cfg.at("Native resource inventory sha256");
                    contract["resource_geometry_sha256"]=cfg.at("Native resource geometry sha256");
                    contract["resource_side_policy"]=cfg.at("Native resource side policy");
                }
                if(action=="seal")cache.seal(cfg.at("Native final static positions"),contract,true);
                else cache.certifyResources(contract);
                cache.write(cfg.at("Native static cache sealed output"));
            }else if(action=="compose-inspect") {
                auto prepared=amf_native::StaticDesignCache::prepare(cfg);
                std::ofstream f(cfg.at("Native workspace")+"/netlist.txt");f<<prepared.netlist;f.close();
                if(!f)throw std::runtime_error("Native inspection write failed");
                std::cout<<"AMF_NATIVE_COMPOSE_OK dynamic="<<prepared.dynamicCells<<" fixed="<<prepared.fixedCells<<" pins="<<prepared.retainedPins<<"\n";
            }else throw std::runtime_error("Unknown native cache action");
            return 0;
        }catch(const std::exception &e){std::cerr<<"AMF_ERROR: "<<e.what()<<"\n";return 2;}
    }
    if (argc != 2 && !(argc == 4 && (std::string(argv[2]) == "--inspect-global-placement" || std::string(argv[2]) == "--inspect-initial-placement" || std::string(argv[2]) == "--inspect-packing" || std::string(argv[2]) == "--inspect-input" || std::string(argv[2]) == "--legalize-resources")))
    {
        std::cerr << "Usage: " << argv[0] << " <config JSON file> [--inspect-global-placement <report.json> | --inspect-input <report.json> | --inspect-initial-placement <report.json> | --legalize-resources <directory>]\n";
        return 1;
    }
    try
    {
        AMFPlacer placer(argv[1]);
        if (argc == 4 && std::string(argv[2]) == "--legalize-resources")
            placer.legalizeResources(argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-global-placement")
            placer.run("", "", argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-initial-placement")
            placer.run("", argv[3]);
        else if (argc == 4 && std::string(argv[2]) == "--inspect-packing")
            placer.run(argv[3]);
        else if (argc == 4)
            placer.inspectInputs(argv[3]);
        else
            placer.run();
    }
    catch (const std::exception &error)
    {
        std::cerr << "AMF_ERROR: " << error.what() << "\n";
        return 2;
    }
    return 0;
}
