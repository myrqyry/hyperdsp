#include "effect-library.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdio>
namespace {
using namespace hyperdsp;
struct Source{
    std::mutex mutex;Stereo engine;StereoAnalysis meter;obs_source_t* source=nullptr;
    unsigned rate=0,channels=0;
    std::atomic<uint64_t> sequence{0},stamp{0};
    std::atomic<double> correlation{0},loss{0},side{0};
    std::atomic<unsigned> flags{0};
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void publish(StereoMetrics m,uint64_t now){
        sequence.fetch_add(1);correlation.store(m.correlation);loss.store(m.mono_loss_db);side.store(m.side_fraction);
        flags.store((m.valid?1:0)|(m.correlation_valid?2:0));stamp.store(now);sequence.fetch_add(1);
    }
    bool read(StereoMetrics& m){
        for(int i=0;i<3;++i){
            auto a=sequence.load();if(a&1)continue;
            auto f=flags.load();auto time=stamp.load();
            m={bool(f&1),bool(f&2),correlation.load(),loss.load(),side.load()};
            auto now=os_gettime_ns();
            if(sequence.load()==a)return m.valid&&time&&now>=time&&now-time<500000000ULL&&source&&obs_source_enabled(source);
        }
        return false;
    }
};
void defaults(obs_data_t* s){obs_data_set_default_int(s,"mode",1);obs_parameters::defaults(s,StereoSettings{},stereo_parameters);}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    StereoSettings settings;settings.enabled=obs_data_get_int(s,"mode")==1;obs_parameters::read(s,settings,stereo_parameters);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    std::lock_guard lock(f->mutex);auto channels=get_audio_channels(ai.speakers);
    if(f->rate!=ai.samples_per_sec||f->channels!=channels){
        f->rate=ai.samples_per_sec;f->channels=channels;f->engine.reset(f->rate,settings);
    }else f->engine.set(settings);
    f->meter.reset(f->rate);f->publish({},0);
}
void status(void* ptr,calldata_t* d){
    auto* f=static_cast<Source*>(ptr);StereoMetrics m;
    bool available=f&&f->read(m);calldata_set_bool(d,"available",available);if(!available)return;
    calldata_set_bool(d,"correlation_valid",m.correlation_valid);calldata_set_float(d,"correlation",m.correlation);
    calldata_set_float(d,"mono_loss_db",m.mono_loss_db);calldata_set_float(d,"side_fraction",m.side_fraction);
}
void* create(obs_data_t* s,obs_source_t* source){
    auto f=std::make_unique<Source>();f->source=source;update(f.get(),s);
    proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_stereo_status_v1(out bool available, out bool correlation_valid, out float correlation, out float mono_loss_db, out float side_fraction)",status,f.get());
    return f.release();
}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Stereo";}
std::string description(Source* f){
    StereoMetrics m;if(!f||!f->read(m))return "Stereo diagnostics unavailable; play stereo audio and refresh.";
    char correlation[32],line[256];
    if(m.correlation_valid)std::snprintf(correlation,sizeof(correlation),"%+.3f",m.correlation);
    else std::snprintf(correlation,sizeof(correlation),"undefined");
    std::snprintf(line,sizeof(line),"Output correlation %s | mono fold-down %+.1f dB | side energy %.1f%%",correlation,m.mono_loss_db,m.side_fraction*100);
    return line;
}
bool refresh(obs_properties_t* p,obs_property_t*,void* ptr){
    auto line=description(static_cast<Source*>(ptr));obs_property_set_description(obs_properties_get(p,"diagnostics"),line.c_str());return true;
}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();auto* mode=obs_properties_add_list(p,"mode","Stereo type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);add_native_modes(mode,stereo_modes);
    for(auto& parameter:stereo_parameters)obs_parameters::add(p,parameter);
    auto line=description(static_cast<Source*>(ptr));obs_properties_add_text(p,"diagnostics",line.c_str(),OBS_TEXT_INFO);
    obs_properties_add_button2(p,"refresh_diagnostics","Refresh diagnostics",refresh,ptr);
    obs_properties_add_text(p,"notice","Stereo only; other layouts bypass. Width changes side gain; balance attenuates the opposite channel. Negative correlation indicates potential mono cancellation. No peak limiter.",OBS_TEXT_INFO);return p;
}
obs_audio_data* process(void* ptr,obs_audio_data* a){
    auto* f=static_cast<Source*>(ptr);if(!f||!a||!a->frames)return a;
    std::unique_lock lock(f->mutex,std::try_to_lock);if(!lock.owns_lock())return a;
    obs_audio_info ai{};if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||f->rate<8000||f->rate>192000||get_audio_channels(ai.speakers)!=2||f->channels!=2||!a->data[0]||!a->data[1])return a;
    float* p[]={reinterpret_cast<float*>(a->data[0]),reinterpret_cast<float*>(a->data[1])};
    f->engine.process(p,2,a->frames);
    for(unsigned j=0;j<a->frames;++j)f->meter.next(p[0][j],p[1][j]);
    f->publish(f->meter.snapshot(),os_gettime_ns());return a;
}
}
extern "C" {
struct obs_source_info stereo_filter={.id="hyperdsp_stereo_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process};
}
