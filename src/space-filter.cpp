#include "effect-library.hpp"
#include "framework/obs/parameter_properties.hpp"
#include <obs-module.h>
#include "framework/dsp/space.hpp"
#include "framework/obs/route_properties.hpp"
#include <memory>
#include <mutex>
namespace {
using namespace hyperdsp;
struct Source {
    std::mutex mutex;
    Space engine;
    ExternalInputs external;
    unsigned rate=0,channels=0;
};
SpaceSettings read(obs_data_t* s){
    SpaceSettings f;
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=2?SpaceMode(mode):SpaceMode::Bypass;
    obs_parameters::read(s,f,space_parameters);
    f.stages=unsigned(std::clamp(obs_data_get_int(s,"stages"),2LL,12LL));
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,10);
    return f;
}
void defaults(obs_data_t* s){
    SpaceSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,space_parameters);
    obs_data_set_default_int(s,"stages",f.stages);
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
const char* name(void*){return "HyperDSP Space";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    for(auto k:{"center","depth","rate_hz","feedback","stages"})show(k,mode==1);
    for(auto k:{"predelay","decay","damping","size"})show(k,mode==2);
    show("width",mode!=0);
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Space effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::space_modes);
    obs_parameters::add(p,space_parameters[0]);
    obs_parameters::add(p,space_parameters[1]);
    obs_parameters::add(p,space_parameters[2]);
    obs_parameters::add(p,space_parameters[3]);
    auto* stages=obs_properties_add_list(p,"stages","All-pass stages",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(auto i:space_stages)obs_property_list_add_int(stages,std::to_string(i).c_str(),i);
    obs_parameters::add(p,space_parameters[4]);
    auto* decay=obs_parameters::add(p,space_parameters[5]);
    obs_property_set_long_description(decay,"Per-line decay target before damping; measured room response varies with diffusion and damping.");
    obs_parameters::add(p,space_parameters[6]);
    obs_parameters::add(p,space_parameters[7]);
    obs_parameters::add(p,space_parameters[8]);
    obs_parameters::add(p,space_parameters[9]);
    obs_parameters::add(p,space_parameters[10]);
    obs_parameters::add(p,space_parameters[11]);
    obs_routes::add(p,{"Center (Hz)","Sweep depth (octaves)","Sweep rate (Hz)","Phaser feedback","Wet / dry","Output amplitude","Decay (s)","Damping","Pre-delay (ms)","Room size","Stereo width"},
        "Signed amounts in destination units. Same-destination contributions sum before bounds. Only destinations applicable to the selected mode affect it.");
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
struct obs_source_info space_filter={
    .id="hyperdsp_space_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
