#pragma once
// Observation only: no graph or placement data is modified here.
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <vector>
#include <omp.h>
namespace incremental_profile {
inline double wall(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline double threadCPU(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;}
inline bool enabled(){static bool e=std::getenv("AMF_INCREMENTAL_STA_PROFILE")!=nullptr;return e;}
struct Layer {int direction=0,index=0,nodes=0,workers=0;unsigned long long edges=0;double wall=0,criticalCompute=0,workerComputeCPU=0,waitWallSum=0,waitCPUSum=0,entryLagMax=0,teamTail=0;};
struct Record {unsigned long long id=0,pus=0,cells=0,pins=0,edges=0,nodes=0,forwardEdges=0,backwardEdges=0;bool noBack=false;double begin=0,totalWall=0,threadCPU=0;std::array<double,9> stageWall{},stageCPU{};size_t layerBegin=0,layerEnd=0;};
struct Store {std::mutex mutex;std::vector<Record> calls;std::vector<Layer> layers;double commitWall=0;
 Store(){calls.reserve(20000);layers.reserve(300000);}
 ~Store(){const char*p=std::getenv("AMF_INCREMENTAL_STA_PROFILE");if(!p)return;auto start=wall();std::ofstream c(std::string(p)+".calls.tsv"),l(std::string(p)+".layers.tsv");
 c<<std::setprecision(15)<<"call\tbegin_monotonic_s\tpus\tcells\tpins_unique\tedges_unique\tforward_nodes\tforward_search_edges\tbackward_search_edges\tno_back\twall_s\tcaller_thread_cpu_s";
 const char* names[]={"backup","pin_update","edge_delay","forward_search","forward_propagation","backward_search","backward_propagation","summary","reserved"};
 for(auto n:names)c<<'\t'<<n<<"_wall_s\t"<<n<<"_caller_cpu_s";c<<"\tlayers\n";
 l<<std::setprecision(15)<<"call\tdirection\tlayer\tnodes\tedges\tworkers\twall_s\tcritical_compute_s\tworker_compute_cpu_s\tbarrier_wall_sum_s\tbarrier_cpu_sum_s\tentry_lag_max_s\tteam_tail_s\n";
 for(auto&r:calls){c<<r.id<<'\t'<<r.begin<<'\t'<<r.pus<<'\t'<<r.cells<<'\t'<<r.pins<<'\t'<<r.edges<<'\t'<<r.nodes<<'\t'<<r.forwardEdges<<'\t'<<r.backwardEdges<<'\t'<<r.noBack<<'\t'<<r.totalWall<<'\t'<<r.threadCPU;for(size_t i=0;i<9;++i)c<<'\t'<<r.stageWall[i]<<'\t'<<r.stageCPU[i];c<<'\t'<<r.layerEnd-r.layerBegin<<'\n';
 for(size_t i=r.layerBegin;i<r.layerEnd;++i){auto&a=layers[i];l<<r.id<<'\t'<<a.direction<<'\t'<<a.index<<'\t'<<a.nodes<<'\t'<<a.edges<<'\t'<<a.workers<<'\t'<<a.wall<<'\t'<<a.criticalCompute<<'\t'<<a.workerComputeCPU<<'\t'<<a.waitWallSum<<'\t'<<a.waitCPUSum<<'\t'<<a.entryLagMax<<'\t'<<a.teamTail<<'\n';}}
 c.close();l.close();std::ofstream m(std::string(p)+".meta.tsv");m<<std::setprecision(15)<<"calls\tlayers\tobserver_commit_wall_s\treport_write_wall_s\n"<<calls.size()<<'\t'<<layers.size()<<'\t'<<commitWall<<'\t'<<wall()-start<<'\n';}
};
inline Store& store(){static Store s;return s;}
struct Call;inline Call*& active(){static thread_local Call*c=nullptr;return c;}
struct Call {bool on;Record row;Call*previous=nullptr;double w=0,c=0,c0=0;std::vector<Layer> layers;
 Call(size_t pus,bool noBack):on(enabled()){if(!on)return;row.pus=pus;row.noBack=noBack;layers.reserve(32);previous=active();active()=this;w=row.begin=wall();c=c0=threadCPU();}
 void mark(int i){if(!on)return;double nw=wall(),nc=threadCPU();row.stageWall[i]+=nw-w;row.stageCPU[i]+=nc-c;w=nw;c=nc;}
 ~Call(){if(!on)return;mark(7);row.totalWall=wall()-row.begin;row.threadCPU=threadCPU()-c0;active()=previous;double start=wall();auto&s=store();std::lock_guard<std::mutex>lock(s.mutex);row.id=s.calls.size();row.layerBegin=s.layers.size();s.layers.insert(s.layers.end(),layers.begin(),layers.end());row.layerEnd=s.layers.size();s.calls.push_back(row);s.commitWall+=wall()-start;}
};
struct alignas(64) Worker {double start=0,cpuStart=0,compute=0,computeCPU=0,waitStart=0,waitCPUStart=0,waitWall=0,waitCPU=0,finish=0;unsigned long long edges=0;bool on=false;
 void begin(bool enabled){on=enabled;if(on){start=wall();cpuStart=threadCPU();}}
 void wait(){if(on){waitStart=wall();waitCPUStart=threadCPU();compute=waitStart-start;computeCPU=waitCPUStart-cpuStart;}}
 void end(){if(on){finish=wall();waitWall=finish-waitStart;waitCPU=threadCPU()-waitCPUStart;}}
};
inline std::vector<Worker>& workerBuffer(){static thread_local std::vector<Worker> v;v.resize(omp_get_max_threads());for(auto&w:v)w=Worker{};return v;}
struct Parallel {Call*call;Layer row;double start=0;std::vector<Worker>& workers;
 Parallel(int direction,int index,int nodes):call(active()),workers(workerBuffer()){row.direction=direction;row.index=index;row.nodes=nodes;if(call)start=wall();}
 ~Parallel(){if(!call)return;row.wall=wall()-start;double last=0;for(auto&w:workers){if(!w.on)continue;++row.workers;row.edges+=w.edges;row.criticalCompute=std::max(row.criticalCompute,w.compute);row.workerComputeCPU+=w.computeCPU;row.waitWallSum+=w.waitWall;row.waitCPUSum+=w.waitCPU;row.entryLagMax=std::max(row.entryLagMax,w.start-start);last=std::max(last,w.finish);}row.teamTail=last?std::max(0.,start+row.wall-last):0;call->layers.push_back(row);}
};
}
