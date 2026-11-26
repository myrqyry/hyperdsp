#include "effect-library.hpp"
#include "framework/obs/filter_telemetry.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include <mutex>
#include <memory>
namespace {
using namespace hyperdsp;
struct Source{std::mutex mutex;DynamicEq engine;FilterTelemetry telemetry;bool active=false;unsigned rate=0,channels=0;};
void defaults(obs_data_t* s){
    DynamicEqSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,dynamic_eq_parameters);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    DynamicEqSettings settings;settings.enabled=obs_data_get_int(s,"mode")==1;
    obs_parameters::read(s,settings,dynamic_eq_parameters);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    std::lock_guard lock(f->mutex);f->telemetry.invalidate();
    f->active=settings.enabled;auto channels=get_audio_channels(ai.speakers);
    if(f->rate!=ai.samples_per_sec||f->channels!=channels){
        f->rate=ai.samples_per_sec;f->channels=channels;f->engine.reset(f->rate,settings);
    }else f->engine.set(settings);
}
void* create(obs_data_t* s,obs_source_t* source){auto f=std::make_unique<Source>();f->telemetry.attach(source);update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Dynamic EQ";}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Dynamic EQ type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    add_native_modes(mode,dynamic_eq_modes);
    for(auto& parameter:dynamic_eq_parameters)obs_parameters::add(p,parameter);
    obs_properties_add_text(p,"notice","Frequency-selective linked peak detector. Negative dynamic gain cuts loud bands; positive gain boosts them. Set center near 5–8 kHz for de-essing. No lookahead or peak limiter.",OBS_TEXT_INFO);
    if(ptr)static_cast<Source*>(ptr)->telemetry.add_button(p);
    return p;
}
obs_audio_data* process(void* ptr,obs_audio_data* a){
    auto* f=static_cast<Source*>(ptr);if(!f||!a)return a;
    std::unique_lock lock(f->mutex,std::try_to_lock);if(!lock.owns_lock()){f->telemetry.invalidate();return a;}
    obs_audio_info ai{};if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||f->rate<8000||f->rate>192000||
        get_audio_channels(ai.speakers)!=f->channels||f->channels>MAX_AUDIO_CHANNELS){f->telemetry.invalidate();return a;}
    std::array<float*,MAX_AUDIO_CHANNELS> p{};
    for(unsigned c=0;c<f->channels;++c)p[c]=reinterpret_cast<float*>(a->data[c]);
    f->engine.process(p.data(),f->channels,a->frames);
    if(f->active&&a->frames)f->telemetry.publish(f->rate,[&]{return dynamic_eq_telemetry(f->engine);});
    return a;
}
}
extern "C" {
struct obs_source_info dynamic_eq_filter={
    .id="hyperdsp_dynamic_eq_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
