#include "effect-library.hpp"
#include <obs-module.h>
#include "framework/dsp/time_effect.hpp"
#include "framework/obs/route_properties.hpp"
#include <memory>
#include <mutex>
namespace {
using namespace hyperdsp;
struct Source {
    std::mutex mutex;
    TimeEffect engine;
    ExternalInputs external;
    unsigned rate=0,channels=0;
};
TimeSettings read(obs_data_t* s){
    TimeSettings f;
    auto number=[&](const char* k){return float(obs_data_get_double(s,k));};
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=5?TimeMode(mode):TimeMode::Bypass;
    f.delay_ms=number("delay_ms");f.feedback=number("feedback");f.damping=number("damping");
    f.depth_ms=number("depth_ms");f.rate_hz=number("rate_hz");f.stereo=number("stereo");
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,5);
    return f;
}
void defaults(obs_data_t* s){
    TimeSettings f;
    obs_data_set_default_int(s,"mode",1);
    auto set=[&](const char* k,double v){obs_data_set_default_double(s,k,v);};
    set("delay_ms",f.delay_ms);set("feedback",f.feedback);set("damping",f.damping);
    set("depth_ms",f.depth_ms);set("rate_hz",f.rate_hz);set("stereo",f.stereo);
    set("mix",f.wet);set("input_db",f.input_db);set("output_db",f.output_db);
    obs_routes::defaults(s);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    auto settings=read(s);obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    std::lock_guard lock(f->mutex);
    for(unsigned i=0;i<modulation_slots;++i)obs_routes::connect(f->external,s,i,settings.routes[i].source);
    auto channels=get_audio_channels(ai.speakers);
    if(f->rate!=ai.samples_per_sec||f->channels!=channels){
        f->rate=ai.samples_per_sec;f->channels=channels;f->engine.reset(f->rate,f->channels,settings);
    }else f->engine.set(settings);
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
        const double maximum=mode==4?10:moving?40:2000;
        obs_property_float_set_limits(delay,moving?.1:.001,maximum,moving?.1:.001);
        if(moving&&obs_data_get_double(s,"delay_ms")>maximum)
            obs_data_set_double(s,"delay_ms",mode==4?2:mode==3?15:8);
    }
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Time effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::time_modes);
    auto slider=[&](const char* k,const char* n,double lo,double hi,double step,const char* unit=nullptr){
        auto* prop=obs_properties_add_float_slider(p,k,n,lo,hi,step);if(unit)obs_property_float_set_suffix(prop,unit);return prop;
    };
    slider("delay_ms","Delay / center",.001,2000,.001," ms");
    slider("feedback","Feedback",-.95,.95,.01);
    slider("damping","Feedback damping",0,.99,.01);
    slider("depth_ms","Modulation depth",0,30,.1," ms");
    slider("rate_hz","Modulation rate",.05,10,.05," Hz");
    slider("stereo","Stereo phase spread",0,1,.01);
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
    obs_routes::add(p,{"Delay (ms)","Feedback","Depth (ms)","Rate (Hz)","Wet / dry","Output amplitude"},
        "Signed amount in destination units. Delay ±2000 ms, feedback ±0.95, depth ±30 ms, rate ±10 Hz, mix/output ±1. Short-delay modes clamp center and excursion.");
    obs_property_set_modified_callback(mode,visibility);return p;
}
obs_audio_data* process(void* p,obs_audio_data* audio){
    auto* f=static_cast<Source*>(p);if(!f||!audio)return audio;
    std::unique_lock lock(f->mutex,std::try_to_lock);if(!lock.owns_lock())return audio;
    obs_audio_info ai{};if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||
        f->rate<8000||f->rate>192000||get_audio_channels(ai.speakers)!=f->channels||f->channels>MAX_AUDIO_CHANNELS)return audio;
    std::array<float*,MAX_AUDIO_CHANNELS> buffers{};
    for(unsigned c=0;c<f->channels;++c)buffers[c]=reinterpret_cast<float*>(audio->data[c]);
    f->engine.external_values(f->external.snapshot());f->engine.process(buffers.data(),f->channels,audio->frames);
    return audio;
}
}
extern "C" {
struct obs_source_info time_filter={
    .id="hyperdsp_time_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
