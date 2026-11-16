#pragma once
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <omp.h>
namespace phase_wait {
inline double wall(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline double cpu(clockid_t id){timespec t;clock_gettime(id,&t);return t.tv_sec+t.tv_nsec*1e-9;}
inline bool enabled(){static bool on=std::getenv("AMF_PHASE_WAIT_PROFILE")!=nullptr;return on;}
struct Row {long calls=0;double wall=0,cpu=0,compute=0,workerCPU=0,barrier=0,barrierCPU=0;};
struct Store {std::mutex lock;std::map<std::string,Row> rows;
 ~Store(){const char*p=std::getenv("AMF_PHASE_WAIT_PROFILE");if(!p)return;std::ofstream o(p);o<<"name\tcalls\twall_s\tprocess_or_thread_cpu_s\tcritical_compute_s\tworker_cpu_s\tbarrier_thread_wall_s\tbarrier_thread_cpu_s\n"<<std::setprecision(15);for(auto &kv:rows){auto&a=kv.second;o<<kv.first<<'\t'<<a.calls<<'\t'<<a.wall<<'\t'<<a.cpu<<'\t'<<a.compute<<'\t'<<a.workerCPU<<'\t'<<a.barrier<<'\t'<<a.barrierCPU<<'\n';}}
};
inline Store& store(){static Store s;return s;}
struct Scope {const char*name;double w=0,c=0;clockid_t id;Scope(const char*n,clockid_t i=CLOCK_PROCESS_CPUTIME_ID):name(n),id(i){if(enabled()){w=wall();c=cpu(id);}}~Scope(){if(!w)return;double dw=wall()-w,dc=cpu(id)-c;auto&s=store();std::lock_guard<std::mutex> l(s.lock);auto&r=s.rows[name];r.calls++;r.wall+=dw;r.cpu+=dc;}};
struct Worker {double start=0,cstart=0,compute=0,ccpu=0,bstart=0,bcstart=0,bwall=0,bcpu=0;void begin(){if(enabled()){start=wall();cstart=cpu(CLOCK_THREAD_CPUTIME_ID);}}void wait(){if(start){compute=wall()-start;ccpu=cpu(CLOCK_THREAD_CPUTIME_ID)-cstart;bstart=wall();bcstart=cpu(CLOCK_THREAD_CPUTIME_ID);}}void end(){if(start){bwall=wall()-bstart;bcpu=cpu(CLOCK_THREAD_CPUTIME_ID)-bcstart;}}};
struct Parallel {const char*name;std::vector<Worker> workers;double start=0;Parallel(const char*n):name(n),workers(omp_get_max_threads()){if(enabled())start=wall();}~Parallel(){if(!start)return;double dw=wall()-start;auto&s=store();std::lock_guard<std::mutex> l(s.lock);auto&r=s.rows[name];r.calls++;r.wall+=dw;double mx=0;for(auto&w:workers){mx=std::max(mx,w.compute);r.workerCPU+=w.ccpu;r.barrier+=w.bwall;r.barrierCPU+=w.bcpu;}r.compute+=mx;}};
}
