#include "PlacementTimingInfo.h"
#include "PlacementInfo.h"
#include "InitialPacker.h"
#include "CLBSiteLegality.h"
#include "simpleJSON.h"
#include <cmath>
#include <fstream>
#include <omp.h>
#include <set>
#include <stdexcept>

using D = DesignInfo;
using G = PlacementTimingInfo::TimingGraph<D::DesignCell>;
static void need(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
static void near(float got, float expected, const char *why) {
    need(std::fabs(got-expected)<1e-4f, std::string(why)+": "+std::to_string(got));
}

static void fixture(G *g, D &design) {
    auto node = [&](std::string name) { return g->getNodes().at(design.getCell(name)->getCellId()); };
    auto a=node("gate0"), b=node("gate1"), sink=node("sink"), fan=node("fan");
    need(a->getForwardLevel()==1 && b->getForwardLevel()==2, "AND chain forward levels");
    need(a->getBackwardLevel()==2 && b->getBackwardLevel()==1, "AND chain backward levels");
    // Alternate which input is critical; SRI is also a real data path here.
    for (bool sriSlow : {false, true}) {
        for (auto e : g->getEdges()) {
            const auto name=e->getSink()->getDesignNode()->getName();
            if (name=="gate0") e->setDelay((e->getSinkPin()->getRefPinName()=="SRI")==sriSlow ? 2.f : .2f);
            else if (name=="gate1") e->setDelay(e->getSinkPin()->getRefPinName()=="DI" ? .3f : .1f);
            else if (name=="sink") e->setDelay(.5f);
            else if (name=="fan") e->setDelay(.8f);
        }
        for (int iteration=0; iteration<20; ++iteration) {
            g->propogateArrivalTime();
            g->backPropogateRequiredArrivalTime();
            near(a->getLatestInputArrival(),2.1f,"DI/SRI input propagation");
            near(b->getLatestInputArrival(),2.5f,"combinational chain propagation");
            near(sink->getLatestInputArrival(),3.1f,"capture after two ANDs");
            near(fan->getLatestInputArrival(),3.0f,"fanout capture");
            near(a->getRequiredArrivalTime(),2.332f,"budget through both ANDs");
            near(b->getRequiredArrivalTime(),2.732f,"budget through final AND");
            auto launch=node(sriSlow ? "launch_s" : "launch_d");
            near(launch->getEndpointCriticality(),.232f,"launch criticality through AND");
            std::vector<int> expected{sink->getId(),b->getId(),a->getId(),launch->getId()};
            need(g->backTraceDelayLongestPathFromNode(sink->getId())==expected,"critical path cut at AND");
        }
    }
    // The two ANDs share SRI but have opposite polarity: preserve physical checks.
    auto ca=a->getDesignNode(), cb=b->getDesignNode();
    need(!ca->getSRInverted() && cb->getSRInverted(),"fixture polarity import");
    struct Mapping { D::DesignCell *FFs[2][2][4]={}; } slots;
    slots.FFs[0][0][0]=ca;
    need(!CLBSiteLegality::canPlaceFF(slots,cb,0,1),"opposite SRI polarity shared a half SLICE");
    need(CLBSiteLegality::canPlaceFF(slots,cb,1,0),"independent half SLICE rejected");
}

int main(int argc, char **argv) { try {
    need(argc==4,"usage: checkAND2B1LTiming config fixture|real report.tsv");
    const bool small=std::string(argv[2])=="fixture";
    need(small || std::string(argv[2])=="real","unknown mode");
    omp_set_num_threads(small ? 4 : 12);
    auto cfg=parseJSONFile(argv[1]);
    DeviceInfo device(cfg,cfg.at("device")); D design(cfg,&device);
    PlacementInfo pi(&design,&device,cfg);
    InitialPacker packer(&design,&device,&pi,cfg); packer.pack();
    PlacementTimingInfo timing(&design,&device,cfg); timing.buildSimpleTimingGraph();
    std::ofstream report(argv[3]); need(bool(report),"cannot open report");
    report << "view\tcell\tis_register\tforward_level\tbackward_level\tdi_edges\tsri_edges\tout_edges\tphysical_sr_mode\n";
    for (bool clustering : {false,true}) {
        auto g=clustering ? timing.getClusteringGraph() : timing.getSimplePlacementTimingGraph();
        int count=0;
        for (auto n : g->getNodes()) {
            auto c=n->getDesignNode();
            if(c->getCellType()!=D::CellType_AND2B1L) continue;
            ++count;
            need(c->isFF() && D::isFF(c->getCellType()),"physical FF class changed");
            need(D::getFFSRType(c->getCellType())==3 && c->getControlSetInfo(),"physical control mode lost");
            need(!c->isTimingEndPoint() && !n->checkIsRegister(),"AND2B1L incorrectly cuts timing path");
            need(n->getForwardLevel()>0 && n->getBackwardLevel()>0,"AND absent from combinational levels");
            std::map<std::string,int> legs;
            for(auto e:n->getInEdges()) {
                auto p=e->getSinkPin(); ++legs[p->getRefPinName()];
                if(p->getRefPinName()=="SRI")
                    need(p->getPinType()==D::PinType_SR && c->getControlSetInfo()->getSR()==p->getNet(),
                         "SRI physical resource mapping changed");
            }
            need(legs["DI"]==1 && legs["SRI"]==1 && legs.size()==2,"missing DI/SRI graph edge");
            need(!n->getOutEdges().empty(),"missing O graph edge");
            report << (clustering ? "clustering" : "timing") << '\t' << c->getName() << '\t'
                   << n->checkIsRegister() << '\t' << n->getForwardLevel() << '\t' << n->getBackwardLevel()
                   << '\t' << legs["DI"] << '\t' << legs["SRI"] << '\t' << n->getOutEdges().size() << "\t3\n";
        }
        need(count==(small ? 2 : 4),"unexpected AND2B1L count");
        if(small) fixture(g,design);
    }
    // True FFs and latches retain their timing boundary classification.
    for(auto type:{D::CellType_FDRE,D::CellType_FDSE,D::CellType_FDCE,D::CellType_FDPE,D::CellType_LDCE}) {
        D::DesignCell c(std::string("boundary"),type,0);
        need(c.isTimingEndPoint(),"sequential endpoint classification changed");
    }
    std::cout << "PASS AND2B1L " << (small ? "DI/SRI forward/backward propagation, critical paths, polarity packing" : "four real cells, both graph views, initial packing") << '\n';
} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
