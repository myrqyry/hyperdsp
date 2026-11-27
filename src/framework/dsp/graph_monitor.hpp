#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "realtime_snapshot.hpp"
#include "node_telemetry.hpp"
namespace hyperdsp {
inline constexpr unsigned monitor_samples=1024;
struct GraphMonitorControl {
    std::array<char,33> id{};
    std::array<char,81> name{};
    float base=0,effective=0;
};
// In-process, versioned, caller-owned snapshot. No pointers to live DSP state.
struct GraphMonitorFrame {
    uint32_t version=1,size=sizeof(GraphMonitorFrame);
    uint64_t sequence=0,stamp=0,epoch=0;
    unsigned rate=0,control_count=0;
    std::array<float,monitor_samples> input{},output{};
    std::array<GraphMonitorControl,8> controls{};
};
// V1 is unchanged; V2 adds paired stereo samples in the same publication.
struct GraphMonitorFrameV2 {
    uint32_t version=2,size=sizeof(GraphMonitorFrameV2);
    GraphMonitorFrame signal{};
    bool stereo=false;
    std::array<float,monitor_samples> input_right{},output_right{};
};
struct GraphMonitorFrameV3 {
    uint32_t version=3,size=sizeof(GraphMonitorFrameV3);
    GraphMonitorFrameV2 audio{};
    unsigned node_count=0;
    NodeTelemetryArray nodes{};
};
class GraphMonitor {
    static constexpr uint64_t lease_ns=1000000000ULL, fresh_ns=500000000ULL;
    std::atomic<uint64_t> requested_{0},epoch_{1};
    RealtimeSnapshot<GraphMonitorFrameV3> published_;
    // Serialized audio callback only; control invalidates via the atomic epoch.
    GraphMonitorFrameV3 staging_{};
    std::array<float,monitor_samples> pending_{},pending_right_{};
    unsigned used_=0,take_=0;
    uint64_t seen_epoch_=0,sequence_=0;
    static float clean(float v){return std::isfinite(v)?v:0;}
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void invalidate(){epoch_.fetch_add(1);}
    template<class Frame> bool read(Frame& frame,uint64_t now){
        requested_.store(now);
        return published_.visit([&](const GraphMonitorFrameV3& snapshot){
            const auto& v=snapshot.audio.signal;
            if(!v.stamp||v.epoch!=epoch_.load()||now<v.stamp||now-v.stamp>fresh_ns)return false;
            if constexpr(std::is_same_v<Frame,GraphMonitorFrameV3>)frame=snapshot;
            else if constexpr(std::is_same_v<Frame,GraphMonitorFrameV2>)frame=snapshot.audio;
            else {static_assert(std::is_same_v<Frame,GraphMonitorFrame>);frame=v;}
            return v.epoch==epoch_.load();
        });
    }
    bool begin(const float* input,unsigned frames,unsigned rate,uint64_t now,const float* right=nullptr){
        auto request=requested_.load(),epoch=epoch_.load();
        if(!request||now<request||now-request>lease_ns||!input||!frames){
            used_=take_=0;return false;
        }
        if(seen_epoch_!=epoch||staging_.audio.signal.rate!=rate||staging_.audio.stereo!=bool(right)){
            used_=0;seen_epoch_=epoch;staging_.audio.signal.rate=rate;staging_.audio.stereo=bool(right);
            if(!right){staging_.audio.input_right.fill(0);staging_.audio.output_right.fill(0);}
        }
        take_=std::min(frames,monitor_samples);
        if(frames>monitor_samples)used_=0;
        for(unsigned i=0;i<take_;++i){
            pending_[i]=clean(input[frames-take_+i]);
            if(right)pending_right_[i]=clean(right[frames-take_+i]);
        }
        return true;
    }
    void end(const float* output,unsigned frames,uint64_t now,const std::array<GraphMonitorControl,8>& controls,unsigned count,const float* right=nullptr,const NodeTelemetryArray& nodes={},unsigned node_count=0){
        if(!output||!take_||take_>frames||staging_.audio.stereo!=bool(right)){used_=take_=0;return;}
        unsigned start=frames-take_;
        for(unsigned i=0;i<take_;++i){
            staging_.audio.signal.input[used_]=pending_[i];staging_.audio.signal.output[used_]=clean(output[start+i]);
            if(right){staging_.audio.input_right[used_]=pending_right_[i];staging_.audio.output_right[used_]=clean(right[start+i]);}
            ++used_;
            if(used_==monitor_samples){
                staging_.audio.signal.stamp=now;staging_.audio.signal.epoch=seen_epoch_;staging_.audio.signal.sequence=++sequence_;
                staging_.audio.signal.controls=controls;staging_.audio.signal.control_count=std::min(count,8u);
                staging_.nodes=nodes;staging_.node_count=std::min(node_count,16u);
                published_.publish(staging_);
                used_=0;
            }
        }
        take_=0;
    }
};
}
