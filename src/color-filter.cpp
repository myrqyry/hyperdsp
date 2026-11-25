#include "effect-library.hpp"
#include <obs-module.h>
#include "framework/dsp/color.hpp"
#include "framework/obs/route_properties.hpp"
#include "framework/obs/parameter_properties.hpp"
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
    auto mode=obs_data_get_int(s,"mode");f.mode=mode>=0&&mode<=5?ColorMode(mode):ColorMode::Bypass;
    obs_parameters::read(s,f,color_parameters);
    auto os=obs_data_get_int(s,"oversample");f.oversample=valid_oversampling(os)?unsigned(os):1;
    for(unsigned i=0;i<modulation_slots;++i)f.routes[i]=obs_routes::read(s,i,4);
    return f;
}
void defaults(obs_data_t* s){
    ColorSettings f;
    obs_data_set_default_int(s,"mode",1);
    obs_parameters::defaults(s,f,color_parameters);
    obs_data_set_default_int(s,"oversample",f.oversample);
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
    hyperdsp::add_native_modes(mode,hyperdsp::color_modes);
    for(unsigned i=0;i<3;++i)obs_parameters::add(p,color_parameters[i]);
    auto* oversample=obs_properties_add_list(p,"oversample","Oversampling",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    for(auto factor:oversampling_choices){
        const auto label=factor==1?std::string("Off (1×)"):std::to_string(factor)+"×";
        obs_property_list_add_int(oversample,label.c_str(),factor);
    }
    obs_property_set_long_description(oversample,"Filtered interpolation and decimation, 24 samples of latency with aligned dry branch. Not compensated across OBS sources. Disabled for bitcrusher.");
    for(unsigned i=3;i<std::size(color_parameters);++i)obs_parameters::add(p,color_parameters[i]);
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
