#ifndef AMF_INCREMENTAL_CONTEXT_H
#define AMF_INCREMENTAL_CONTEXT_H
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Version 1 handles one active RP. Vivado owns static routing and partition pins;
// AMF receives the complete primitive netlist, not an electrically cut RM graph.
struct IncrementalContext
{
    struct FixedCell { std::string site, bel; };
    bool enabled = false;
    std::string partition;
    std::set<std::string> allowedSites, blockedSites;
    std::map<std::string, FixedCell> fixed;
    std::set<int> staticCellIds;
    float minX = 0, maxX = 0, minY = 0, maxY = 0;

    static std::vector<std::string> fields(const std::string &line)
    {
        std::vector<std::string> result;
        std::stringstream stream(line); std::string field;
        while (std::getline(stream, field, '\t')) result.push_back(field);
        return result;
    }
    static std::string required(const std::map<std::string, std::string> &cfg, const std::string &key)
    {
        auto it = cfg.find(key);
        if (it == cfg.end() || it->second.empty()) throw std::runtime_error("DFX missing configuration: " + key);
        return it->second;
    }
    static std::set<std::string> sites(const std::string &path, bool allowEmpty)
    {
        std::ifstream input(path); std::string line; std::set<std::string> result;
        if (!input) throw std::runtime_error("Cannot read DFX site list: " + path);
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            if (line.find_first_of(" \t\r") != std::string::npos || !result.insert(line).second)
                throw std::runtime_error("Invalid/duplicate DFX site: " + line);
        }
        if (!allowEmpty && result.empty()) throw std::runtime_error("Empty DFX allowed-site list");
        return result;
    }
    explicit IncrementalContext(const std::map<std::string, std::string> &cfg)
    {
        auto mode = cfg.find("IncremenralCompilation");
        if (mode == cfg.end() || mode->second == "false") return;
        if (mode->second != "true") throw std::runtime_error("IncremenralCompilation must be true or false");
        enabled = true;
        partition = required(cfg, "DFX active partition");
        if (partition.back() == '/' || partition.find_first_of("\t\r\n ") != std::string::npos)
            throw std::runtime_error("Invalid active DFX partition path");
        allowedSites = sites(required(cfg, "DFX allowed sites file"), false);
        blockedSites = sites(required(cfg, "DFX blocked sites file"), true);
        std::ifstream input(required(cfg, "DFX static placement file"));
        std::string line;
        if (!input || !std::getline(input, line) || line != "cell\tsite\tbel")
            throw std::runtime_error("Invalid DFX static placement header");
        while (std::getline(input, line)) {
            auto f = fields(line);
            if (f.size() != 3 || f[0].empty() || f[1].empty() || f[2].empty() ||
                isActive(f[0]) || !fixed.emplace(f[0], FixedCell{f[1], f[2]}).second)
                throw std::runtime_error("Invalid DFX static placement row: " + line);
            blockedSites.insert(f[1]);
        }
        if (fixed.empty()) throw std::runtime_error("DFX requires a nonempty fixed static context");
    }
    bool isActive(const std::string &name) const
    { return name.compare(0, partition.size() + 1, partition + "/") == 0; }
    bool siteAllowed(const std::string &name) const
    { return allowedSites.count(name) && !blockedSites.count(name); }
};
#endif
