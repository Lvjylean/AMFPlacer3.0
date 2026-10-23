#pragma once
#include "SllQuantityModel.h"
#include "QPSolverWrapper.h"

namespace amf_sll {
// Own the pre-injection matrix, since the unguarded solver can append entries.
// Original QP guard repairs are reported separately; they are not SLL curvature.
struct FrozenAxis {
    std::vector<double> position, gradient, linear;
    std::vector<float> diagonal;
    std::vector<Eigen::Triplet<float>> entries;
    void capture(const QPSolverWrapper &solver, const std::vector<double>& p) {
        position=p; const auto &d=solver.solverData;
        diagonal=d.objectiveMatrixDiag; entries=d.objectiveMatrixTripletList;
        linear.assign(d.objectiveVector.data(), d.objectiveVector.data()+p.size());
        gradient=linear;
        for(size_t i=0;i<p.size();++i) gradient[i]+=diagonal[i]*p[i];
        for(const auto&e:entries) {
            gradient[e.row()]+=0.5*e.value()*p[e.col()];
            gradient[e.col()]+=0.5*e.value()*p[e.row()];
        }
    }
    double change(const std::vector<double>& p) const {
        std::vector<double> d(p.size()); double q=0;
        for(size_t i=0;i<p.size();++i) {
            d[i]=p[i]-position[i]; q+=gradient[i]*d[i]+0.5*diagonal[i]*d[i]*d[i];
        }
        for(const auto&e:entries)q+=0.5*e.value()*d[e.row()]*d[e.col()];
        return q;
    }
};

struct NetDetail {
    std::vector<double> y,z,dz,pplus,pminus,gradient;
    double value=0, minimum=0, maximum=0;
    int discrete=0;
};
// Read-only pin decomposition; tests compare it to the production evaluator.
inline NetDetail detail(const Model& m, const Net& n, const std::vector<double>& positions) {
    NetDetail d; size_t count=n.end-n.begin;
    d.y.resize(count);d.z.assign(count,0);d.dz.assign(count,0);
    d.pplus.resize(count);d.pminus.resize(count);d.gradient.assign(count,0);
    if(!count)return d;
    for(size_t j=0;j<count;++j) {
        auto &p=m.pins[n.begin+j];d.y[j]=positions[p.unit]+p.offsetY;
        for(size_t b=0;b<m.boundaries.size();++b) {
            double s=sigmoid((d.y[j]-m.boundaries[b])/m.tau[b]);
            d.z[j]+=s;d.dz[j]+=s*(1-s)/m.tau[b];
        }
    }
    d.minimum=*std::min_element(d.y.begin(),d.y.end());
    d.maximum=*std::max_element(d.y.begin(),d.y.end());
    d.discrete=m.slr(d.maximum)-m.slr(d.minimum);
    double zlo=*std::min_element(d.z.begin(),d.z.end()), zhi=*std::max_element(d.z.begin(),d.z.end());
    double sp=0,sm=0,zp=0,zm=0;
    for(size_t j=0;j<count;++j) {
        d.pplus[j]=std::exp((d.z[j]-zhi)/m.gamma); d.pminus[j]=std::exp((zlo-d.z[j])/m.gamma);
        sp+=d.pplus[j];sm+=d.pminus[j];zp+=d.z[j]*d.pplus[j];zm+=d.z[j]*d.pminus[j];
    }
    double mup=zp/sp,mum=zm/sm;d.value=n.weight*(mup-mum);
    for(size_t j=0;j<count;++j) {
        d.pplus[j]/=sp;d.pminus[j]/=sm;
        d.gradient[j]=n.weight*(d.pplus[j]*(1+(d.z[j]-mup)/m.gamma)
                              -d.pminus[j]*(1-(d.z[j]-mum)/m.gamma))*d.dz[j];
    }
    return d;
}
}
