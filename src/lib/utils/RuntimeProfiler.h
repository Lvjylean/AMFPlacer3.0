#pragma once

// Opt-in function scopes; normal builds compile every instrumentation macro out.
#ifdef AMF_ENABLE_RUNTIME_PROFILING
#include <cstddef>
#include <cstdint>

namespace amf_profile {
struct Site {
    std::size_t id;
    Site(const char *category, const char *name, const char *file, int line);
};
class Scope {
    void *thread = nullptr;
    Scope *parent = nullptr;
    std::size_t site = 0;
    std::uint64_t wallStart = 0, cpuStart = 0, childWall = 0, childCpu = 0;
public:
    explicit Scope(const Site &site);
    ~Scope() { stop(); }
    void stop();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};
class Session {
public:
    explicit Session(const char *suffix = nullptr);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
};
}
#define AMF_PROFILE_JOIN_INNER(a, b) a##b
#define AMF_PROFILE_JOIN(a, b) AMF_PROFILE_JOIN_INNER(a, b)
#define AMF_PROFILE_NAMED(var, category, label) \
    static const amf_profile::Site AMF_PROFILE_JOIN(var, _site)(category, label, __FILE__, __LINE__); \
    amf_profile::Scope var(AMF_PROFILE_JOIN(var, _site))
#define AMF_PROFILE_SCOPE(category, label) AMF_PROFILE_NAMED(AMF_PROFILE_JOIN(amfScope_, __LINE__), category, label)
#define AMF_PROFILE_FUNCTION(category) AMF_PROFILE_SCOPE(category, __PRETTY_FUNCTION__)
#define AMF_PROFILE_STOP(var) var.stop()
#else
namespace amf_profile { struct Session { explicit Session(const char * = nullptr) {} }; }
#define AMF_PROFILE_NAMED(var, category, label)
#define AMF_PROFILE_SCOPE(category, label)
#define AMF_PROFILE_FUNCTION(category)
#define AMF_PROFILE_STOP(var)
#endif
