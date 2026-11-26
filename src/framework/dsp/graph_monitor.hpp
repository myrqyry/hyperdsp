#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <mutex>
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
class GraphMonitor {
    static constexpr uint64_t lease_ns=1000000000ULL, fresh_ns=500000000ULL;
    std::atomic<uint64_t> requested_{0},epoch_{1};
    std::mutex snapshot_mutex_;
    GraphMonitorFrame published_{};
    // Audio thread only, while the graph processing lock is held.
    GraphMonitorFrame staging_{};
    std::array<float,monitor_samples> pending_{};
    unsigned used_=0,take_=0;
    uint64_t seen_epoch_=0,sequence_=0;
    static float clean(float v){return std::isfinite(v)?v:0;}
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void invalidate(){epoch_.fetch_add(1);}
    bool read(GraphMonitorFrame& frame,uint64_t now){
        requested_.store(now);
        std::lock_guard lock(snapshot_mutex_);
        if(!published_.stamp||published_.epoch!=epoch_.load()||now<published_.stamp||now-published_.stamp>fresh_ns)return false;
        frame=published_;return true;
    }
    bool begin(const float* input,unsigned frames,unsigned rate,uint64_t now){
        auto request=requested_.load(),epoch=epoch_.load();
        if(!request||now<request||now-request>lease_ns||!input||!frames){
            used_=take_=0;return false;
        }
        if(seen_epoch_!=epoch||staging_.rate!=rate){used_=0;seen_epoch_=epoch;staging_.rate=rate;}
        take_=std::min(frames,monitor_samples);
        if(frames>monitor_samples)used_=0;
        for(unsigned i=0;i<take_;++i)pending_[i]=clean(input[frames-take_+i]);
        return true;
    }
    void end(const float* output,unsigned frames,uint64_t now,const std::array<GraphMonitorControl,8>& controls,unsigned count){
        if(!output||!take_||take_>frames){used_=take_=0;return;}
        unsigned start=frames-take_;
        for(unsigned i=0;i<take_;++i){
            staging_.input[used_]=pending_[i];staging_.output[used_]=clean(output[start+i]);++used_;
            if(used_==monitor_samples){
                staging_.stamp=now;staging_.epoch=seen_epoch_;staging_.sequence=++sequence_;
                staging_.controls=controls;staging_.control_count=std::min(count,8u);
                std::unique_lock lock(snapshot_mutex_,std::try_to_lock);
                if(lock.owns_lock())published_=staging_;
                used_=0;
            }
        }
        take_=0;
    }
};
}
