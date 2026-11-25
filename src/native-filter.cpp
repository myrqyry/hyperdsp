#include <obs-module.h>
#include "framework/dsp/native_filter.hpp"
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <initializer_list>
namespace {
using namespace hyperdsp;
struct NativeSource {
    std::mutex mutex;
    NativeFilter filter;
    unsigned rate=0;
};
FilterSettings read(obs_data_t* s){
    FilterSettings f;
    auto enumeration=[&](const char* key,int max,int fallback){
        const auto i=obs_data_get_int(s,key);return i>=0&&i<=max?int(i):fallback;
    };
    auto number=[&](const char* key){return float(obs_data_get_double(s,key));};
    f.mode=FilterMode(enumeration("mode",9,0));
    f.frequency=number("frequency");f.q=number("q");
    f.gain_db=number("gain_db");f.comb_depth=number("comb_depth");
    f.alignment=FilterAlignment(enumeration("alignment",1,0));
    f.stages=unsigned(std::clamp(obs_data_get_int(s,"stages"),1LL,4LL));
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    f.source=ModSource(enumeration("mod_source",2,0));f.target=ModTarget(enumeration("mod_target",4,0));
    f.shape=WaveShape(enumeration("wave_shape",3,0));
    f.hz=number("lfo_rate");f.depth=number("mod_depth");
    f.attack_ms=number("attack_ms");f.release_ms=number("release_ms");
    return f;
}
void defaults(obs_data_t* s){
    obs_data_set_default_int(s,"mode",1);obs_data_set_default_int(s,"stages",1);obs_data_set_default_int(s,"alignment",0);
    obs_data_set_default_double(s,"frequency",1000);obs_data_set_default_double(s,"q",.70710678);
    obs_data_set_default_double(s,"gain_db",0);obs_data_set_default_double(s,"comb_depth",1);
    obs_data_set_default_double(s,"mix",1);obs_data_set_default_double(s,"input_db",0);obs_data_set_default_double(s,"output_db",0);
    obs_data_set_default_int(s,"mod_source",0);obs_data_set_default_int(s,"mod_target",0);
    obs_data_set_default_int(s,"wave_shape",0);obs_data_set_default_double(s,"lfo_rate",1);
    obs_data_set_default_double(s,"mod_depth",0);obs_data_set_default_double(s,"attack_ms",10);
    obs_data_set_default_double(s,"release_ms",100);
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<NativeSource*>(ptr);if(!f)return;
    auto settings=read(s);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    std::lock_guard lock(f->mutex);
    if(f->rate!=ai.samples_per_sec){f->rate=ai.samples_per_sec;f->filter.reset(f->rate,settings);}
    else f->filter.set(settings);
}
void* create(obs_data_t* s,obs_source_t*){
    auto f=std::make_unique<NativeSource>();update(f.get(),s);return f.release();
}
void destroy(void* ptr){delete static_cast<NativeSource*>(ptr);}
const char* name(void*){return "HyperDSP Filter";}
bool visibility(obs_properties_t* p,obs_property_t*,obs_data_t* s){
    const auto mode=obs_data_get_int(s,"mode");
    const auto source=obs_data_get_int(s,"mod_source");
    auto show=[&](const char* k,bool yes){if(auto* prop=obs_properties_get(p,k))obs_property_set_visible(prop,yes);};
    const bool cascade=mode==1||mode==2;
    const bool butterworth=cascade&&obs_data_get_int(s,"alignment")==1;
    show("q",!butterworth&&((mode>=1&&mode<=5)||mode==9));
    show("alignment",cascade);
    show("stages",mode==1||mode==2);
    show("gain_db",mode>=5&&mode<=7);show("comb_depth",mode==8);
    show("frequency",mode!=0);
    show("mod_target",source!=0);show("mod_depth",source!=0);
    show("wave_shape",source==1);show("lfo_rate",source==1);
    show("attack_ms",source==2);show("release_ms",source==2);
    return true;
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto list=[&](const char* key,const char* label,std::initializer_list<const char*> names){
        auto* prop=obs_properties_add_list(p,key,label,OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        int i=0;for(auto n:names)obs_property_list_add_int(prop,n,i++);
        return prop;
    };
    auto slider=[&](const char* key,const char* label,double lo,double hi,double step,const char* suffix=nullptr){
        auto* prop=obs_properties_add_float_slider(p,key,label,lo,hi,step);
        if(suffix)obs_property_float_set_suffix(prop,suffix);
        return prop;
    };
    auto* mode=list("mode","Filter type",{"Bypass","Low-pass","High-pass","Band-pass","Notch","Peaking EQ","Low shelf","High shelf","Comb (feedforward notch)","All-pass"});
    auto* freq=slider("frequency","Frequency",20,20000,1," Hz");
    obs_property_set_long_description(freq,"Cutoff or center frequency; in comb mode, spacing between notches. Limited to 45% of sample rate.");
    slider("q","Resonance (Q)",.25,12,.01);
    auto* stages=obs_properties_add_list(p,"stages","Slope",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(int i=1;i<=4;++i){auto text=std::to_string(i*12)+" dB/oct";obs_property_list_add_int(stages,text.c_str(),i);}
    auto* alignment=list("alignment","Response",{"Resonant cascade","Butterworth (flat passband)"});
    obs_property_set_long_description(alignment,"Butterworth keeps the cutoff at -3.01 dB for every slope, with fixed per-stage Q. Resonant cascade uses the adjustable Q for each section.");
    obs_property_set_long_description(stages,"Choose slope in either alignment.");
    obs_property_set_modified_callback(alignment,visibility);
    slider("gain_db","EQ gain",-24,24,.1," dB");
    slider("comb_depth","Comb depth",0,1,.01);
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
    auto* source=list("mod_source","Modulation source",{"Off","LFO","Input envelope"});
    list("mod_target","Modulation target",{"Cutoff","Resonance (Q)","EQ gain","Wet / dry","Output amplitude"});
    list("wave_shape","Waveform",{"Sine","Triangle","Square","Saw"});
    slider("lfo_rate","LFO rate",.05,20,.05," Hz");
    auto* depth=slider("mod_depth","Modulation depth",0,1,.01);
    obs_property_set_long_description(depth,"At full depth: cutoff ±4 octaves, Q ±2 octaves (not in Butterworth), EQ gain ±12 dB, wet/dry ±1, or output amplitude ×(1 + modulation). Envelope is positive-only.");
    slider("attack_ms","Envelope attack",1,200,1," ms");
    slider("release_ms","Envelope release",10,2000,10," ms");
    obs_property_set_modified_callback(mode,visibility);
    obs_property_set_modified_callback(source,visibility);
    return p;
}
obs_audio_data* process(void* ptr,obs_audio_data* audio){
    auto* f=static_cast<NativeSource*>(ptr);
    if(!f||!audio)return audio;
    std::unique_lock lock(f->mutex,std::try_to_lock);
    if(!lock.owns_lock())return audio;
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||f->rate<8000||f->rate>192000)return audio;
    const auto ch=get_audio_channels(ai.speakers);
    if(ch>MAX_AUDIO_CHANNELS)return audio;
    std::array<float*,MAX_AUDIO_CHANNELS> buffers{};
    for(unsigned i=0;i<ch;++i)buffers[i]=reinterpret_cast<float*>(audio->data[i]);
    f->filter.process(buffers.data(),ch,audio->frames);
    return audio;
}
}
extern "C" {
struct obs_source_info native_filter={
    .id="hyperdsp_native_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process,
};
}
