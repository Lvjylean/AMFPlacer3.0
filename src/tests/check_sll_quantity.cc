#include "../lib/HiFPlacer/placement/globalPlacement/SllQuantityModel.h"
#include <iomanip>
#include <iostream>
#include <random>
#include <numeric>

using namespace amf_sll;
static void require(bool x, const char *why) { if (!x) throw std::runtime_error(why); }
static Model base(unsigned n) {
    Model m; m.units = n; m.boundaries = {100, 200, 300}; m.tau = {20, 15, 25};
    m.minY = 0; m.maxY = 400; return m;
}
int main() {
    unsigned tests = 0;
    // Every nonempty 1x4 occupancy mask, exact seam convention, fanout sharing.
    for (unsigned mask = 1; mask < 16; ++mask) {
        Model m = base(4);
        int lo = 4, hi = -1;
        for (unsigned s = 0; s < 4; ++s) if (mask & (1u << s)) {
            m.pins.push_back({s, 0}); lo = std::min(lo, int(s)); hi = std::max(hi, int(s));
        }
        m.nets.push_back({0, m.pins.size(), 0}); m.validate();
        auto r = m.evaluate({50, 150, 250, 350}, {1, 1, 1, 1}, 4, true);
        require(r.discrete == hi - lo, "1x4 occupancy count");
        require(std::accumulate(r.seams.begin(), r.seams.end(), 0LL) == r.discrete, "seam conservation");
        ++tests;
    }
    Model m = base(4);
    m.pins = {{0, -2}, {0, 3}, {1, 0}, {2, 1}, {1, -4}, {3, 0}};
    m.nets = {{0, 4, 5, true, 1.7}, {4, 6, 7, true, 0.6}};
    m.validate();
    std::vector<double> y = {95, 203, 305, 162};
    std::vector<unsigned char> movable(4, 1);
    double maxError = 0;
    std::mt19937 rng(13);
    std::uniform_real_distribution<double> pos(30, 370);
    for (int trial = 0; trial < 60; ++trial) {
        for (double &v : y) v = pos(rng);
        const auto analytic = m.evaluate(y, movable, 1, true);
        const auto parallel = m.evaluate(y, movable, 4, true);
        require(std::abs(analytic.smooth - parallel.smooth) < 1e-12, "parallel cost");
        for (unsigned i = 0; i < y.size(); ++i) {
            require(std::abs(analytic.gradient[i] - parallel.gradient[i]) < 1e-12, "parallel gradient");
            auto yp = y, ym = y; yp[i] += 1e-4; ym[i] -= 1e-4;
            const double numeric = (m.evaluate(yp, movable, 1, false).smooth - m.evaluate(ym, movable, 1, false).smooth) / 2e-4;
            maxError = std::max(maxError, std::abs(numeric - analytic.gradient[i]));
        }
        ++tests;
    }
    require(maxError < 1e-8, "finite difference gradient including macro offsets");
    movable[0] = 0;
    require(m.evaluate(y, movable, 2, true).gradient[0] == 0, "fixed PU gradient"); ++tests;
    Model many = base(2);
    many.pins.push_back({0, 0});
    for (int i = 0; i < 4000; ++i) many.pins.push_back({1, 0});
    many.nets = {{0, many.pins.size(), 9, true, 1}};
    many.validate();
    require(many.evaluate({50, 350}, {1, 1}, 4, true).discrete == 3, "shared high-fanout count"); ++tests;
    many.nets[0].optimize = false;
    auto filtered = many.evaluate({50, 350}, {1, 1}, 2, true);
    require(filtered.discrete == 3 && filtered.discreteOptimized == 0 && filtered.smooth == 0, "statistics retain excluded optimization nets"); ++tests;
    many.nets[0].optimize = true; many.gamma = 1e-6;
    require(std::isfinite(many.evaluate({50, 350}, {1, 1}, 2, true).smooth), "stable small-temperature exponents"); ++tests;
    auto unknown = many.evaluate({50, 450}, {1, 1}, 2, true);
    require(unknown.invalidNets == 1 && unknown.validNets == 0, "out-of-domain explicitly unknown"); ++tests;
    require(many.slr(100) == 0 && many.slr(100.00001) == 1, "physical exact boundary convention"); ++tests;
    // Exercise shared-PU reductions across many nets, rather than only thread-private nets.
    Model large = base(128);
    for (unsigned n = 0; n < 5000; ++n) {
        auto begin = large.pins.size();
        for (unsigned j = 0; j < 3 + n % 8; ++j) large.pins.push_back({(n * 7 + j * 3) % 128, double(j) / 10});
        large.nets.push_back({begin, large.pins.size(), n});
    }
    large.validate(); std::vector<double> ly(128); std::vector<unsigned char> lm(128, 1);
    for (double &v : ly) v = pos(rng);
    auto serial = large.evaluate(ly, lm, 1, true), parallel = large.evaluate(ly, lm, 8, true);
    require(serial.discrete == parallel.discrete && std::abs(serial.smooth-parallel.smooth) < 1e-8, "large parallel objective");
    double parallelError = 0;
    for (unsigned i=0; i<128; ++i) parallelError=std::max(parallelError,std::abs(serial.gradient[i]-parallel.gradient[i]));
    require(parallelError < 1e-10, "shared-PU parallel reductions"); ++tests;
    // Equality, not a tolerance: jobs must not change SLL arithmetic order.
    for (int workers : {1, 2, 8, 12, 16, 12, 16}) {
        const auto other = large.evaluate(ly, lm, workers, true);
        require(other.smooth == serial.smooth && other.gradient == serial.gradient &&
                other.discrete == serial.discrete && other.seams == serial.seams &&
                other.discreteOptimized == serial.discreteOptimized &&
                other.validNets == serial.validNets && other.invalidNets == serial.invalidNets,
                "worker count changed SLL objective or gradient");
        require(large.evaluate(ly, lm, workers, false).smooth == serial.smooth,
                "cost-only evaluation changed SLL objective");
        ++tests;
    }
    auto empty = base(1); empty.validate();
    require(empty.evaluate({50}, {1}, 16, true).smooth == 0, "empty net list"); ++tests;
    // Local QP signs and expansion point are independently checked by differentiation.
    std::vector<double> diag = {2, 3}, linear = {-10, -20}, yy = {4, 5}, gg = {-0.4, 0.7};
    addLocalModel(yy, gg, {1, 0}, 2, 0.5, diag, linear);
    require(std::abs(diag[0]*yy[0]+linear[0] - (2*yy[0]-10+2*gg[0])) < 1e-12, "QP gradient/sign");
    require(diag[1] == 3 && linear[1] == -20, "fixed QP DOF unchanged"); ++tests;
    auto d0=diag, l0=linear; addLocalModel(yy,gg,{1,1},0,0.5,diag,linear);
    require(diag==d0 && linear==l0, "lambda zero exact no-op"); ++tests;
    // Synthetic crossing: a weakly anchored source just below the seam, a fixed sink above it.
    Model crossing=base(2); crossing.pins={{0,0},{1,0}}; crossing.nets={{0,2,0}}; crossing.validate();
    std::vector<double> cy={95,150}; const std::vector<unsigned char> cm={1,0};
    const auto before=crossing.evaluate(cy,cm,2,true);
    unsigned accepted=0;
    for (int iteration=0;iteration<40;++iteration) {
        auto r=crossing.evaluate(cy,cm,2,true);
        std::vector<double> a={0.001,0}, b={-0.095,0};
        addLocalModel(cy,r.gradient,cm,10,0.005,a,b);
        double delta=std::max(-5.0,std::min(5.0,-b[0]/a[0]-cy[0]));
        for(int bt=0;bt<16;++bt) {
            auto trial=cy;trial[0]+=delta;
            auto candidate=crossing.evaluate(trial,cm,2,false);
            double change=0.0005*((trial[0]-95)*(trial[0]-95)-(cy[0]-95)*(cy[0]-95))+10*(candidate.smooth-r.smooth);
            if(change<=1e-12){cy=trial;++accepted;break;}
            delta*=0.5;
        }
    }
    const auto after=crossing.evaluate(cy,cm,2,false);
    require(before.discrete==1 && after.discrete==0 && after.smooth<before.smooth && cy[1]==150 && accepted, "QP crossing and true objective acceptance"); ++tests;
    bool invalid=false;try{auto bad=m;bad.gamma=0;bad.validate();}catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"invalid parameter validation"); ++tests;
    std::cout << std::setprecision(14) << "{\"result\":\"PASS\",\"tests\":"<<tests
              <<",\"finite_difference_max_abs_error\":"<<maxError<<",\"parallel_max_abs_error\":"<<parallelError
              <<",\"synthetic_discrete_before\":"<<before.discrete<<",\"synthetic_discrete_after\":"<<after.discrete
              <<",\"synthetic_source_y\":"<<cy[0]<<"}\n";
}
