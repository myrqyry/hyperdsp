#include "effect-library.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include "framework/dsp/time_effect.hpp"
#include "framework/obs/route_properties.hpp"
#include "framework/obs/realtime_settings.hpp"
namespace {
using namespace hyperdsp;
struct Source {
    RealtimeSettings<TimeSettings,TimeEffect> parameters;
    uint64_t generation=0;
};
TimeSettings read(obs_data_t* s){
    TimeSettings f;
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=5?TimeMode(mode):TimeMode::Bypass;
    obs_parameters::read(s,f,time_parameters);
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,5);
    return f;
}
void defaults(obs_data_t* s){
    TimeSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,time_parameters);
    obs_routes::defaults(s);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    auto settings=read(s);obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    const auto channels=get_audio_channels(ai.speakers);
    if(ai.samples_per_sec<8000||ai.samples_per_sec>192000||!channels||channels>MAX_AUDIO_CHANNELS)return;
    try{
        f->parameters.publish(settings,s,ai.samples_per_sec,channels,[](TimeEffect& engine,unsigned rate,unsigned channels,const TimeSettings& settings){
            engine.reset(rate,channels,settings);
        });
    }catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Time] Settings preparation failed: %s",e.what());}
}
void* create(obs_data_t* s,obs_source_t*){auto f=std::make_unique<Source>();update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Time";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    const bool moving=mode>=3&&mode<=5;
    const bool feedback=mode==1||mode==2||mode==4;
    show("delay_ms",mode!=0);show("feedback",feedback);show("damping",feedback);
    show("depth_ms",moving);show("rate_hz",moving);show("stereo",moving);
    auto* delay=obs_properties_get(p,"delay_ms");
    if(delay){
        const double maximum=moving?short_delay_max(mode):2000;
        obs_property_float_set_limits(delay,moving?.1:.001,maximum,moving?.1:.001);
        if(moving&&obs_data_get_double(s,"delay_ms")>maximum)
            obs_data_set_double(s,"delay_ms",short_delay_default(mode));
    }
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Time effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::time_modes);
    obs_parameters::add(p,time_parameters[0]);
    obs_parameters::add(p,time_parameters[1]);
    obs_parameters::add(p,time_parameters[2]);
    obs_parameters::add(p,time_parameters[3]);
    obs_parameters::add(p,time_parameters[4]);
    obs_parameters::add(p,time_parameters[5]);
    obs_parameters::add(p,time_parameters[6]);
    obs_parameters::add(p,time_parameters[7]);
    obs_parameters::add(p,time_parameters[8]);
    obs_routes::add(p,{"Delay (ms)","Feedback","Depth (ms)","Rate (Hz)","Wet / dry","Output amplitude"},
        "Signed amount in destination units. Delay ±2000 ms, feedback ±0.95, depth ±30 ms, rate ±10 Hz, mix/output ±1. Short-delay modes clamp center and excursion.");
    obs_property_set_modified_callback(mode,visibility);return p;
}
obs_audio_data* process(void* p,obs_audio_data* audio){
    auto* f=static_cast<Source*>(p);if(!f||!audio)return audio;
    auto* state=f->parameters.consume();
    obs_audio_info ai{};if(!state||!obs_get_audio_info(&ai)||ai.samples_per_sec!=state->rate||
        get_audio_channels(ai.speakers)!=state->channels)return audio;
    auto& engine=*state->engine;
    if(state->generation!=f->generation){engine.set(state->settings);f->generation=state->generation;}
    std::array<float*,MAX_AUDIO_CHANNELS> buffers{};
    for(unsigned c=0;c<state->channels;++c)buffers[c]=reinterpret_cast<float*>(audio->data[c]);
    engine.external_values(state->external.snapshot());engine.process(buffers.data(),state->channels,audio->frames);
    return audio;
}
}
extern "C" {
struct obs_source_info time_filter={
    .id="hyperdsp_time_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
