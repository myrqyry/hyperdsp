#pragma once
#include "framework/dsp/realtime_handoff.hpp"
#include "framework/obs/route_properties.hpp"
#include <mutex>

namespace hyperdsp {
// Routed settings and subscriptions are prepared together on the control
// path. The DSP engine itself remains audio-owned, so ordinary updates
// preserve filter, envelope, delay, modulation and ramp history.
template<class Settings> class RealtimeSettings {
public:
    struct Packet {
        Settings settings;
        ExternalInputs external;
        unsigned rate,channels;
        uint64_t generation;
    };
private:
    std::mutex control_;
    ExternalInputs accepted_;
    std::atomic<uint64_t> generation_{0};
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    RealtimeHandoff<Packet> handoff_;
public:
    void publish(Settings settings,obs_data_t* data,unsigned rate,unsigned channels){
        std::lock_guard lock(control_);
        auto next=std::make_unique<Packet>(Packet{settings,accepted_,rate,channels,generation_.load()+1});
        for(unsigned i=0;i<modulation_slots;++i)obs_routes::connect(next->external,data,i,settings.routes[i].source);
        accepted_=next->external;
        generation_.store(next->generation);
        handoff_.publish(std::move(next));
    }
    uint64_t latest_generation()const noexcept{return generation_.load();}
    const Packet* consume() noexcept{return handoff_.consume();}
};
}
