#include "effect-library.hpp"
#include <obs-module.h>
#include "framework/dsp/formant.hpp"
#include "framework/obs/route_properties.hpp"
#include <memory>
#include <mutex>
namespace {
using namespace hyperdsp;
struct Source {
    std::mutex mutex;
    Formant engine;
    ExternalInputs external;
    unsigned rate=0,channels=0;
};
FormantSettings read(obs_data_t* s){
    FormantSettings f;
    auto number=[&](const char* k){return float(obs_data_get_double(s,k));};
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=2?FormantMode(mode):FormantMode::Bypass;
    f.from=unsigned(std::clamp(obs_data_get_int(s,"from"),0LL,4LL));
    f.to=unsigned(std::clamp(obs_data_get_int(s,"to"),0LL,4LL));
    f.morph=number("morph");f.shift=number("shift");f.width=number("width");
    for(unsigned j=0;j<3;++j){
        const auto suffix=std::to_string(j+1);
        f.frequency[j]=number(("f"+suffix).c_str());
        f.bandwidth[j]=number(("b"+suffix).c_str());
        f.gain[j]=number(("g"+suffix).c_str());
    }
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,10);
    return f;
}
void defaults(obs_data_t* s){
    FormantSettings f;
    obs_data_set_default_int(s,"mode",1);
    auto set=[&](const char* k,double v){obs_data_set_default_double(s,k,v);};
    obs_data_set_default_int(s,"from",f.from);obs_data_set_default_int(s,"to",f.to);
    set("morph",f.morph);set("shift",f.shift);set("width",f.width);
    for(unsigned j=0;j<3;++j){
        const auto suffix=std::to_string(j+1);
        set(("f"+suffix).c_str(),f.frequency[j]);set(("b"+suffix).c_str(),f.bandwidth[j]);set(("g"+suffix).c_str(),f.gain[j]);
    }
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
        f->rate=ai.samples_per_sec;f->channels=channels;f->engine.reset(f->rate,settings);
    }else f->engine.set(settings);
}
void* create(obs_data_t* s,obs_source_t*){auto f=std::make_unique<Source>();update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Formant";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    for(auto k:{"from","to","morph"})show(k,mode==1);
    for(auto k:{"f1","f2","f3","b1","b2","b3","g1","g2","g3"})show(k,mode==2);
    show("shift",mode!=0);show("width",mode!=0);
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Formant effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::formant_modes);
    auto slider=[&](const char* k,const char* n,double lo,double hi,double step,const char* unit=nullptr){
        auto* prop=obs_properties_add_float_slider(p,k,n,lo,hi,step);if(unit)obs_property_float_set_suffix(prop,unit);return prop;
    };
    const char* vowels[]={"A (ah)","E (eh)","I (ee)","O (oh)","U (oo)"};
    for(auto key:{"from","to"}){
        auto* p_vowel=obs_properties_add_list(p,key,key==std::string("from")?"From vowel":"To vowel",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        for(int j=0;j<5;++j)obs_property_list_add_int(p_vowel,vowels[j],j);
    }
    slider("morph","Vowel morph",0,1,.01);
    auto* shift=slider("shift","Resonance shift",-24,24,.1," semitones");
    obs_property_set_long_description(shift,"Scales the resonator center frequencies and bandwidths; does not transpose the input pitch.");
    slider("width","Bandwidth scale",.25,4,.01," ×");
    for(unsigned j=0;j<3;++j){
        auto suffix=std::to_string(j+1),prefix="Formant "+suffix+" · ";
        slider(("f"+suffix).c_str(),(prefix+"Frequency").c_str(),40,16000,1," Hz");
        slider(("b"+suffix).c_str(),(prefix+"Bandwidth").c_str(),20,4000,1," Hz");
        slider(("g"+suffix).c_str(),(prefix+"Gain").c_str(),-36,6,.1," dB");
    }
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
    obs_routes::add(p,{"Vowel morph","Resonance shift (semitones)","Bandwidth scale","Wet / dry","Output amplitude","F1 offset (Hz)","F2 offset (Hz)","F3 offset (Hz)","F1 gain (dB)","F2 gain (dB)","F3 gain (dB)"},
        "Signed amount in destination units. Morph is bounded to 0–1, and resonance offsets/gains also apply to vowel presets. Frequency and Q remain bounded at the actual sample rate.");
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
struct obs_source_info formant_filter={
    .id="hyperdsp_formant_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
