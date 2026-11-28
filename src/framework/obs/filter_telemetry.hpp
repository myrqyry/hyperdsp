#pragma once
#include "framework/dsp/node_telemetry.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <atomic>
#include "framework/dsp/realtime_snapshot.hpp"
#ifdef HYPERDSP_WITH_BUILDER
#include "ui/filter_analysis.hpp"
#endif
namespace hyperdsp {
// Demand-driven, bounded snapshot publication without mutex acquisition.
// Invalidation is atomic and readers never access mutable engine state.
class FilterTelemetry {
    obs_source_t* source_=nullptr;
    std::atomic<uint64_t> request_{0},epoch_{1};
    RealtimeSnapshot<FilterTelemetryFrame> published_;
    static void read_proc(void* ptr,calldata_t* data){
        auto* self=static_cast<FilterTelemetry*>(ptr);
        calldata_set_bool(data,"available",false);
        void* raw=nullptr;
        if(!self||!calldata_get_ptr(data,"frame",&raw)||!raw)return;
        auto& frame=*static_cast<FilterTelemetryFrame*>(raw);
        if(frame.version!=1||frame.size!=sizeof(frame))return;
        if(!obs_source_enabled(self->source_)){self->invalidate();return;}
        auto now=os_gettime_ns();self->request_.store(now);
        bool available=self->published_.visit([&](const FilterTelemetryFrame& v){
            if(!v.stamp||v.epoch!=self->epoch_.load()||now<v.stamp||now-v.stamp>500000000ULL)return false;
            frame=v;return v.epoch==self->epoch_.load();
        });
        calldata_set_bool(data,"available",available);
    }
#ifdef HYPERDSP_WITH_BUILDER
    static bool open(obs_properties_t*,obs_property_t*,void* ptr){
        auto* self=static_cast<FilterTelemetry*>(ptr);
        if(self)show_filter_analysis(self->source_);
        return false;
    }
#endif
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void attach(obs_source_t* source){
        source_=source;
        proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_filter_telemetry_v1(in ptr frame, out bool available)",read_proc,this);
    }
    void invalidate(){epoch_.fetch_add(1);}
    uint64_t epoch()const{return epoch_.load();}
    template<class ReadNode>
    void publish(unsigned rate,ReadNode read){publish(rate,read,epoch());}
    template<class ReadNode>
    void publish(unsigned rate,ReadNode read,uint64_t epoch){
        auto request=request_.load();if(!request)return;
        auto now=os_gettime_ns();
        if(now<request||now-request>1000000000ULL)return;
        if(epoch!=epoch_.load())return;
        FilterTelemetryFrame frame;
        frame.node=read();frame.rate=rate;frame.stamp=now;frame.epoch=epoch;
        published_.publish(frame);
    }
    void add_button(obs_properties_t* p){
#ifdef HYPERDSP_WITH_BUILDER
        obs_properties_add_button2(p,"open_analysis","Open live analysis",open,this);
#else
        (void)p;
#endif
    }
};
}
