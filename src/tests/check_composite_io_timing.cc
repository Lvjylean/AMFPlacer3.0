#include "PlacementTimingInfo.h"
#include <cmath>
#include <iostream>
#include <omp.h>
#include <stdexcept>

static void need(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    need(argc == 2, "Expected existing report directory");
    omp_set_num_threads(4);
    using D = DesignInfo;
    using G = PlacementTimingInfo::TimingGraph<D::DesignCell>;
    std::map<std::string, std::string> cfg{{"CompositeIOTimingBoundary", "true"}, {"ClockPeriod", "3.332"}};
    cfg["dumpDirectory"] = argv[1];
    PlacementTimingInfo owner(nullptr, nullptr, cfg);
    G graph(&owner);
    D::DesignCell source(std::string("tx"), D::CellType_FDRE, 0), io(std::string("pad"), D::CellType_IOBUFE3, 1), sink(std::string("rx"), D::CellType_FDRE, 2);
    D::DesignCell mmcm(std::string("mmcm"), D::CellType_MMCME4_ADV, 3), pll(std::string("pll"), D::CellType_PLLE4_ADV, 4);
    need(mmcm.isIO() && pll.isIO() && D::isIO(D::CellType_MMCME4_ADV) && D::isIO(D::CellType_PLLE4_ADV), "E4 type mapping");
    need(io.isCompositeIO() && !mmcm.isCompositeIO(), "composite I/O classification");
    D::DesignCell mid(std::string("logic"), D::CellType_LUT1, 3), fast(std::string("fast"), D::CellType_FDRE, 4);
    for (auto cell : {&source, &io, &sink, &mid, &fast}) {
        auto node = new G::TimingNode(cell, graph.getNodes().size());
        if (cell != &mid) node->setIsRegister();
        node->setClockPeriod(cell == &fast ? 0.75f : 3.332f);
        if (cell == &io) node->setInnerDelay(0);
        graph.insertTimingNode(node);
    }
    std::string qn="tx/Q", qr="Q", in="pad/I", ir="I", on="pad/O", orf="O", dn="rx/D", dr="D";
    D::DesignPin q(qn, qr, D::PinType_Q, false, &source, 0);
    D::DesignPin i(in, ir, D::PinType_Others, true, &io, 1);
    D::DesignPin o(on, orf, D::PinType_Others, false, &io, 2);
    D::DesignPin d(dn, dr, D::PinType_D, true, &sink, 3);
    std::string min="logic/I0", mir="I0", mon="logic/O", mor="O", fn="fast/D", fr="D";
    D::DesignPin mi(min, mir, D::PinType_LUTInput, true, &mid, 4);
    D::DesignPin mo(mon, mor, D::PinType_LUTOutput, false, &mid, 5);
    D::DesignPin fd(fn, fr, D::PinType_D, true, &fast, 6);
    graph.addEdgeBetween(1, 3, &o, &mi, nullptr, 0.2f);
    graph.addEdgeBetween(3, 4, &mo, &fd, nullptr, 0.3f);
    graph.addEdgeBetween(0, 1, &q, &i, nullptr, 7.0f);
    graph.addEdgeBetween(1, 2, &o, &d, nullptr, 1.4f);
    graph.forwardLevelization();graph.backwardLevelization();
    for (int iteration = 0; iteration < 100; ++iteration) {
        graph.propogateArrivalTime();graph.backPropogateRequiredArrivalTime();
        need(std::fabs(graph.getNodes()[1]->getLatestInputArrival()-7.1f) < 1e-5, "direct FF to I/O capture was ignored");
        need(graph.getNodes()[1]->getLatestOutputArrival() == 0, "capture propagated into I/O launch");
        need(std::fabs(graph.getNodes()[2]->getLatestInputArrival()-1.4f) < 1e-5, "I/O launch contaminated by transmission arrival");
        need(graph.getNodes()[1]->getRequiredArrivalTime() < 3.333f, "capture target lost");
    }
    std::ifstream report(std::string(argv[1])+"/io_boundary_estimates.tsv");
    std::string line;bool checkedReceive=false;
    while (std::getline(report,line)) {
        std::istringstream row(line);std::string cell,pin,role;float delay,period,margin;
        if (!(row>>cell>>pin>>role>>delay>>period>>margin) || role!="receive_path") continue;
        need(std::fabs(delay-0.6f)<1e-5 && std::fabs(period-0.75f)<1e-5 && std::fabs(margin-0.15f)<1e-5,
             "receive report did not select the tightest per-domain path budget");
        checkedReceive=true;
    }
    need(checkedReceive,"missing receive path report");
    std::string netName="direct_chain";D::DesignNet net(netName, 0);
    net.setAsyncProximityWeight(4);net.resetEnhanceRatio();
    need(net.getOverallEnhanceRatio() == 4, "synchronizer proximity disappeared after timing reset");
    std::cout << "PASS E4 types, independent I/O launch/capture, direct timing edges, repeated parallel propagation, persistent proximity\n";
}
