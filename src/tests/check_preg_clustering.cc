// Graph and cluster-selection regression. No placement optimizer or routing.
#include "InitialPacker.h"
#include "BoundaryAwareClusterer.h"
#include "simpleJSON.h"
#include <omp.h>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <tuple>
using Graph = PlacementTimingInfo::TimingGraph<DesignInfo::DesignCell>;
static void need(bool ok,const std::string &s){if(!ok)throw std::runtime_error(s);}
static uint64_t fingerprint(Graph *g) {
    uint64_t h=14695981039346656037ULL;
    auto add=[&](uint64_t x){for(int j=0;j<8;++j){h^=(x>>(j*8))&255;h*=1099511628211ULL;}};
    for(auto n:g->getNodes()) {
        add(n->getId());add(n->checkIsRegister());add(n->getForwardLevel());
        add(n->getBackwardLevel());add(n->getLongestPathLength());
        for(auto e:n->getOutEdges()){add(e->getSource()->getId());add(e->getSink()->getId());}
    }
    for(auto n:g->getPathLenSortedNodes())add(n->getId());
    return h;
}
static int independentPercentile(Graph *g) {
    // Equivalent rank calculation, independently sorted from per-node levels.
    std::vector<int> levels;
    for(auto n:g->getNodes())if(n->getForwardLevel()>=0)levels.push_back(n->getForwardLevel());
    std::sort(levels.begin(),levels.end());
    const size_t rank=g->getNodes().size()*0.95f;
    need(rank>0 && rank<=levels.size(),"95% rank not covered by reachable nodes");
    return levels[rank-1];
}
int main(int argc,char **argv) {
 try {
    need(argc==4,"usage: checkPREGClustering config {getrf|legacy-reference|synthetic|short-chain} result.json");
    omp_set_num_threads(1);auto cfg=parseJSONFile(argv[1]);const std::string mode=argv[2];
    const bool synthetic=mode=="synthetic"||mode=="short-chain",reference=mode=="legacy-reference";
    need(!cfg.count("BoundaryPaperMinCells") && !cfg.count("BoundaryPaperPathLengthThreshold"),"initial config must use automatic coupled threshold");
    need(synthetic||reference||mode=="getrf","unknown mode");
    if(reference) {
        cfg.erase("DSP timing properties file");cfg.erase("DSP timing properties sha256");
        cfg.erase("DSP timing source DCP sha256");
    }
    DeviceInfo device(cfg,cfg.at("device"));DesignInfo design(cfg,&device);PlacementInfo p(&design,&device,cfg);
    if(!synthetic) {InitialPacker packer(&design,&device,&p,cfg);packer.pack();}
    else {
        for(auto c:design.getCells()) {
            if(c->isDSP()) {c->setDSPPREG(1);c->setHasDSPReg(false);}
            auto pu=new PlacementInfo::PlacementUnpackedCell(c->getName(),p.getPlacementUnits().size(),c);
            pu->setWeight(1);pu->setAnchorLocationAndForgetTheOriginalOne(20,320);
            p.getPlacementUnits().push_back(pu);p.getPlacementUnpackedCells().push_back(pu);
            p.getCellId2PlacementUnit()[c->getCellId()]=pu;
        }
        p.updateCells2PlacementUnits();p.reloadNets();
        p.getCompatiblePlacementTable()->setBELTypeForCells(&design);design.updateFFControlSets();
        p.calculateNetNumDistributionOfPUs();p.createGridBins(5,5);p.updateElementBinGrid();
    }
    p.buildSimpleTimingGraph();auto timing=p.getTimingInfo();auto sta=timing->getSimplePlacementTimingGraph();
    const auto staBefore=fingerprint(sta);
    auto cluster=timing->getClusteringGraph();need(cluster!=sta,"graphs alias");
    need(cluster==timing->getClusteringGraph(),"clustering view rebuilt unexpectedly");
    need(fingerprint(sta)==staBefore,"clustering construction mutated STA graph");
    size_t preg1=0,legacyReg=0,disagree=0;
    for(auto n:sta->getNodes()) {
        auto c=n->getDesignNode();auto ln=cluster->getNodes().at(n->getId());
        need(c->getTimingLength()==n->getLongestPathLength() && c->getTimingForwardLevel()==n->getForwardLevel(),"clustering overwrote public timing fields");
        if(c->isDSP()) {
            if(!reference)need(n->checkIsRegister()==(c->getDSPPREG()==1),"STA ignored real PREG");
            need(ln->checkIsRegister()==c->dspClusteringIsRegister(true),"cluster ignored legacy register inference");
            preg1+=c->getDSPPREG()==1;legacyReg+=ln->checkIsRegister();disagree+=n->checkIsRegister()!=ln->checkIsRegister();
        }
    }
    need(cluster->getLongPathThresholdLevel()==independentPercentile(cluster),"cluster threshold not computed from 95% rank");
    need(sta->getLongPathThresholdLevel()==independentPercentile(sta),"STA percentile changed");
    if(!synthetic) {
        need(cluster->getLongPathThresholdLevel()==18,"GETRF legacy 95% threshold changed");
        if(reference)need(fingerprint(sta)==fingerprint(cluster),"no-metadata legacy reference differs between views");
        else {need(sta->getLongPathThresholdLevel()==7,"GETRF real-PREG timing threshold changed");need(preg1==768 && disagree>0,"GETRF classifications unexpectedly identical");}
    } else {
        need(cluster->getLongPathThresholdLevel()!=18,"synthetic percentile was hardcoded to 18");
        PlacementTimingOptimizer opt(&p,cfg);BoundaryAwareClusterer bc(&p,&opt,cfg);
        auto select=[&](int threshold){
            if(threshold>0)cfg["BoundaryPaperPathLengthThreshold"]=std::to_string(threshold);
            else cfg.erase("BoundaryPaperPathLengthThreshold");
            bc.run(true);std::set<int> ids;for(auto e:p.getRegionPreferences())ids.insert(e.first->getId());return ids;
        };
        if(mode=="short-chain") {
            // 17 combinational cells including DSP, L=19: both path thresholds
            // pass, but only coupled minimum 17 accepts this chain.
            need(select(17).size()==16,"17-cell chain rejected at coupled threshold 17");
            need(select(18).empty(),"minimum did not follow path threshold to 18");
        }
        auto accepted=select(0);need(!accepted.empty(),"automatic legacy chain selection failed");
        need(select(1000).empty(),"explicit path threshold ignored");
        need(select(0)==accepted,"override contaminated automatic selection");
        cfg["BoundaryPaperMinCells"]="18";bool rejected=false;
        try{bc.run(true);}catch(const std::exception&){rejected=true;}
        need(rejected,"removed independent minimum was accepted");cfg.erase("BoundaryPaperMinCells");
        need(fingerprint(sta)==staBefore,"cluster selection changed timing graph");
        for(auto pu:p.getPlacementUnits())need(pu->X()==20 && pu->Y()==320,"deferred cluster-only test moved a PU");
    }
    std::ofstream out(argv[3]);out<<"{\"state\":\"passed\",\"mode\":\""<<mode<<"\",\"nodes\":"<<sta->getNodes().size()
      <<",\"timing_threshold\":"<<sta->getLongPathThresholdLevel()<<",\"cluster_threshold\":"<<cluster->getLongPathThresholdLevel()
      <<",\"percentile\":0.95,\"minimum_source\":\"path-threshold\",\"minimum_cells\":"<<cluster->getLongPathThresholdLevel()<<",\"preg1\":"<<preg1<<",\"legacy_dsp_registers\":"<<legacyReg<<",\"disagree\":"<<disagree
      <<",\"timing_fingerprint_fnv64\":\""<<fingerprint(sta)<<"\",\"cluster_fingerprint_fnv64\":\""<<fingerprint(cluster)
      <<"\",\"placement_executed\":false,\"routing_executed\":false}\n";
    std::cout<<"PASS true PREG timing and legacy depth; coupled automatic minimum, public timing fields, no placement\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
