#include "FrozenQpArchive.h"
#include "simpleJSON.h"
#include <iostream>
using namespace amf_sll_audit;
int main(int argc,char**argv){try{
    if(argc==3&&std::string(argv[1])=="--parse-config"){
        auto cfg=parseJSONFile(argv[2]);
        if(parseQps(cfg.at("SllQpAuditQps"))!=std::set<size_t>({53,54,55})||cfg.at("SllQuantityTau")!="10"||cfg.at("SllQuantityGamma")!="0.02"||cfg.at("y2xRatio")!="0.4")throw std::runtime_error("Parsed diagnostic/algorithm parameters changed");
        std::cout<<"PASS real legacy JSON parser: QP 53/54/55, tau10 gamma0.02 y2x0.4; no design loaded\n";return 0;
    }
    if(argc!=2)throw std::runtime_error("usage: checkFrozenQpAudit NEW_OUTPUT_DIRECTORY");
    std::filesystem::path dir=argv[1];if(!std::filesystem::create_directory(dir))throw std::runtime_error("Refusing fixture overwrite");
    size_t count=0;auto check=[&](bool v,const char*s){++count;if(!v)throw std::runtime_error(s);};
    auto rejects=[&](const std::function<void()>&fn){bool thrown=false;try{fn();}catch(const std::exception&){thrown=true;}check(thrown,"Expected rejection");};
    check(parseQps("53;54;55")==std::set<size_t>({53,54,55}),"Legacy config compatible QP selection");
    check(parseQps("53,54,55")==std::set<size_t>({53,54,55}),"QP selection");
    for(auto s:{"","0","1,1","1,","-1","1x","1,,2","53;54,55","1;","1;;2","0;1"})rejects([&]{parseQps(s);});
    check(json("a\tb\"c\\d\n")=="\"a\\u0009b\\\"c\\\\d\\u000a\"","Lossless identity escaping");
    amf_sll::Model m;m.units=4;m.minY=-.5;m.maxY=959.5;m.boundaries={239.5,479.5,719.5};m.tau={10,10,10};m.gamma=.02;
    m.pins={{0,0},{1,0},{0,.1},{2,-.25},{3,0},{1,.2},{2,0}};
    m.nets={{0,2,7,true,1},{2,5,8,true,1.7},{5,7,9,false,1}};m.validate();
    std::vector<double> pos={239.99330139160156,238.97261047363281,240.6,300};std::vector<unsigned char>mask={1,1,1,0};
    auto grad=m.evaluate(pos,mask,1,true);check(grad.gradient[3]==0,"Fixed gradient mask");
    auto threaded=m.evaluate(pos,mask,2,true);check(grad.gradient==threaded.gradient,"Deterministic full-net gradient");
    QPSolverWrapper q(true,false,-.5,959.5,4,false);
    q.solverSettings.stabilityGuard=true;q.solverSettings.tolerence=1e-10;q.solverSettings.maxIters=10000;
    q.solverData.oriSolution=Eigen::Map<Eigen::VectorXd>(pos.data(),4);
    q.solverData.objectiveMatrixDiag={2,4,4,1};
    q.solverData.objectiveMatrixTripletList={{0,1,-1},{0,1,-1},{1,0,-2},{1,2,-1},{2,1,-1},{2,3,-.1},{3,2,-.1}};
    q.solverData.objectiveVector<<0,-250,-500,-270;
    amf_sll::FrozenAxis axis;axis.capture(q,pos);
    Frozen f;f.y=axis;f.x=pos;f.warmX=pos;f.warmY=pos;f.movable=mask;f.sll=grad.gradient;f.targets={0,1};f.coefficient=24;f.injectedCoefficient=24;f.tolerance=q.solverSettings.tolerence;f.maxIters=10000;f.guarded=true;f.limit=-10;
    Components components;components.start({0,1},pos,pos);components.cumulative("wire",q,q);
    auto originalB=q.solverData.objectiveVector;auto originalDiag=q.solverData.objectiveMatrixDiag;
    q.solverData.objectiveVector[0]+=3;components.cumulative("timing",q,q);
    check(std::abs(components.parts.back().y[0]-3)<1e-12,"Timing delta");
    q.solverData.objectiveMatrixDiag[0]+=2;q.solverData.objectiveVector[0]-=2*pos[0];components.cumulative("self_anchor",q,q);
    check(std::abs(components.parts.back().y[0])<1e-10,"Centered anchor has zero gradient");
    auto total=gradient(q,pos,{0,1});for(size_t i=0;i<2;++i){double v=0;for(auto&p:components.parts)v+=p.y[i];check(std::abs(v-total[i])<1e-12,"Component closure");}
    q.solverData.objectiveVector=originalB;q.solverData.objectiveMatrixDiag=originalDiag;
    auto entries=q.solverData.objectiveMatrixTripletList;
    Budget budget{0,10000000};writeModel(dir/"model.bin",m,budget);writeFrozen(dir/"original-y.bin",f,budget);
    auto loaded=readModel(dir/"model.bin");auto ff=readFrozen(dir/"original-y.bin");
    check(loaded.evaluate(pos,mask,2,true).gradient==grad.gradient,"All nets/pins/weights roundtrip");
    check(ff.y.linear==f.y.linear&&ff.y.diagonal==f.y.diagonal&&ff.y.position==pos&&ff.warmY==pos&&ff.movable==mask&&ff.targets==f.targets,"Frozen roundtrip");
    check(ff.y.entries.size()==entries.size(),"Duplicate triplets retained");
    for(size_t i=0;i<entries.size();++i)check(ff.y.entries[i].row()==entries[i].row()&&ff.y.entries[i].col()==entries[i].col()&&ff.y.entries[i].value()==entries[i].value(),"Triplet bytes retained");
    rejects([&]{writeFrozen(dir/"original-y.bin",f,budget);});
    Budget tiny{0,2};rejects([&]{writeModel(dir/"partial.bin",m,tiny);});rejects([&]{readModel(dir/"partial.bin");});
    check(tiny.used<=tiny.limit,"Byte budget enforced");
    for(size_t i=0;i<4;++i)q.solverData.objectiveVector[i]+=24*grad.gradient[i];
    auto input=q.solverData.objectiveVector;auto qp=&q;QPSolverWrapper::QPSolve(qp);auto unobserved=q.solverData.solution;
    q.modelObserver=[&](const Eigen::SparseMatrix<double>&a,const Eigen::VectorXd&b){writeEffective(dir/"effective-y.bin",a,b,budget);};
    QPSolverWrapper::QPSolve(qp);
    check((q.solverData.solution.array()==unobserved.array()).all(),"Observer changed solver result");
    check((q.solverData.objectiveVector.array()==input.array()).all()&&q.solverData.objectiveMatrixDiag==originalDiag,"Observer/guard mutated inputs");
    check(q.guardDiagnostics.repairedRows==1&&q.guardDiagnostics.converged&&!q.guardDiagnostics.rollback,"Expected original guard repair");
    auto effective=readEffective(dir/"effective-y.bin");check(effective.matrix.coeff(0,0)>2,"Actual guard matrix exported");
    for(size_t i=0;i<4;++i)check(std::abs(effective.objective[i]-input[i]+(effective.matrix.coeff(i,i)-originalDiag[i])*pos[i])<1e-9,"Guard anchor objective exported");
    Eigen::ConjugateGradient<Eigen::SparseMatrix<double>,Eigen::Lower|Eigen::Upper>cg;cg.setTolerance(1e-12);cg.setMaxIterations(10000);cg.compute(effective.matrix);
    Eigen::VectorXd b0=effective.objective;for(size_t i=0;i<4;++i)b0[i]-=24*grad.gradient[i];
    Eigen::VectorXd base=cg.solve(-b0);
    for(double tau:{.2,.1}){auto mm=m;mm.tau.assign(3,tau);auto g=mm.evaluate(pos,mask,2,true);Eigen::Map<Eigen::VectorXd>sv(g.gradient.data(),4);Eigen::VectorXd extra=cg.solve(-24*sv),candidate=cg.solve(-b0-24*sv);
        check((candidate-base-extra).norm()<1e-7,"A^-1 extra displacement identity");
        check((effective.matrix*extra+24*sv).norm()<1e-8,"Full coupled residual");
        for(size_t i=0;i<3;++i){auto p=pos,n=pos;double h=tau*1e-4;p[i]+=h;n[i]-=h;double fd=(mm.evaluate(p,mask,1,false).smooth-mm.evaluate(n,mask,1,false).smooth)/(2*h);check(std::abs(fd-g.gradient[i])<1e-7,"Small-tau derivative FD");}
    }
    Writer complete(dir/"COMPLETE.json",budget);complete.text("{\"schema\":1,\"status\":\"complete\",\"fixture\":true}\n");complete.finish();
    std::cout<<"PASS "<<count<<" frozen QP audit checks; synthetic only, no placement\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
