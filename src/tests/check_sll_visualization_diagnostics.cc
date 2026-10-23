#include "SllDiagnosticMath.h"
#include <iostream>
#include <stdexcept>
int main(){
    using namespace amf_sll;size_t checks=0;
    auto check=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
    Model m;m.units=4;m.minY=-.5;m.maxY=959.5;m.boundaries={239.5,479.5,719.5};m.tau={10,10,10};m.gamma=.02;
    m.pins={{0,0},{1,1.25},{2,-2},{3,0},{2,4}};m.nets={{0,5,0,true,1}};m.validate();
    std::vector<unsigned char> movable(4,1);
    for(auto y:std::vector<std::vector<double>>{{180,239.5,470,840},{239.49,239.5,239.51,240},{0,0,0,0},{-50,300,721,1000}}){
        std::vector<double> strength;auto result=m.evaluate(y,movable,4,true,&strength);auto d=detail(m,m.nets[0],y);
        check(std::abs(result.smooth-d.value)<1e-12,"trace F differs");
        std::vector<double> summed(4,0);double strongest=0;
        for(size_t j=0;j<m.pins.size();++j){summed[m.pins[j].unit]+=d.gradient[j];strongest=std::max(strongest,std::abs(d.gradient[j]));check(std::isfinite(d.gradient[j]),"non-finite small gamma gradient");}
        check(std::abs(strength[0]-strongest)<1e-12,"dynamic ranking mismatch");
        for(size_t i=0;i<4;++i){check(std::abs(summed[i]-result.gradient[i])<1e-12,"pin/PU aggregation mismatch");auto a=y,b=y;double e=1e-4;a[i]+=e;b[i]-=e;double fd=(m.evaluate(a,movable,1,false).smooth-m.evaluate(b,movable,1,false).smooth)/(2*e);check(std::abs(fd-result.gradient[i])<2e-8,"finite difference mismatch");}
        auto serial=m.evaluate(y,movable,1,true);check(serial.gradient==result.gradient,"thread determinism");
    }
    // Exact frozen-QP displacement algebra, including coupled coordinates.
    QPSolverWrapper q(true,false,-100,100,2,false);
    q.solverData.objectiveMatrixDiag={3,4};q.solverData.objectiveMatrixTripletList={{0,1,-1},{1,0,-1}};
    q.solverData.objectiveVector<<2,-3;FrozenAxis f;f.capture(q,{5,7});
    auto energy=[](double x,double y){return .5*(3*x*x+4*y*y-2*x*y)+2*x-3*y;};
    check(std::abs(f.change({6,3})-(energy(6,3)-energy(5,7)))<1e-12,"frozen delta mismatch");
    q.solverData.objectiveMatrixTripletList.push_back({0,0,100});q.solverData.objectiveVector<<99,99;
    check(std::abs(f.change({6,3})-(energy(6,3)-energy(5,7)))<1e-12,"frozen snapshot changed");
    std::vector<double> linear={2,-3};std::vector<unsigned char> mask={1,0};
    addFirstOrderModel({.1,.2},mask,firstOrderCoefficient(.4),linear);
    check(std::abs(linear[0]-4.4)<1e-12&&linear[1]==-3,"first-order b units/fixed mask");
    std::cout<<"PASS "<<checks<<" checks: tau=10 gamma=.02, finite differences, repeated-PU pin aggregation, fixed masks, stable exponentials, trace decomposition, frozen QP\n";
}
