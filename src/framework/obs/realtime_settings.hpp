#pragma once
#include "framework/dsp/realtime_handoff.hpp"
#include "framework/obs/route_properties.hpp"
#include <mutex>
#include <type_traits>

namespace hyperdsp {
// Settings and optional routed subscriptions are prepared together on the control
// path. The DSP engine itself remains audio-owned, so ordinary updates
// preserve filter, envelope, delay, modulation and ramp history.
template<class Settings,class Engine=void> class RealtimeSettings {
public:
    struct Packet {
        Settings settings;
        ExternalInputs external;
        unsigned rate,channels;
        uint64_t generation;
        // Only control copies or destroys ownership. Audio borrows the
        // engine through its active packet; no reference-count operations.
        std::shared_ptr<Engine> engine;
    };
private:
    std::mutex control_;
    ExternalInputs accepted_;
    std::shared_ptr<Engine> accepted_engine_;
    unsigned accepted_rate_=0,accepted_channels_=0;
    std::atomic<uint64_t> generation_{0};
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    RealtimeHandoff<Packet> handoff_;
public:
    void publish(Settings settings,obs_data_t* data,unsigned rate,unsigned channels) requires std::is_void_v<Engine> {
        publish(settings,data,rate,channels,[](auto&,auto,auto,const auto&){});
    }
    template<class Prepare>
    void publish(Settings settings,obs_data_t* data,unsigned rate,unsigned channels,Prepare prepare){
        publish_with_routes(settings,data,rate,channels,prepare,[](ExternalInputs& external,obs_data_t* data,const Settings& settings){
            if constexpr(requires {settings.routes;})
                for(unsigned i=0;i<modulation_slots;++i)obs_routes::connect(external,data,i,settings.routes[i].source);
        });
    }
    template<class Connect>
    void publish_with_routes(Settings settings,obs_data_t* data,unsigned rate,unsigned channels,Connect connect) requires std::is_void_v<Engine>{
        publish_with_routes(settings,data,rate,channels,[](auto&,auto,auto,const auto&){},connect);
    }
private:
    template<class Prepare,class Connect>
    void publish_with_routes(Settings settings,obs_data_t* data,unsigned rate,unsigned channels,Prepare prepare,Connect connect){
        std::lock_guard lock(control_);
        auto next=std::make_unique<Packet>(Packet{settings,accepted_,rate,channels,generation_.load()+1,accepted_engine_});
        connect(next->external,data,settings);
        if constexpr(!std::is_void_v<Engine>){
            if(!next->engine||accepted_rate_!=rate||accepted_channels_!=channels){
                auto engine=std::make_shared<Engine>();
                prepare(*engine,rate,channels,settings);
                next->engine=std::move(engine);
            }
        }
        accepted_=next->external;
        accepted_engine_=next->engine;accepted_rate_=rate;accepted_channels_=channels;
        generation_.store(next->generation);
        handoff_.publish(std::move(next));
    }
public:
    uint64_t latest_generation()const noexcept{return generation_.load();}
    const Packet* consume() noexcept{return handoff_.consume();}
};
}
