#include "effect-library.hpp"
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
    auto number=[&](const char* k){return float(obs_data_get_double(s,k));};
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=2?SpaceMode(mode):SpaceMode::Bypass;
    f.center=number("center");f.depth=number("depth");f.rate_hz=number("rate_hz");
    f.feedback=number("feedback");f.width=number("width");
    f.stages=unsigned(std::clamp(obs_data_get_int(s,"stages"),2LL,12LL));
    f.predelay=number("predelay");f.decay=number("decay");f.damping=number("damping");f.size=number("size");
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,10);
    return f;
}
void defaults(obs_data_t* s){
    SpaceSettings f;
    obs_data_set_default_int(s,"mode",1);
    auto set=[&](const char* k,double v){obs_data_set_default_double(s,k,v);};
    set("center",f.center);set("depth",f.depth);set("rate_hz",f.rate_hz);
    set("feedback",f.feedback);set("width",f.width);obs_data_set_default_int(s,"stages",f.stages);
    set("predelay",f.predelay);set("decay",f.decay);set("damping",f.damping);set("size",f.size);
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
    auto slider=[&](const char* k,const char* n,double lo,double hi,double step,const char* unit=nullptr){
        auto* prop=obs_properties_add_float_slider(p,k,n,lo,hi,step);if(unit)obs_property_float_set_suffix(prop,unit);return prop;
    };
    slider("center","Center frequency",40,8000,1," Hz");
    slider("depth","Sweep depth",0,4,.05," oct");
    slider("rate_hz","Sweep rate",.05,10,.05," Hz");
    slider("feedback","Phaser feedback",-.85,.85,.01);
    auto* stages=obs_properties_add_list(p,"stages","All-pass stages",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(int i=2;i<=12;i+=2)obs_property_list_add_int(stages,std::to_string(i).c_str(),i);
    slider("predelay","Pre-delay",0,200,.1," ms");
    auto* decay=slider("decay","Nominal decay (RT60)",.1,10,.05," s");
    obs_property_set_long_description(decay,"Per-line decay target before damping; measured room response varies with diffusion and damping.");
    slider("damping","Feedback damping",0,.99,.01);
    slider("size","Room size",.5,1.9,.01);
    slider("width","Stereo width / phase spread",0,1,.01);
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
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
