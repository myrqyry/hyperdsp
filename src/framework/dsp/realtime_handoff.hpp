#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace hyperdsp {
// Latest-wins ownership exchange. One serialized control writer and one
// serialized audio consumer. The low bit distinguishes a prepared candidate
// from a retired object. The consumer never allocates, destroys or waits.
// A consumed object stays alive through the end of its current packet;
// only the *next* consume may retire it. Stop both users before destruction.
template<class T> class RealtimeHandoff {
    static_assert(alignof(T)>=2);
    static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
    static constexpr std::uintptr_t ready=1;
    static std::uintptr_t bits(T* p){return reinterpret_cast<std::uintptr_t>(p);}
    static T* pointer(std::uintptr_t value){return reinterpret_cast<T*>(value&~ready);}
    std::atomic<std::uintptr_t> slot_{0};
    T* active_=nullptr; // consumer-owned; never read by control
public:
    RealtimeHandoff()=default;
    RealtimeHandoff(const RealtimeHandoff&)=delete;
    RealtimeHandoff& operator=(const RealtimeHandoff&)=delete;
    ~RealtimeHandoff(){delete active_;delete pointer(slot_.load(std::memory_order_relaxed));}
    void publish(std::unique_ptr<T> candidate){
        if(!candidate)return;
        // The previous slot is either an unconsumed candidate or an object
        // retired at a packet boundary. Both belong to this writer now.
        delete pointer(slot_.exchange(bits(candidate.release())|ready,std::memory_order_acq_rel));
    }
    T* consume() noexcept {
        // Only the consumer clears ready; a concurrent publisher can replace
        // a ready candidate but cannot turn it into a retired object.
        if(slot_.load(std::memory_order_acquire)&ready)
            active_=pointer(slot_.exchange(bits(active_),std::memory_order_acq_rel));
        return active_;
    }
};
}
