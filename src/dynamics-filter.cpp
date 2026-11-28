#include "effect-library.hpp"
#include "framework/obs/filter_telemetry.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include "framework/dsp/dynamics.hpp"
#include "framework/obs/route_properties.hpp"
#include "framework/obs/realtime_settings.hpp"
namespace {
using namespace hyperdsp;
struct Source {
    RealtimeSettings<DynamicsSettings> parameters;
    Dynamics engine;FilterTelemetry telemetry;
    unsigned rate=0,channels=0;uint64_t generation=0;
};
DynamicsSettings read(obs_data_t* s){
    DynamicsSettings f;
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=4?DynamicsMode(mode):DynamicsMode::Bypass;
    obs_parameters::read(s,f,dynamics_parameters);
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,5);
    return f;
}
void defaults(obs_data_t* s){
    DynamicsSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,dynamics_parameters);
    obs_routes::defaults(s);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    auto settings=read(s);obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    auto channels=get_audio_channels(ai.speakers);
    try{f->parameters.publish(settings,s,ai.samples_per_sec,channels);f->telemetry.invalidate();}
    catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Dynamics] Settings preparation failed: %s",e.what());}
}
void* create(obs_data_t* s,obs_source_t* source){auto f=std::make_unique<Source>();f->telemetry.attach(source);update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Dynamics";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    show("ratio",mode==1||mode==2);show("knee",mode==1||mode==2);
    show("range",mode==2||mode==3);show("hold",mode==3);show("hysteresis",mode==3);
    show("attack",mode>=1&&mode<=3);show("release",mode!=0);show("threshold",mode!=0);
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void* ptr){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Dynamics type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::dynamics_modes);
    auto* threshold=obs_parameters::add(p,dynamics_parameters[0]);
    obs_property_set_long_description(threshold,"Limiter ceiling applies to the wet branch before output gain. No lookahead or true-peak guarantee.");
    obs_parameters::add(p,dynamics_parameters[1]);
    obs_parameters::add(p,dynamics_parameters[2]);
    obs_parameters::add(p,dynamics_parameters[3]);
    obs_parameters::add(p,dynamics_parameters[4]);
    obs_parameters::add(p,dynamics_parameters[5]);
    obs_parameters::add(p,dynamics_parameters[6]);
    obs_parameters::add(p,dynamics_parameters[7]);
    obs_parameters::add(p,dynamics_parameters[8]);
    obs_parameters::add(p,dynamics_parameters[9]);
    obs_parameters::add(p,dynamics_parameters[10]);
    obs_parameters::add(p,dynamics_parameters[11]);
    obs_routes::add(p,{"Threshold (dB)","Ratio","Makeup (dB)","Range (dB)","Wet / dry","Output amplitude"},
        "Signed amount in destination units. Threshold ±80 dB, ratio ±19, makeup ±24 dB, range ±100 dB, mix/output ±1 per route. Summed before bounds.");
    obs_property_set_modified_callback(mode,visibility);if(ptr)static_cast<Source*>(ptr)->telemetry.add_button(p);return p;
}
obs_audio_data* process(void* p,obs_audio_data* audio){
    auto* f=static_cast<Source*>(p);if(!f||!audio)return audio;
    auto* state=f->parameters.consume();
    obs_audio_info ai{};
    if(!state||!obs_get_audio_info(&ai)||ai.samples_per_sec!=state->rate||
        state->rate<8000||state->rate>192000||get_audio_channels(ai.speakers)!=state->channels||state->channels>MAX_AUDIO_CHANNELS){
        f->telemetry.invalidate();return audio;
    }
    if(state->generation!=f->generation){
        if(f->rate!=state->rate||f->channels!=state->channels){
            f->rate=state->rate;f->channels=state->channels;f->engine.reset(f->rate,state->settings);
        }else f->engine.set(state->settings);
        f->generation=state->generation;f->telemetry.invalidate();
    }
    const auto epoch=f->telemetry.epoch();
    std::array<float*,MAX_AUDIO_CHANNELS> buffers{};
    for(unsigned c=0;c<f->channels;++c)buffers[c]=reinterpret_cast<float*>(audio->data[c]);
    f->engine.external_values(state->external.snapshot());f->engine.process(buffers.data(),f->channels,audio->frames);
    if(state->settings.mode!=DynamicsMode::Bypass&&audio->frames&&state->generation==f->parameters.latest_generation())
        f->telemetry.publish(f->rate,[&]{return dynamics_telemetry(f->engine);},epoch);
    return audio;
}
}
extern "C" {
struct obs_source_info dynamics_filter={
    .id="hyperdsp_dynamics_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
