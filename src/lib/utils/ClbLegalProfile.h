#pragma once
// Observational CLB diagnostics. No placement, candidate order or solver state is modified.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <omp.h>
namespace clb_profile {
using Clock=std::chrono::steady_clock;
inline double wall(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
inline double cpu(clockid_t id){timespec t{};clock_gettime(id,&t);return t.tv_sec+t.tv_nsec*1e-9;}
inline bool enabled(){static const bool b=[](){const char*p=std::getenv("AMF_CLB_LEGAL_PROFILE");return p&&*p;}();return b;}
inline bool active=false;
inline unsigned long long call=0,phase=0,sequence=0;
inline std::ofstream &output(){static std::ofstream f(std::getenv("AMF_CLB_LEGAL_PROFILE"),std::ios::out);return f;}
inline void emit(const std::string &s){if(active)output()<<std::setprecision(12)<<"CLB_PROFILE t="<<wall()<<" call="<<call<<" phase="<<phase<<" "<<s<<"\n";}
struct Sched {
 unsigned long long run=0,wait=0,slices=0; bool valid=false;
 explicit Sched(bool read=true){if(!read)return;std::ifstream f("/proc/thread-self/schedstat");valid=bool(f>>run>>wait>>slices);}
};
struct Work {
 unsigned long long items=0,raw=0,legal=0,kept=0,rejected=0,dpStates=0;
 unsigned long long lookups=0,misses=0,nets=0,pins=0;
 double costWall=0,filterWall=0,sortWall=0;
};
inline thread_local Work *current=nullptr;
inline void lookup(){if(current)++current->lookups;}
inline void net(std::size_t pins){if(current){++current->nets;current->pins+=pins;}}
struct Cost {
 Work *p=current; double start=0;
 Cost(){if(p){++p->misses;start=wall();}}
 ~Cost(){if(p)p->costWall+=wall()-start;}
};
struct Call {
 bool prior=active;double start=0,pcpu=0;
 explicit Call(bool exact){active=enabled();if(active){++call;phase=sequence=0;start=wall();pcpu=cpu(CLOCK_PROCESS_CPUTIME_ID);emit("kind=call_begin exact="+std::to_string(exact));}}
 ~Call(){if(active){std::ostringstream s;s<<std::setprecision(12)<<"kind=call_end wall_s="<<wall()-start<<" process_cpu_s="<<cpu(CLOCK_PROCESS_CPUTIME_ID)-pcpu;emit(s.str());output().flush();}active=prior;}
};
struct Stage {
 bool on=active;const char *name;double start=0,pcpu=0,tcpu=0;Sched before;
 unsigned long long previous=0;
 Work work;Work *prior=nullptr;
 explicit Stage(const char*n):name(n),before(on){if(on){previous=phase;phase=++sequence;start=wall();pcpu=cpu(CLOCK_PROCESS_CPUTIME_ID);tcpu=cpu(CLOCK_THREAD_CPUTIME_ID);prior=current;current=&work;emit(std::string("kind=stage_begin name=")+name);}}
 ~Stage(){if(on){const double end=wall(),p=cpu(CLOCK_PROCESS_CPUTIME_ID),t=cpu(CLOCK_THREAD_CPUTIME_ID);Sched after;
 std::ostringstream s;s<<std::setprecision(12)<<"kind=stage_end name="<<name<<" wall_s="<<end-start<<" process_cpu_s="<<p-pcpu<<" main_cpu_s="<<t-tcpu
 <<" main_sched_valid="<<(before.valid&&after.valid)<<" main_runqueue_wait_s="<<(after.wait-before.wait)*1e-9
 <<" lookups="<<work.lookups<<" misses="<<work.misses<<" nets="<<work.nets<<" pins="<<work.pins<<" cost_worker_wall_s="<<work.costWall;emit(s.str());current=prior;phase=previous;}}
};
struct Worker:Work {
 bool used=false,valid=false;long tid=0;int cpuBegin=-1,cpuEnd=-1;
 double computeWall=0,computeCPU=0,barrierWall=0,barrierCPU=0,queueWait=0;
};
struct Team {
 bool on=active;const char*name;std::vector<Worker> rows;
 explicit Team(const char*n,int size=omp_get_max_threads()):name(n){if(on)rows.resize(size);}
 ~Team(){if(on)for(std::size_t i=0;i<rows.size();++i){auto&r=rows[i];if(!r.used)continue;std::ostringstream s;s<<std::setprecision(12)
 <<"kind=worker name="<<name<<" worker="<<i<<" tid="<<r.tid<<" cpu_begin="<<r.cpuBegin<<" cpu_end="<<r.cpuEnd
 <<" compute_wall_s="<<r.computeWall<<" compute_cpu_s="<<r.computeCPU<<" barrier_wall_s="<<r.barrierWall<<" barrier_cpu_s="<<r.barrierCPU
 <<" sched_valid="<<r.valid<<" runqueue_wait_s="<<r.queueWait<<" items="<<r.items<<" raw="<<r.raw<<" legal="<<r.legal<<" kept="<<r.kept
 <<" rejected="<<r.rejected<<" dp_states="<<r.dpStates<<" lookups="<<r.lookups<<" misses="<<r.misses<<" nets="<<r.nets<<" pins="<<r.pins
 <<" cost_wall_s="<<r.costWall<<" filter_wall_s="<<r.filterWall<<" sort_with_cost_wall_s="<<r.sortWall;emit(s.str());}}
};
struct WorkerScope {
 Worker *r=nullptr;Work *prior=nullptr;double start=0,cstart=0,bstart=0,bcstart=0;Sched before;
 explicit WorkerScope(Team&t):before(t.on){if(t.on){r=&t.rows.at(omp_get_thread_num());r->used=true;r->tid=syscall(SYS_gettid);r->cpuBegin=sched_getcpu();
 prior=current;current=r;start=wall();cstart=cpu(CLOCK_THREAD_CPUTIME_ID);}}
 void beforeBarrier(){if(r){bstart=wall();bcstart=cpu(CLOCK_THREAD_CPUTIME_ID);r->computeWall=bstart-start;r->computeCPU=bcstart-cstart;}}
 void afterBarrier(){if(r){const double end=wall(),ce=cpu(CLOCK_THREAD_CPUTIME_ID);Sched after;
 r->barrierWall=end-bstart;r->barrierCPU=ce-bcstart;r->queueWait=(after.wait-before.wait)*1e-9;r->valid=after.valid&&before.valid;r->cpuEnd=sched_getcpu();}}
 ~WorkerScope(){if(r)current=prior;}
};
} // namespace clb_profile

