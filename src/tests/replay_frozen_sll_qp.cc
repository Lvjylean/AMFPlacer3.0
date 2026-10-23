#include "FrozenQpArchive.h"
#include <iostream>
using namespace amf_sll_audit;
int main(int argc,char**argv){try{
    if(argc<3||argc>4)throw std::invalid_argument("usage: replayFrozenSllQp SNAPSHOT OUTPUT.json [coefficient=24]");
    std::filesystem::path dir=argv[1],output=argv[2];
    if(!std::filesystem::exists(dir/"COMPLETE.json"))throw std::runtime_error("Incomplete archive: missing COMPLETE.json");
    if(std::filesystem::exists(output))throw std::runtime_error("Refusing output overwrite");
    size_t end=0;double c=argc==4?std::stod(argv[3],&end):24.;
    if(!std::isfinite(c)||c<=0||(argc==4&&end!=std::string(argv[3]).size()))throw std::invalid_argument("Invalid coefficient");
    auto f=readFrozen(dir/"original-y.bin");auto m=readModel(dir/"model.bin");auto e=readEffective(dir/"effective-y.bin");
    if(!f.unconstrained)throw std::runtime_error("Offline raw displacement currently requires unconstrained CG");
    const size_t n=m.units;if(f.y.position.size()!=n||size_t(e.matrix.rows())!=n)throw std::runtime_error("Archive dimension mismatch");
    auto recorded=m.evaluate(f.y.position,f.movable,2,true);double gerror=0;
    for(size_t i=0;i<n;++i){if(!std::isfinite(f.y.position[i])||!std::isfinite(f.y.linear[i])||!std::isfinite(f.sll[i]))throw std::runtime_error("Nonfinite frozen vector");gerror=std::max(gerror,std::abs(recorded.gradient[i]-f.sll[i]));}
    if(gerror>1e-11)throw std::runtime_error("Archived SLL gradient does not match full model");
    Eigen::SparseMatrix<double> asym=e.matrix-Eigen::SparseMatrix<double>(e.matrix.transpose());
    if(asym.norm()>1e-12*std::max(1.,e.matrix.norm()))throw std::runtime_error("Asymmetric effective matrix");
    Eigen::VectorXd b0=e.objective;
    for(size_t i=0;i<n;++i)b0[i]-=f.injectedCoefficient*f.sll[i];
    // The effective matrix is observed from the real solver AFTER its existing
    // guard. Removing the exact injected vector preserves the same guard anchors.
    std::vector<Eigen::Triplet<double>> originalEntries;
    for(auto&t:f.y.entries)originalEntries.emplace_back(t.row(),t.col(),double(t.value()));
    for(size_t i=0;i<n;++i)originalEntries.emplace_back(i,i,double(f.y.diagonal[i]));
    Eigen::SparseMatrix<double>a0(n,n);a0.setFromTriplets(originalEntries.begin(),originalEntries.end());
    Eigen::SparseMatrix<double> repair=e.matrix-a0;double offRepair=0,bRepairError=0;
    for(int k=0;k<repair.outerSize();++k)for(Eigen::SparseMatrix<double>::InnerIterator it(repair,k);it;++it)if(it.row()!=it.col())offRepair=std::max(offRepair,std::abs(it.value()));
    for(size_t i=0;i<n;++i){
        double anchor=repair.coeff(i,i)*f.warmY[i],injection=f.injectedCoefficient*f.sll[i];
        double expected=(f.y.linear[i]+injection)-anchor;
        double error=std::abs(e.objective[i]-expected);bRepairError=std::max(bRepairError,error);
        double tolerance=64*std::numeric_limits<double>::epsilon()*(1+std::abs(f.y.linear[i])+std::abs(injection)+std::abs(anchor));
        if(error>tolerance)throw std::runtime_error("Effective objective does not match recorded injection and guard");
        b0[i]=f.y.linear[i]-anchor;
    }
    if(offRepair>1e-12)throw std::runtime_error("Effective guard unexpectedly changed off-diagonal entries");
    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>,Eigen::Lower|Eigen::Upper>cg;
    cg.setTolerance(1e-10);cg.setMaxIterations(10000);cg.compute(e.matrix);
    auto solve=[&](const Eigen::VectorXd&rhs){Eigen::VectorXd x=cg.solve(rhs);double residual=(e.matrix*x-rhs).norm()/std::max(1e-30,rhs.norm());if(cg.info()!=Eigen::Success||!x.allFinite()||residual>1e-8)throw std::runtime_error("Offline solve not converged; no displacement claim");return x;};
    Eigen::VectorXd base=solve(-b0),current;
    std::ostringstream out;out<<std::setprecision(17)<<"{\"schema\":1,\"scope\":\"offline frozen full-Y-system stationary solutions, before writeback/clipping; not a placement trajectory\",\"coefficient\":"<<c<<",\"archived_coefficient\":"<<f.coefficient<<",\"archived_injected_coefficient\":"<<f.injectedCoefficient<<",\"model_gradient_error\":"<<gerror<<",\"guard_reconciliation_error\":"<<bRepairError<<",\"offline_tolerance\":1e-10,\"original_tolerance\":"<<f.tolerance<<",\"original_limit\":"<<f.limit<<",\"variants\":[";
    const std::vector<double> taus={m.tau.front(),.2,.1};
    for(size_t v=0;v<taus.size();++v){auto model=m;if(v)model.tau.assign(m.tau.size(),taus[v]);auto g=model.evaluate(f.y.position,f.movable,2,true);
        Eigen::VectorXd s(n);for(size_t i=0;i<n;++i)s[i]=c*g.gradient[i];
        Eigen::VectorXd delta=solve(-s),candidate=base+delta;
        double residual=(e.matrix*delta+s).norm()/std::max(1e-30,s.norm());if(v==0)current=delta;
        std::vector<double> y(candidate.data(),candidate.data()+n);auto after=model.evaluate(y,f.movable,2,false);
        Eigen::Map<const Eigen::VectorXd> start(f.y.position.data(),n),linear(f.y.linear.data(),n);
        Eigen::VectorXd originalGradient=a0*start+linear;
        Eigen::VectorXd d=candidate-start;double dq=d.dot(a0*start+linear)+.5*d.dot(a0*d);
        if(v)out<<',';out<<"{\"tau\":"<<taus[v]<<",\"F_start\":"<<g.smooth<<",\"F_raw_candidate\":"<<after.smooth<<",\"raw_invalid_nets\":"<<after.invalidNets<<",\"dQ_original\":"<<dq<<",\"c_dF\":"<<c*(after.smooth-g.smooth)<<",\"dJ_original\":"<<dq+c*(after.smooth-g.smooth)<<",\"extra_delta_norm\":"<<delta.norm()<<",\"extra_delta_max\":"<<delta.lpNorm<Eigen::Infinity>()<<",\"relative_residual\":"<<residual<<",\"targets\":[";
        for(size_t k=0;k<f.targets.size();++k){auto i=f.targets[k];if(k)out<<',';out<<"{\"pu\":"<<i<<",\"all_net_SLL_gradient\":"<<s[i]<<",\"original_gradient_y\":"<<originalGradient[i]<<",\"effective_diagonal\":"<<e.matrix.coeff(i,i)<<",\"guard_diagonal\":"<<repair.coeff(i,i)<<",\"guard_gradient_at_start\":"<<repair.coeff(i,i)*(f.y.position[i]-f.warmY[i])<<",\"raw_no_SLL_y\":"<<base[i]<<",\"raw_with_SLL_y\":"<<candidate[i]<<",\"extra_y_vs_no_SLL\":"<<delta[i]<<",\"extra_y_vs_current_tau\":"<<delta[i]-current[i]<<"}";}
        out<<"],\"incident_net_contributions\":[";
        bool firstNet=true;std::set<uint32_t> targets(f.targets.begin(),f.targets.end());
        for(auto&net:model.nets){bool incident=false;for(size_t j=net.begin;j<net.end;++j)if(targets.count(model.pins[j].unit)){incident=true;break;}if(!incident)continue;
            auto detail=amf_sll::detail(model,net,f.y.position);if(!firstNet)out<<',';firstNet=false;
            out<<"{\"net_id\":"<<net.id<<",\"optimize\":"<<int(net.optimize)<<",\"gradient_active\":"<<int(net.gradientActive)<<",\"F\":"<<detail.value<<",\"S\":"<<detail.discrete<<",\"target_contributions\":[";
            for(size_t k=0;k<f.targets.size();++k){if(k)out<<',';double v=0;for(size_t j=net.begin;j<net.end;++j)if(model.pins[j].unit==f.targets[k]&&f.movable[f.targets[k]]&&net.optimize&&net.gradientActive)v+=c*detail.gradient[j-net.begin];out<<v;}
            out<<"]}";
        }
        out<<"]}";
    }
    out<<"]}\n";std::ofstream file(output);file<<out.str();file.close();if(!file)throw std::runtime_error("Output write failed");std::cout<<"OFFLINE_REPLAY_PASSED "<<output<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
