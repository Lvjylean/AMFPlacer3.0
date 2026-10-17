#ifndef AMF_STATIC_DESIGN_CACHE_H
#define AMF_STATIC_DESIGN_CACHE_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <iosfwd>

namespace amf_native {
using Config = std::map<std::string, std::string>;
struct Pin { std::string name, ref, direction, alias, driver; };
struct Cell {
    std::string name, type, site, bel;
    std::vector<Pin> pins;
    std::string srlMetadata;
    bool registeredDSP = false;
};
struct Port {
    std::string module, name, direction, driver;
    bool clock = false;
    std::string partitionNode;
    double x=0, y=0;
};
struct Module {
    std::string id, instance;
    std::set<std::string> sites;
    std::vector<Port> ports;
};
struct PreparedInput {
    std::string netlist;
    std::size_t dynamicCells=0, fixedCells=0, interfaceEndpoints=0, retainedPins=0;
};
struct OutputBuffer {
    std::string module, port, cell, driver, net;
};

// Native database: only static fabric cells are persisted. RP ownership is
// assigned during the ordinary full-input parse, before placement. Interfaces
// are named port bits, never cross-version array indices or old RM cell names.
// Vivado owns physical routes; this cache contains connectivity and placement.
class StaticDesignCache {
public:
    explicit StaticDesignCache(const Config &cfg);
    StaticDesignCache() = default;
    static bool captureEnabled(const Config &cfg);
    static bool reuseEnabled(const Config &cfg);
    void observeCell(const std::string &name, const std::string &type);
    void observePin(const Pin &pin);
    void finishCapture();
    void savePlacement(const std::string &placementTcl, const Config &cfg);
    static PreparedInput prepare(Config &cfg);
    static std::vector<Port> readPorts(const std::string &file);
    // Split shared data-output drivers before placement. The adapter executes
    // this exact plan with INIT=2'h2; it does not choose topology or placement.
    static std::vector<OutputBuffer> normalizeOutputInterfaces(
        const std::string &module, std::vector<Cell> &dynamic, std::vector<Port> &ports);
    static std::vector<Cell> readNetlist(std::istream &in);
    static StaticDesignCache load(const std::string &file);
    void write(const std::string &file) const;
    void writeContextAuditSpec(const std::string &file) const;
    // Backend finalization is one-time; complete actual positions are required.
    // New file only, never rewrite a cache consumed by an incremental worker.
    void seal(const std::string &positions, const Config &contract, bool allowCorrection);
    // Adds final resource legality to a NEW sealed cache. Never reseal positions
    // or edit a cache on disk; caller must publish an immutable child artifact.
    void certifyResources(const Config &contract);
    PreparedInput compose(const std::string &module, std::vector<Cell> dynamic,
                          const std::vector<Port> &ports, Config &cfg) const;
    const std::vector<Cell> &staticCells() const { return cells; }
    const std::map<std::string,Module> &moduleTable() const { return modules; }
    const Config &identity() const { return metadata; }
    bool isSealed() const { return sealed; }
private:
    Config metadata;
    std::map<std::string,Module> modules;
    std::vector<Cell> cells;
    std::unordered_map<std::string,std::size_t> nameIndex;
    std::map<std::string,std::string> dynamicOutputTokens;
    std::map<std::string,std::string> boundaryPinTokens;
    std::set<std::string> baselineBlocked;
    std::map<std::string,std::size_t> ownerCounts;
    std::string currentCell, outputPath;
    bool currentStatic=false, captured=false, sealed=false;
    std::string owner(const std::string &name) const;
    void indexAndValidate();
    std::string interfaceDriver(const std::string &driver) const;
};
}
#endif
