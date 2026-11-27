#include "effect-library.hpp"
#include "framework/obs/filter_telemetry.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include "framework/obs/realtime_settings.hpp"
namespace {
using namespace hyperdsp;
struct Source{
    RealtimeSettings<MultibandSettings> parameters;
    Multiband engine;FilterTelemetry telemetry;
    unsigned rate=0,channels=0;uint64_t generation=0;
};
void defaults(obs_data_t* s){
    MultibandSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,multiband_crossovers);obs_parameters::defaults(s,f,multiband_parameters);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    MultibandSettings settings;settings.enabled=obs_data_get_int(s,"mode")==1;
    obs_parameters::read(s,settings,multiband_crossovers);obs_parameters::read(s,settings,multiband_parameters);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    try{f->parameters.publish(settings,s,ai.samples_per_sec,get_audio_channels(ai.speakers));f->telemetry.invalidate();}
    catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Multiband] Settings preparation failed: %s",e.what());}
}
void* create(obs_data_t* s,obs_source_t* source){auto f=std::make_unique<Source>();f->telemetry.attach(source);update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Multiband";}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Multiband type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    add_native_modes(mode,multiband_modes);
    for(auto& parameter:multiband_crossovers)obs_parameters::add(p,parameter);
    for(auto& parameter:multiband_parameters)obs_parameters::add(p,parameter);
    obs_properties_add_text(p,"notice","Three linked-channel compressors. The phase-matched wet/dry blend retains crossover phase; bypass removes it. High crossover is limited to 45% of sample rate. Changing crossovers resets filter state.",OBS_TEXT_INFO);
    if(ptr)static_cast<Source*>(ptr)->telemetry.add_button(p);
    return p;
}
obs_audio_data* process(void* ptr,obs_audio_data* a){
    auto* f=static_cast<Source*>(ptr);if(!f||!a)return a;
    auto* state=f->parameters.consume();
    obs_audio_info ai{};
    if(!state||!obs_get_audio_info(&ai)||ai.samples_per_sec!=state->rate||state->rate<8000||state->rate>192000||
        get_audio_channels(ai.speakers)!=state->channels||!state->channels||state->channels>MAX_AUDIO_CHANNELS){f->telemetry.invalidate();return a;}
    if(state->generation!=f->generation){
        if(f->rate!=state->rate||f->channels!=state->channels){
            f->rate=state->rate;f->channels=state->channels;f->engine.reset(f->rate,state->settings);
        }else f->engine.set(state->settings);
        f->generation=state->generation;f->telemetry.invalidate();
    }
    const auto epoch=f->telemetry.epoch();
    std::array<float*,MAX_AUDIO_CHANNELS> p{};
    for(unsigned c=0;c<f->channels;++c)p[c]=reinterpret_cast<float*>(a->data[c]);
    f->engine.process(p.data(),f->channels,a->frames);
    if(state->settings.enabled&&a->frames&&state->generation==f->parameters.latest_generation())
        f->telemetry.publish(f->rate,[&]{return multiband_telemetry(f->engine);},epoch);
    return a;
}
}
extern "C" {
struct obs_source_info multiband_filter={
    .id="hyperdsp_multiband_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
