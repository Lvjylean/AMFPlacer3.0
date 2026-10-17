#ifndef AMF_SLL_CLIP_TRACE_H
#define AMF_SLL_CLIP_TRACE_H
#include <atomic>
namespace sll_clip_trace {
inline bool enabled=false; // Set before spawning placement workers.
inline std::atomic<unsigned long long> x{0},y{0};
inline void note(float oldX,float oldY,float newX,float newY) {
    if(!enabled)return;
    if(oldX!=newX)x.fetch_add(1,std::memory_order_relaxed);
    if(oldY!=newY)y.fetch_add(1,std::memory_order_relaxed);
}
}
#endif
