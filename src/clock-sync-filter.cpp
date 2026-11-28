#include <obs-module.h>
#include <util/platform.h>
#include "framework/dsp/clock_sync.hpp"
#include "framework/dsp/realtime_handoff.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>
namespace {
using hyperdsp::ClockSync;
struct Configuration {
    unsigned rate=0,channels=0,quality=1;
    double limit=1000;
    bool arrival=false;
    bool operator==(const Configuration&)const=default;
};
struct Prepared {
    Configuration config;
    uint64_t generation=0;
    ClockSync engine;
};
struct Source {
    std::mutex control; // writers only
    Configuration accepted;
    hyperdsp::RealtimeHandoff<Prepared> handoff;
    std::atomic<uint64_t> requested{0};
    std::atomic<bool> interrupted{false};
    obs_audio_data output{};
    // The audio thread is the only diagnostics writer. A pending generation
    // reports acquiring until the next packet adopts the prepared engine.
    std::atomic<uint64_t> sequence{0},generation{0},relocks{0},last_packet{0};
    std::atomic<double> drift{0},correction{0},error{0};
    std::atomic<unsigned> flags{0};
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void publish(const Prepared& state,uint64_t stamp){
        auto d=state.engine.diagnostics();
        sequence.fetch_add(1);
        drift.store(d.drift_ppm);correction.store(d.correction_ppm);error.store(d.error_ms);
        relocks.store(d.relocks);flags.store(1u|(d.locked?2u:0u)|(d.limited?4u:0u));
        generation.store(state.generation);last_packet.store(stamp);
        sequence.fetch_add(1);
    }
    bool snapshot(ClockSync::Diagnostics& d)const{
        for(int i=0;i<3;++i){
            auto a=sequence.load();if(a&1)continue;
            auto flag=flags.load();
            auto applied=generation.load(),stamp=last_packet.load();
            d={drift.load(),correction.load(),error.load(),relocks.load(),bool(flag&2),bool(flag&4)};
            if(sequence.load()==a){
                auto desired=requested.load();
                if(desired!=applied){d={};return desired!=0;}
                auto now=os_gettime_ns();
                if(!stamp||now<stamp||now-stamp>1000000000ULL)d.locked=false;
                return bool(flag&1)&&!interrupted.load();
            }
        }
        return false;
    }
};
const char* name(void*){return "HyperDSP Clock Sync";}
void defaults(obs_data_t* s){
    obs_data_set_default_int(s,"clock",0);
    obs_data_set_default_int(s,"quality",1);
    obs_data_set_default_double(s,"limit_ppm",1000);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    auto rate=ai.samples_per_sec,channels=get_audio_channels(ai.speakers);
    unsigned quality=unsigned(std::clamp(obs_data_get_int(s,"quality"),0LL,2LL));
    double limit=obs_data_get_double(s,"limit_ppm");
    limit=std::isfinite(limit)?std::clamp(limit,50.,2000.):1000.;
    bool arrival=obs_data_get_int(s,"clock")==1;
    const Configuration config{rate,channels,quality,limit,arrival};
    std::lock_guard lock(f->control);
    if(f->requested.load()&&f->accepted==config)return;
    try{
        auto next=std::make_unique<Prepared>();
        next->config=config;
        if(!next->engine.prepare(rate,channels,quality,limit))return;
        next->generation=f->requested.load()+1;
        f->accepted=config;
        f->requested.store(next->generation);
        f->handoff.publish(std::move(next));
    }catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Clock Sync] Preparation failed: %s",e.what());}
}
void status(void* ptr,calldata_t* data){
    auto* f=static_cast<Source*>(ptr);calldata_set_bool(data,"available",false);
    if(!f)return;
    ClockSync::Diagnostics d;if(!f->snapshot(d))return;
    calldata_set_bool(data,"available",true);calldata_set_bool(data,"locked",d.locked);
    calldata_set_bool(data,"limited",d.limited);calldata_set_float(data,"drift_ppm",d.drift_ppm);
    calldata_set_float(data,"correction_ppm",d.correction_ppm);calldata_set_float(data,"ratio",1+d.correction_ppm*1e-6);
    calldata_set_float(data,"error_ms",d.error_ms);calldata_set_int(data,"relocks",d.relocks);
}
void* create(obs_data_t* s,obs_source_t* source){
    auto f=std::make_unique<Source>();update(f.get(),s);
    proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_clock_sync_status_v1(out bool available, out bool locked, out bool limited, out float drift_ppm, out float correction_ppm, out float ratio, out float error_ms, out int relocks)",status,f.get());
    return f.release();
}
void destroy(void* p){delete static_cast<Source*>(p);}
std::string description(Source* f){
    if(!f)return "Synchronizes a live capture clock; not for local file playback.";
    ClockSync::Diagnostics d;if(!f->snapshot(d))return "Diagnostics unavailable; refresh to retry.";
    char buffer[320];
    std::snprintf(buffer,sizeof(buffer),"%s | estimated correction %+.1f ppm | applied %+.1f ppm (%.7f) | phase error %+.3f ms | relocks %llu",
        d.limited?"Correction limit reached":d.locked?"Locked":"Acquiring",d.drift_ppm,d.correction_ppm,1+d.correction_ppm*1e-6,d.error_ms,(unsigned long long)d.relocks);
    return buffer;
}
bool refresh(obs_properties_t* p,obs_property_t*,void* ptr){
    auto text=description(static_cast<Source*>(ptr));obs_property_set_description(obs_properties_get(p,"diagnostics"),text.c_str());return true;
}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();
    auto* clock=obs_properties_add_list(p,"clock","Reference clock",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(clock,"Source timestamps",0);obs_property_list_add_int(clock,"OBS arrival time (jitter sensitive)",1);
    auto* quality=obs_properties_add_list(p,"quality","Resampling quality",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(quality,"Fast sinc",0);obs_property_list_add_int(quality,"Medium sinc",1);obs_property_list_add_int(quality,"Best sinc",2);
    obs_properties_add_float_slider(p,"limit_ppm","Maximum correction (ppm)",50,2000,50);
    auto text=description(static_cast<Source*>(ptr));
    obs_properties_add_text(p,"diagnostics",text.c_str(),OBS_TEXT_INFO);
    obs_properties_add_button2(p,"refresh_diagnostics","Refresh diagnostics",refresh,ptr);
    obs_properties_add_text(p,"notice","Use on live capture sources before other effects. Does not synchronize independent monitoring hardware. Avoid local media files.",OBS_TEXT_INFO);
    return p;
}
obs_audio_data* process(void* ptr,obs_audio_data* audio){
    auto* f=static_cast<Source*>(ptr);if(!f||!audio||!audio->frames)return audio;
    // OBS serializes callbacks for this source and consumes returned buffers
    // before the next callback. Retire previous state only at that boundary.
    auto* state=f->handoff.consume();
    obs_audio_info ai{};
    if(!state||!obs_get_audio_info(&ai)||ai.samples_per_sec!=state->config.rate||get_audio_channels(ai.speakers)!=state->config.channels){
        f->interrupted=true;return audio;
    }
    const auto& config=state->config;
    if(f->interrupted.exchange(false))state->engine.reset();
    std::array<const float*,8> buffers{};
    for(unsigned c=0;c<config.channels;++c)buffers[c]=reinterpret_cast<const float*>(audio->data[c]);
    uint64_t timestamp=audio->timestamp;
    if(config.arrival){
        auto now=os_gettime_ns();auto duration=uint64_t(audio->frames)*1000000000ULL/config.rate;
        timestamp=now>duration?now-duration:0;
    }
    auto out=state->engine.process(buffers.data(),audio->frames,timestamp);
    f->publish(*state,out.valid?os_gettime_ns():0);
    if(!out.valid)return audio;
    if(!out.frames)return nullptr; // sinc priming can buffer a complete short packet
    f->output=*audio;f->output.frames=out.frames;f->output.timestamp=out.timestamp;
    for(unsigned c=0;c<config.channels;++c)f->output.data[c]=reinterpret_cast<uint8_t*>(const_cast<float*>(out.data[c]));
    return &f->output;
}
}
extern "C" {
struct obs_source_info clock_sync_filter={
    .id="hyperdsp_clock_sync_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
