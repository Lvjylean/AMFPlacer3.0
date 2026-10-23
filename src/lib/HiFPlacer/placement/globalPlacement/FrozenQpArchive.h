#pragma once
#include "SllDiagnosticMath.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <cstring>

// Diagnostic-only archive. Numeric fields are explicit little-endian IEEE754
// primitives; never serialize compiler-dependent struct padding.
namespace amf_sll_audit {
inline std::string json(const std::string& s) {
    std::ostringstream o;o << '"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\')o << '\\' << c;
        else if(c<32)o << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
        else o << c;
    }
    return o.str()+'"';
}
inline void architecture() {
    uint16_t one=1;
    if(*reinterpret_cast<unsigned char*>(&one)!=1 || sizeof(double)!=8 || sizeof(float)!=4 ||
       !std::numeric_limits<double>::is_iec559 || !std::numeric_limits<float>::is_iec559)
        throw std::runtime_error("Frozen QP archive needs little-endian IEEE754");
}
struct Budget {uint64_t used=0,limit=0;};
class Writer {
    std::ofstream f;Budget& budget;
public:
    Writer(const std::filesystem::path& p,Budget& b):budget(b) {
        architecture();if(std::filesystem::exists(p))throw std::runtime_error("Refusing archive overwrite: "+p.string());
        f.open(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open archive: "+p.string());
    }
    void bytes(const void* p,size_t n) {
        if(n>budget.limit-budget.used)throw std::runtime_error("Frozen QP byte budget exhausted");
        f.write(static_cast<const char*>(p),n);if(!f)throw std::runtime_error("Frozen QP write failed");budget.used+=n;
    }
    template<class T>void scalar(T x){bytes(&x,sizeof(x));}
    template<class T>void array(const std::vector<T>& x){if(!x.empty())bytes(x.data(),x.size()*sizeof(T));}
    void text(const std::string& s){bytes(s.data(),s.size());}
    void finish(){f.flush();if(!f)throw std::runtime_error("Frozen QP flush failed");}
};
class Reader {
    std::ifstream f;uint64_t remaining;
public:
    Reader(const std::filesystem::path& p):remaining(std::filesystem::file_size(p)) {
        architecture();f.open(p,std::ios::binary);if(!f)throw std::runtime_error("Missing archive: "+p.string());
    }
    void bytes(void* p,size_t n){if(n>remaining)throw std::runtime_error("Truncated archive");f.read(static_cast<char*>(p),n);if(!f)throw std::runtime_error("Archive read failed");remaining-=n;}
    template<class T>T scalar(){T x;bytes(&x,sizeof(x));return x;}
    template<class T>std::vector<T> array(uint64_t n){if(n>remaining/sizeof(T))throw std::runtime_error("Invalid archive count");std::vector<T>x(n);if(n)bytes(x.data(),n*sizeof(T));return x;}
    void magic(const char* s){char v[8];bytes(v,8);if(std::memcmp(v,s,8))throw std::runtime_error("Wrong archive version");}
    void end(){if(remaining)throw std::runtime_error("Unexpected archive tail");}
};
inline void writeModel(const std::filesystem::path&p,const amf_sll::Model&m,Budget&b) {
    const bool masked=std::any_of(m.nets.begin(),m.nets.end(),[](const amf_sll::Net&n){return !n.gradientActive;});
    Writer w(p,b);w.bytes(masked?"MFSLL02\0":"MFSLL01\0",8);w.scalar<uint64_t>(m.units);w.scalar<uint64_t>(m.boundaries.size());
    w.scalar(m.gamma);w.scalar(m.minY);w.scalar(m.maxY);w.array(m.boundaries);w.array(m.tau);
    w.scalar<uint64_t>(m.pins.size());for(auto&v:m.pins){w.scalar<uint32_t>(v.unit);w.scalar(v.offsetY);}
    w.scalar<uint64_t>(m.nets.size());for(auto&v:m.nets){w.scalar<uint64_t>(v.begin);w.scalar<uint64_t>(v.end);w.scalar<uint32_t>(v.id);w.scalar<uint8_t>(v.optimize);w.scalar(v.weight);if(masked)w.scalar<uint8_t>(v.gradientActive);}w.finish();
}
inline amf_sll::Model readModel(const std::filesystem::path&p) {
    Reader r(p);char version[8];r.bytes(version,8);bool masked=!std::memcmp(version,"MFSLL02\0",8);
    if(!masked && std::memcmp(version,"MFSLL01\0",8))throw std::runtime_error("Wrong SLL model archive version");amf_sll::Model m;m.units=r.scalar<uint64_t>();auto nb=r.scalar<uint64_t>();
    m.gamma=r.scalar<double>();m.minY=r.scalar<double>();m.maxY=r.scalar<double>();m.boundaries=r.array<double>(nb);m.tau=r.array<double>(nb);
    auto np=r.scalar<uint64_t>();if(np>1000000000ULL)throw std::runtime_error("Excessive pin count");
    for(uint64_t i=0;i<np;++i){unsigned u=r.scalar<uint32_t>();double y=r.scalar<double>();m.pins.push_back({u,y});}
    auto nn=r.scalar<uint64_t>();if(nn>1000000000ULL)throw std::runtime_error("Excessive net count");
    for(uint64_t i=0;i<nn;++i){auto a=r.scalar<uint64_t>(),b=r.scalar<uint64_t>();auto id=r.scalar<uint32_t>();auto opt=r.scalar<uint8_t>();auto weight=r.scalar<double>();if(opt>1)throw std::runtime_error("Invalid optimize flag");auto active=masked?r.scalar<uint8_t>():1;if(active>1)throw std::runtime_error("Invalid gradient activity flag");m.nets.push_back({a,b,id,bool(opt),weight,bool(active)});}
    r.end();m.validate();return m;
}
struct Frozen {
    amf_sll::FrozenAxis y;
    std::vector<double> x,warmX,warmY,sll;
    std::vector<unsigned char> movable;
    std::vector<uint32_t> targets;
    double coefficient=0,injectedCoefficient=0,tolerance=0,limit=0;
    uint32_t maxIters=0;bool guarded=false,unconstrained=true,solutionForward=false;
};
inline void writeFrozen(const std::filesystem::path&p,const Frozen&f,Budget&b) {
    Writer w(p,b);w.bytes("MFQP001\0",8);auto n=f.y.position.size();w.scalar<uint64_t>(n);w.scalar<uint64_t>(f.y.entries.size());
    w.scalar(f.coefficient);w.scalar(f.injectedCoefficient);w.scalar(f.tolerance);w.scalar(f.limit);w.scalar(f.maxIters);
    w.scalar<uint8_t>(f.guarded);w.scalar<uint8_t>(f.unconstrained);w.scalar<uint8_t>(f.solutionForward);
    w.array(f.y.position);w.array(f.x);w.array(f.warmY);w.array(f.warmX);w.array(f.y.linear);w.array(f.y.diagonal);w.array(f.movable);w.array(f.sll);
    for(auto&e:f.y.entries){w.scalar<uint32_t>(e.row());w.scalar<uint32_t>(e.col());w.scalar(e.value());}
    w.scalar<uint64_t>(f.targets.size());w.array(f.targets);w.finish();
}
inline Frozen readFrozen(const std::filesystem::path&p) {
    Reader r(p);r.magic("MFQP001\0");Frozen f;auto n=r.scalar<uint64_t>(),ne=r.scalar<uint64_t>();
    f.coefficient=r.scalar<double>();f.injectedCoefficient=r.scalar<double>();f.tolerance=r.scalar<double>();f.limit=r.scalar<double>();f.maxIters=r.scalar<uint32_t>();
    f.guarded=r.scalar<uint8_t>();f.unconstrained=r.scalar<uint8_t>();f.solutionForward=r.scalar<uint8_t>();
    f.y.position=r.array<double>(n);f.x=r.array<double>(n);f.warmY=r.array<double>(n);f.warmX=r.array<double>(n);f.y.linear=r.array<double>(n);f.y.diagonal=r.array<float>(n);f.movable=r.array<unsigned char>(n);f.sll=r.array<double>(n);
    for(uint64_t i=0;i<ne;++i){auto a=r.scalar<uint32_t>(),b=r.scalar<uint32_t>();auto v=r.scalar<float>();if(a>=n||b>=n||!std::isfinite(v))throw std::runtime_error("Invalid triplet");f.y.entries.emplace_back(a,b,v);}
    auto nt=r.scalar<uint64_t>();f.targets=r.array<uint32_t>(nt);r.end();for(auto i:f.targets)if(i>=n)throw std::runtime_error("Invalid target");return f;
}
inline void writeEffective(const std::filesystem::path&p,const Eigen::SparseMatrix<double>&a,const Eigen::VectorXd&v,Budget&b) {
    Writer w(p,b);w.bytes("MFEFF01\0",8);w.scalar<uint64_t>(a.rows());w.scalar<uint64_t>(a.nonZeros());w.bytes(v.data(),v.size()*sizeof(double));
    for(int k=0;k<a.outerSize();++k)for(Eigen::SparseMatrix<double>::InnerIterator it(a,k);it;++it){w.scalar<uint32_t>(it.row());w.scalar<uint32_t>(it.col());w.scalar(it.value());}w.finish();
}
struct Effective {Eigen::SparseMatrix<double> matrix;Eigen::VectorXd objective;};
inline Effective readEffective(const std::filesystem::path&p) {
    Reader r(p);r.magic("MFEFF01\0");auto n=r.scalar<uint64_t>(),ne=r.scalar<uint64_t>();auto b=r.array<double>(n);Effective e;e.objective=Eigen::Map<Eigen::VectorXd>(b.data(),n);
    std::vector<Eigen::Triplet<double>> entries;for(uint64_t i=0;i<ne;++i){auto a=r.scalar<uint32_t>(),c=r.scalar<uint32_t>();auto v=r.scalar<double>();if(a>=n||c>=n||!std::isfinite(v))throw std::runtime_error("Invalid effective matrix");entries.emplace_back(a,c,v);}
    r.end();e.matrix.resize(n,n);e.matrix.setFromTriplets(entries.begin(),entries.end());return e;
}
inline std::vector<double> gradient(const QPSolverWrapper&q,const std::vector<double>&p,const std::vector<uint32_t>&ids) {
    std::vector<double> g(ids.size());std::map<int,size_t> index;
    for(size_t j=0;j<ids.size();++j){auto i=ids[j];index[i]=j;g[j]=q.solverData.objectiveVector[i]+double(q.solverData.objectiveMatrixDiag[i])*p[i];}
    for(auto&e:q.solverData.objectiveMatrixTripletList){auto a=index.find(e.row()),b=index.find(e.col());if(a!=index.end())g[a->second]+=.5*e.value()*p[e.col()];if(b!=index.end())g[b->second]+=.5*e.value()*p[e.row()];}return g;
}
struct Components {
    std::vector<uint32_t> ids;std::vector<double> px,py,lastX,lastY;
    struct Part {std::string name;std::vector<double>x,y;};std::vector<Part> parts;
    void start(std::vector<uint32_t> i,std::vector<double>x,std::vector<double>y){ids=std::move(i);px=std::move(x);py=std::move(y);parts.clear();lastX.assign(ids.size(),0);lastY=lastX;}
    void cumulative(const std::string&name,const QPSolverWrapper&x,const QPSolverWrapper&y){
        auto gx=gradient(x,px,ids),gy=gradient(y,py,ids);Part p{name,gx,gy};
        for(size_t i=0;i<ids.size();++i){p.x[i]-=lastX[i];p.y[i]-=lastY[i];}parts.push_back(p);lastX=gx;lastY=gy;
    }
};
inline std::set<size_t> parseQps(const std::string&s) {
    // Legacy parseJSONFile splits on every comma, including quoted values.
    // Semicolons are the file-config spelling; commas remain accepted for API callers.
    if(s.find(';')!=std::string::npos&&s.find(',')!=std::string::npos)throw std::invalid_argument("Mixed QP separators");
    const char separator=s.find(';')!=std::string::npos?';':',';
    std::set<size_t> out;std::istringstream stream(s);std::string t;
    while(std::getline(stream,t,separator)){if(t.empty()||t.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("SllQpAuditQps requires semicolon-separated positive QP ids");auto n=std::stoull(t);if(!n||!out.insert(n).second)throw std::invalid_argument("Invalid/duplicate QP id");}
    if(out.empty()||s.back()==separator)throw std::invalid_argument("Empty QP selection");return out;
}
}
