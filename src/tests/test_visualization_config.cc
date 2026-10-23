#include "VisualizationConfig.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
 assert(argc==2); namespace fs=std::filesystem;
 fs::path root=argv[1]; fs::create_directories(root);
 using Config=std::map<std::string,std::string>;
 Config legacy{{"VisualizationTraceFine","true"},{"DumpAllCoordTrace","legacy"}};
 auto same=legacy; visualization_trace::configure(legacy,"config.json");assert(legacy==same);
 Config off{{"visualization","false"},{"VisualizationTraceFine","true"},{"DumpAllCoordTrace","old"},{"DumpFFCoordTrace","old"}};
 visualization_trace::configure(off,"config.json");assert(off.at("VisualizationTraceFine")=="false" && !off.count("DumpAllCoordTrace") && !off.count("DumpFFCoordTrace"));
 Config on{{"visualization","true"},{"dumpDirectory",root.string()},{"DumpAllCoordTrace","old"}};
 visualization_trace::configure(on,"config.json");assert(fs::is_directory(root/"visualization"));assert(on.at("DumpAllCoordTrace")==fs::absolute(root/"visualization/DumpAllCoordTrace").string());
 std::ofstream(root/"visualization/retained")<<"keep";
 bool rejected=false;try {visualization_trace::configure(on,"config.json");}catch(const std::runtime_error&){rejected=true;}assert(rejected);
 Config bad{{"visualization","yes"}};rejected=false;try{visualization_trace::configure(bad,"config.json");}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 std::cout<<"PASS legacy, disabled, enabled/path, existing-output protection, invalid-value\n";
}
