#include "effect-library.hpp"
#include "framework/obs/filter_telemetry.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include "framework/obs/realtime_settings.hpp"
namespace {
using namespace hyperdsp;
struct Settings{int mode=0;TransientSettings single;BandTransientSettings band;};
struct Source{
    RealtimeSettings<Settings> parameters;
    Transient engine;BandTransient band;FilterTelemetry telemetry;
    unsigned rate=0,channels=0;int mode=0;uint64_t generation=0;
};
void defaults(obs_data_t* s){
    TransientSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,transient_parameters);
    obs_parameters::defaults(s,BandTransientSettings{},band_transient_crossovers);
    obs_parameters::defaults(s,BandTransientSettings{},band_transient_parameters);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    auto mode=obs_data_get_int(s,"mode");
    if(mode<0||mode>2)mode=0;
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    Settings settings;settings.mode=int(mode);
    if(mode==1)obs_parameters::read(s,settings.single,transient_parameters);
    else if(mode==2){
        obs_parameters::read(s,settings.band,band_transient_crossovers);obs_parameters::read(s,settings.band,band_transient_parameters);
    }
    try{f->parameters.publish(settings,s,ai.samples_per_sec,get_audio_channels(ai.speakers));f->telemetry.invalidate();}
    catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Transient] Settings preparation failed: %s",e.what());}
}
void* create(obs_data_t* s,obs_source_t* source){auto f=std::make_unique<Source>();f->telemetry.attach(source);update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Transient";}
bool visibility(obs_properties_t* p,obs_property_t*,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    show("attack_db",mode==1);show("sustain_db",mode==1);
    for(auto& parameter:band_transient_crossovers)show(parameter.key,mode==2);
    for(auto& parameter:band_transient_parameters)if(parameter.array)show(parameter.key,mode==2);
    for(auto& parameter:transient_parameters)if(std::string_view(parameter.key)!="attack_db"&&std::string_view(parameter.key)!="sustain_db")show(parameter.key,mode==1||mode==2);
    return true;
}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Transient type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    add_native_modes(mode,transient_modes);
    for(auto& parameter:band_transient_crossovers)obs_parameters::add(p,parameter);
    for(auto& parameter:band_transient_parameters)if(parameter.array)obs_parameters::add(p,parameter);
    for(auto& parameter:transient_parameters)obs_parameters::add(p,parameter);
    obs_property_set_modified_callback(mode,visibility);
    auto* initial=obs_data_create();obs_data_set_int(initial,"mode",1);
    visibility(p,mode,initial);obs_data_release(initial);
    obs_properties_add_text(p,"notice","Linked-channel onset and decay shaping. Positive gain enhances the selected contour; negative gain attenuates it. Multiband uses a phase-matched crossover and resets state on crossover edits. Bypass removes crossover phase. No lookahead or peak limiter.",OBS_TEXT_INFO);
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
        const auto& settings=state->settings;
        bool reset=f->rate!=state->rate||f->channels!=state->channels||f->mode!=settings.mode;
        f->rate=state->rate;f->channels=state->channels;f->mode=settings.mode;
        if(f->mode==1){if(reset)f->engine.reset(f->rate,settings.single);else f->engine.set(settings.single);}
        else if(f->mode==2){if(reset)f->band.reset(f->rate,settings.band);else f->band.set(settings.band);}
        f->generation=state->generation;f->telemetry.invalidate();
    }
    const auto epoch=f->telemetry.epoch();
    std::array<float*,MAX_AUDIO_CHANNELS> p{};
    for(unsigned c=0;c<f->channels;++c)p[c]=reinterpret_cast<float*>(a->data[c]);
    if(f->mode==1)f->engine.process(p.data(),f->channels,a->frames);
    else if(f->mode==2)f->band.process(p.data(),f->channels,a->frames);
    if(a->frames&&f->mode&&state->generation==f->parameters.latest_generation())f->telemetry.publish(f->rate,[&]{
        return f->mode==1?transient_telemetry(f->engine):band_transient_telemetry(f->band);
    },epoch);
    return a;
}
}
extern "C" {
struct obs_source_info transient_filter={
    .id="hyperdsp_transient_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
