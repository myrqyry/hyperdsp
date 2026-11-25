#include "effect-library.hpp"
#include <obs-module.h>
#include "framework/dsp/spectral.hpp"
#include "framework/obs/route_properties.hpp"
#include <memory>
#include <mutex>
namespace {
using namespace hyperdsp;
struct Source {
    std::mutex mutex;
    Spectral engine;
    ExternalInputs external;
    unsigned rate=0,channels=0;
};
SpectralSettings read(obs_data_t* s){
    SpectralSettings f;
    auto number=[&](const char* k){return float(obs_data_get_double(s,k));};
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=5?SpectralMode(mode):SpectralMode::Bypass;
    f.threshold=number("threshold");f.range=number("range");f.attack=number("attack");f.release=number("release");
    f.hold=number("hold");f.smear_ms=number("smear_ms");
    f.pitch=number("pitch");f.shift_hz=number("shift_hz");
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,7);
    return f;
}
void defaults(obs_data_t* s){
    SpectralSettings f;
    obs_data_set_default_int(s,"mode",1);
    auto set=[&](const char* k,double v){obs_data_set_default_double(s,k,v);};
    set("threshold",f.threshold);set("range",f.range);set("attack",f.attack);set("release",f.release);
    set("hold",f.hold);set("smear_ms",f.smear_ms);set("pitch",f.pitch);set("shift_hz",f.shift_hz);
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
const char* name(void*){return "HyperDSP Spectral";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    for(auto k:{"threshold","range","attack","release"})show(k,mode==1);
    show("hold",mode==2);show("smear_ms",mode==3);show("pitch",mode==4);show("shift_hz",mode==5);
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Spectral effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::spectral_modes);
    auto slider=[&](const char* k,const char* n,double lo,double hi,double step,const char* unit=nullptr){
        auto* prop=obs_properties_add_float_slider(p,k,n,lo,hi,step);if(unit)obs_property_float_set_suffix(prop,unit);return prop;
    };
    obs_property_set_long_description(mode,"1024-sample analysis/synthesis delay when active, with internal dry alignment. Bypass is undelayed.");
    slider("threshold","Bin threshold",-100,0,.1," dBFS");
    slider("range","Bin attenuation range",0,100,.1," dB");
    slider("attack","Gate opening time",1,500,1," ms");
    slider("release","Gate closing time",1,2000,1," ms");
    auto* hold=slider("hold","Freeze hold",0,1,1);
    obs_property_set_long_description(hold,"Captures the next analysis frame on crossing 0.5 and continues its measured bin phase advance until released.");
    slider("smear_ms","Spectral memory time",10,2000,10," ms");
    slider("pitch","Pitch shift",-24,24,.1," semitones");
    slider("shift_hz","Frequency shift",-5000,5000,1," Hz");
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
    obs_routes::add(p,{"Threshold (dB)","Range (dB)","Freeze hold","Memory time (ms)","Wet / dry","Output amplitude","Pitch (semitones)","Frequency shift (Hz)"},
        "Signed amounts in destination units. Freeze is triggered when summed hold crosses 0.5. Spectral destinations are sampled at analysis hops.");
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
struct obs_source_info spectral_filter={
    .id="hyperdsp_spectral_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
