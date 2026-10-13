#include "../lib/utils/RuntimeProfiler.h"
#include <chrono>
#include <stdexcept>
#include <thread>

void nested() {
    AMF_PROFILE_FUNCTION("test_nested");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
void worker() {
    AMF_PROFILE_FUNCTION("test_worker");
    nested();
    try {
        AMF_PROFILE_SCOPE("test_exception", "unwind");
        throw std::runtime_error("scope unwind");
    } catch (const std::exception &) {}
}
int main() {
    amf_profile::Session session;
    AMF_PROFILE_FUNCTION("test_main");
    nested();
    std::thread a(worker), b(worker);
    a.join(); b.join();
    AMF_PROFILE_NAMED(manual, "test_manual", "explicit stop");
    nested();
    AMF_PROFILE_STOP(manual);
    for (int i = 0; i < 20000; ++i) {
        AMF_PROFILE_SCOPE("test_overhead", "empty scope");
    }
}
