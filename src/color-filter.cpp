#include <obs-module.h>
#include "framework/dsp/color.hpp"
#include "framework/obs/route_properties.hpp"
#include <memory>
#include <mutex>
namespace {
using namespace hyperdsp;
struct Source {
    std::mutex mutex;
    Color engine;
    ExternalInputs external;
    unsigned rate=0,channels=0;
};
ColorSettings read(obs_data_t* s){
    ColorSettings f;
    auto number=[&](const char* k){return float(obs_data_get_double(s,k));};
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=5?ColorMode(mode):ColorMode::Bypass;
    f.drive=number("drive");f.bits=number("bits");f.reduce=number("reduce");
    auto os=obs_data_get_int(s,"oversample");f.oversample=os==2||os==4?unsigned(os):1;
    f.wet=number("mix");f.input_db=number("input_db");f.output_db=number("output_db");
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,4);
    return f;
}
void defaults(obs_data_t* s){
    ColorSettings f;
    obs_data_set_default_int(s,"mode",1);
    auto set=[&](const char* k,double v){obs_data_set_default_double(s,k,v);};
    set("drive",f.drive);set("bits",f.bits);set("reduce",f.reduce);
    obs_data_set_default_int(s,"oversample",1);
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
const char* name(void*){return "HyperDSP Color";}
bool visibility(obs_properties_t* p,obs_property_t* prop,obs_data_t* s){
    auto mode=obs_data_get_int(s,"mode");
    auto show=[&](const char* key,bool visible){obs_property_set_visible(obs_properties_get(p,key),visible);};
    show("drive",mode!=0);show("bits",mode==5);show("reduce",mode==5);
    show("oversample",mode>=1&&mode<=4);
    return obs_routes::visibility(p,prop,s);
}
obs_properties_t* properties(void*){
    auto* p=obs_properties_create();
    auto* mode=obs_properties_add_list(p,"mode","Color effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    const char* names[]={"Bypass","Tanh saturation","Cubic soft clipping","Hard clipping","Wavefolding","Bitcrusher / sample hold"};
    for(int i=0;i<6;++i)obs_property_list_add_int(mode,names[i],i);
    auto slider=[&](const char* k,const char* n,double lo,double hi,double step,const char* unit=nullptr){
        auto* prop=obs_properties_add_float_slider(p,k,n,lo,hi,step);if(unit)obs_property_float_set_suffix(prop,unit);return prop;
    };
    slider("drive","Drive",-24,36,.1," dB");
    slider("bits","Bit depth",2,16,1," bits");
    slider("reduce","Sample hold",1,64,1," samples");
    auto* oversample=obs_properties_add_list(p,"oversample","Oversampling",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(oversample,"Off (1×)",1);
    obs_property_list_add_int(oversample,"2×",2);
    obs_property_list_add_int(oversample,"4×",4);
    obs_property_set_long_description(oversample,"Filtered interpolation and decimation, 24 samples of latency with aligned dry branch. Not compensated across OBS sources. Disabled for bitcrusher.");
    slider("input_db","Input gain",-24,24,.1," dB");
    slider("mix","Wet / dry",0,1,.01);
    slider("output_db","Output gain",-24,24,.1," dB");
    obs_routes::add(p,{"Drive (dB)","Bit depth","Sample hold (samples)","Wet / dry","Output amplitude"},
        "Signed amount in destination units. Drive ±60 dB, bit depth ±14, sample hold ±63, mix/output ±1 per route. Summed before bounds.");
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
struct obs_source_info color_filter={
    .id="hyperdsp_color_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
