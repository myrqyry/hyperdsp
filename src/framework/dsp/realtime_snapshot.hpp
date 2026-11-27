#pragma once
#include <array>
#include <atomic>
#include <limits>
#include <type_traits>

namespace hyperdsp {
// Single serialized publisher, concurrent readers, three preallocated slots.
// Readers pin a slot before touching its non-atomic payload; the writer can
// only acquire an unpublished, unpinned slot. All index/ownership operations
// are sequentially consistent, so a stale index cannot grant unsafe access.
// No waiting or unbounded retry: contention may drop a diagnostic frame.
// Stop all callers before destruction. Visitors must not retain references.
template<class T> class RealtimeSnapshot {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_nothrow_copy_assignable_v<T>);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    static constexpr unsigned count=3;
    static constexpr unsigned writing=std::numeric_limits<unsigned>::max();
    struct Slot {
        std::atomic<unsigned> owners{0};
        T value{};
    };
    std::array<Slot,count> slots_{};
    std::atomic<unsigned> latest_{count};
    struct Pin {
        Slot& slot;
        ~Pin(){slot.owners.fetch_sub(1);}
    };
public:
    bool publish(const T& value) noexcept {
        const auto current=latest_.load();
        for(unsigned i=0;i<count;++i){
            if(i==current)continue;
            auto& slot=slots_[i];
            unsigned free=0;
            if(!slot.owners.compare_exchange_strong(free,writing))continue;
            slot.value=value;
            latest_.store(i);
            slot.owners.store(0);
            return true;
        }
        return false;
    }
    template<class Visitor> bool visit(Visitor visitor){
        for(unsigned attempt=0;attempt<4;++attempt){
            const auto i=latest_.load();
            if(i==count)return false;
            auto& slot=slots_[i];
            auto owners=slot.owners.load();
            if(owners>=writing-1||!slot.owners.compare_exchange_strong(owners,owners+1))continue;
            Pin pin{slot};
            if(latest_.load()!=i)continue;
            return visitor(static_cast<const T&>(slot.value));
        }
        return false;
    }
};
}
