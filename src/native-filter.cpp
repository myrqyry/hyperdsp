#include "effect-library.hpp"
#include <obs-module.h>
#include "framework/dsp/native_filter.hpp"
#include "interop/external_inputs.hpp"
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
    ExternalInputs external;
    unsigned rate=0;
};
std::string extra_key(unsigned slot,const char* field){
    return "mod"+std::to_string(slot+1)+"_"+field;
}
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
    f.source=ModSource(enumeration("mod_source",10,0));f.target=ModTarget(enumeration("mod_target",4,0));
    f.shape=WaveShape(enumeration("wave_shape",3,0));
    f.hz=number("lfo_rate");f.depth=number("mod_depth");
    f.attack_ms=number("attack_ms");f.release_ms=number("release_ms");f.band_sensitivity=number("band_sensitivity");
    for(unsigned slot=1;slot<modulation_slots;++slot){
        auto key=[&](const char* field){return extra_key(slot,field);};
        f.extra_routes[slot-1]={enumeration(key("source").c_str(),10,0),enumeration(key("target").c_str(),4,0),
            enumeration(key("shape").c_str(),3,0),number(key("rate").c_str()),number(key("depth").c_str()),
            number(key("attack").c_str()),number(key("release").c_str()),number(key("sensitivity").c_str())};
    }
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
    obs_data_set_default_double(s,"release_ms",100);obs_data_set_default_double(s,"band_sensitivity",0);
    for(unsigned slot=1;slot<modulation_slots;++slot){
        auto key=[&](const char* field){return extra_key(slot,field);};
        for(auto field:{"source","target","shape"})obs_data_set_default_int(s,key(field).c_str(),0);
        obs_data_set_default_double(s,key("rate").c_str(),1);
        obs_data_set_default_double(s,key("depth").c_str(),0);
        obs_data_set_default_double(s,key("attack").c_str(),10);
        obs_data_set_default_double(s,key("release").c_str(),100);
        obs_data_set_default_double(s,key("sensitivity").c_str(),0);
    }
}
void update(void* ptr,obs_data_t* s){
    auto* f=static_cast<NativeSource*>(ptr);if(!f)return;
    auto settings=read(s);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    std::lock_guard lock(f->mutex);
    for(unsigned slot=0;slot<modulation_slots;++slot){
        const int source=slot?settings.extra_routes[slot-1].source:int(settings.source);
        const char* channel_field=source>=7?"sight_channel":"sound_channel";
        auto channel=slot?extra_key(slot,channel_field):channel_field;
        auto category=slot?extra_key(slot,"sound_category"):"sound_category";
        f->external.configure(slot,source,
            obs_data_get_string(s,channel.c_str()),obs_data_get_string(s,category.c_str()));
    }
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
    show("attack_ms",source>=2&&source<=5);show("release_ms",source>=2&&source<=5);show("band_sensitivity",source>=3&&source<=5);
    show("sound_channel",source==6);show("sound_category",source==6);
    show("sight_channel",source>=7);
    if(source>=7)sight_channel_choices(obs_properties_get(p,"sight_channel"),obs_data_get_string(s,"sight_channel"),source);
    if(source==6)sound_channel_choices(obs_properties_get(p,"sound_channel"),obs_data_get_string(s,"sound_channel"));
    for(unsigned slot=1;slot<modulation_slots;++slot){
        auto key=[&](const char* field){return extra_key(slot,field);};
        const auto src=obs_data_get_int(s,key("source").c_str());
        show(key("target").c_str(),src!=0);show(key("depth").c_str(),src!=0);
        show(key("shape").c_str(),src==1);show(key("rate").c_str(),src==1);
        show(key("attack").c_str(),src>=2&&src<=5);show(key("release").c_str(),src>=2&&src<=5);
        show(key("sensitivity").c_str(),src>=3&&src<=5);
        show(key("sound_channel").c_str(),src==6);show(key("sound_category").c_str(),src==6);
        show(key("sight_channel").c_str(),src>=7);
        if(src==6)sound_channel_choices(obs_properties_get(p,key("sound_channel").c_str()),obs_data_get_string(s,key("sound_channel").c_str()));
        if(src>=7)sight_channel_choices(obs_properties_get(p,key("sight_channel").c_str()),obs_data_get_string(s,key("sight_channel").c_str()),src);
    }
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
    auto* mode=obs_properties_add_list(p,"mode","Filter type",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(mode,hyperdsp::filter_modes);
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
    auto* source=list("mod_source","Modulation source",{"Off","LFO","Input envelope","Bass energy (<250 Hz)","Mid energy (250–2500 Hz)","Treble energy (>2500 Hz)","Sound category confidence","Sight motion energy","Sight hand X","Sight hand Y","Sight pinch closeness"});
    list("mod_target","Modulation target",{"Cutoff","Resonance (Q)","EQ gain","Wet / dry","Output amplitude"});
    list("wave_shape","Waveform",{"Sine","Triangle","Square","Saw"});
    slider("lfo_rate","LFO rate",.05,20,.05," Hz");
    auto* depth=slider("mod_depth","Modulation depth",-1,1,.01);
    obs_property_set_long_description(depth,"At full depth: cutoff ±4 octaves, Q ±2 octaves (not in Butterworth), EQ gain ±12 dB, wet/dry ±1, or output amplitude ×(1 + modulation). Envelope is positive-only.");
    slider("attack_ms","Envelope attack",1,200,1," ms");
    slider("release_ms","Envelope release",10,2000,10," ms");
    slider("band_sensitivity","Band sensitivity",-24,24,.1," dB");
    auto* channel=obs_properties_add_list(p,"sound_channel","Sound classifier",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(channel,"Choose Sound classifier","");
    obs_properties_add_text(p,"sound_category","Sound category (exact label)",OBS_TEXT_DEFAULT);
    obs_properties_add_list(p,"sight_channel","Sight input",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    for(unsigned slot=1;slot<modulation_slots;++slot){
        auto key=[&](const char* field){return extra_key(slot,field);};
        const auto prefix="Route "+std::to_string(slot+1)+" · ";
        auto* src=list(key("source").c_str(),(prefix+"Source").c_str(),{"Off","LFO","Input envelope","Bass energy","Mid energy","Treble energy","Sound category confidence","Sight motion energy","Sight hand X","Sight hand Y","Sight pinch closeness"});
        list(key("target").c_str(),(prefix+"Destination").c_str(),{"Cutoff","Resonance (Q)","EQ gain","Wet / dry","Output amplitude"});
        list(key("shape").c_str(),(prefix+"Waveform").c_str(),{"Sine","Triangle","Square","Saw"});
        slider(key("rate").c_str(),(prefix+"Rate").c_str(),.05,20,.05," Hz");
        slider(key("depth").c_str(),(prefix+"Depth").c_str(),-1,1,.01);
        slider(key("attack").c_str(),(prefix+"Attack").c_str(),1,200,1," ms");
        slider(key("release").c_str(),(prefix+"Release").c_str(),10,2000,10," ms");
        slider(key("sensitivity").c_str(),(prefix+"Sensitivity").c_str(),-24,24,.1," dB");
        auto* ch=obs_properties_add_list(p,key("sound_channel").c_str(),(prefix+"Sound classifier").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(ch,"Choose Sound classifier","");
        obs_properties_add_text(p,key("sound_category").c_str(),(prefix+"Sound category (exact label)").c_str(),OBS_TEXT_DEFAULT);
        obs_properties_add_list(p,key("sight_channel").c_str(),(prefix+"Sight input").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        obs_property_set_modified_callback(src,visibility);
    }
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
    f->filter.external_values(f->external.snapshot());
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
