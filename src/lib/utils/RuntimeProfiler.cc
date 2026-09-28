#include "RuntimeProfiler.h"
#ifdef AMF_ENABLE_RUNTIME_PROFILING
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace amf_profile {
namespace {
std::uint64_t clockNs(clockid_t clock) {
    timespec t{};
    clock_gettime(clock, &t);
    return std::uint64_t(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}
struct Stats {
    std::uint64_t calls = 0, wall = 0, selfWall = 0, cpu = 0, selfCpu = 0, maximum = 0;
};
struct Thread {
    std::size_t id;
    long tid;
    Scope *top = nullptr;
    std::map<std::size_t, Stats> stats;
};
struct Metadata { std::string category, name, file; int line; };
struct Registry {
    bool enabled = false;
    std::string output;
    std::mutex mutex;
    std::vector<Metadata> sites;
    std::vector<std::unique_ptr<Thread>> threads;
    std::uint64_t started = 0;
};
Registry &registry() {
    // Thread records outlive exited worker threads and static Site objects.
    static Registry *value = new Registry;
    return *value;
}
thread_local Thread *localThread = nullptr;
Thread *getThread() {
    if (!localThread) {
        auto &r = registry();
        std::lock_guard<std::mutex> lock(r.mutex);
        auto t = std::make_unique<Thread>();
        t->id = r.threads.size(); t->tid = syscall(SYS_gettid);
        localThread = t.get(); r.threads.push_back(std::move(t));
    }
    return localThread;
}
std::string clean(std::string s) {
    for (char &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}
double cpuSeconds(const timeval &t) { return t.tv_sec + t.tv_usec / 1e6; }
}

Site::Site(const char *category, const char *name, const char *file, int line) {
    auto &r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    id = r.sites.size(); r.sites.push_back({category, name, file, line});
}
Scope::Scope(const Site &s) {
    if (!registry().enabled) return;
    auto t = getThread();
    thread = t; parent = t->top; t->top = this; site = s.id;
    cpuStart = clockNs(CLOCK_THREAD_CPUTIME_ID);
    wallStart = clockNs(CLOCK_MONOTONIC);
}
void Scope::stop() {
    if (!thread) return;
    const auto wall = clockNs(CLOCK_MONOTONIC) - wallStart;
    const auto cpu = clockNs(CLOCK_THREAD_CPUTIME_ID) - cpuStart;
    auto t = static_cast<Thread *>(thread);
    if (t->top != this) { std::cerr << "AMF_PROFILE_SCOPE_ORDER_ERROR\n"; std::abort(); }
    t->top = parent;
    auto &s = t->stats[site];
    ++s.calls; s.wall += wall; s.cpu += cpu;
    s.selfWall += wall >= childWall ? wall - childWall : 0;
    s.selfCpu += cpu >= childCpu ? cpu - childCpu : 0;
    s.maximum = std::max(s.maximum, wall);
    if (parent) { parent->childWall += wall; parent->childCpu += cpu; }
    thread = nullptr;
}
Session::Session(const char *suffix) {
    const char *output = std::getenv("AMF_PROFILE_OUTPUT");
    if (!output || !*output) return;
    auto &r = registry();
    r.output = output;
    if (suffix) r.output += std::string(".") + suffix + "-" + std::to_string(getpid()) + ".tsv";
    r.started = clockNs(CLOCK_MONOTONIC); r.enabled = true;
    getThread(); // Main thread is always index 0, even if a worker registers next.
}
Session::~Session() {
    auto &r = registry();
    if (!r.enabled) return;
    const double wall = (clockNs(CLOCK_MONOTONIC) - r.started) / 1e9;
    // Caller must join all instrumented workers before destroying the session.
    std::ofstream out(r.output);
    out << "thread_index\tos_tid\tsite_id\tcategory\tfunction\tfile\tline\tcalls\tinclusive_wall_s\tself_wall_s\tinclusive_thread_cpu_s\tself_thread_cpu_s\tmax_wall_s\n";
    out << std::fixed << std::setprecision(9);
    bool complete = true;
    for (const auto &t : r.threads) {
        if (t->top) complete = false;
        for (const auto &entry : t->stats) {
            const auto &m = r.sites[entry.first]; const auto &s = entry.second;
            out << t->id << '\t' << t->tid << '\t' << entry.first << '\t' << clean(m.category) << '\t'
                << clean(m.name) << '\t' << clean(m.file) << '\t' << m.line << '\t' << s.calls << '\t'
                << s.wall / 1e9 << '\t' << s.selfWall / 1e9 << '\t' << s.cpu / 1e9 << '\t'
                << s.selfCpu / 1e9 << '\t' << s.maximum / 1e9 << '\n';
        }
    }
    out.close(); complete = complete && bool(out);
    rusage self{}, children{};
    getrusage(RUSAGE_SELF, &self); getrusage(RUSAGE_CHILDREN, &children);
    std::ofstream meta(r.output + ".meta.json");
    meta << std::fixed << std::setprecision(9)
         << "{\"schema\":\"amf-runtime-profile-v1\",\"pid\":" << getpid()
         << ",\"complete\":" << (complete ? "true" : "false")
         << ",\"session_wall_s\":" << wall << ",\"registered_threads\":" << r.threads.size()
         << ",\"process_user_s\":" << cpuSeconds(self.ru_utime)
         << ",\"process_system_s\":" << cpuSeconds(self.ru_stime)
         << ",\"children_user_s\":" << cpuSeconds(children.ru_utime)
         << ",\"children_system_s\":" << cpuSeconds(children.ru_stime)
         << ",\"peak_rss_kib\":" << self.ru_maxrss << "}\n";
    if (!complete || !meta) std::cerr << "AMF_PROFILE_WRITE_ERROR\n";
    r.enabled = false;
}
}
#endif
