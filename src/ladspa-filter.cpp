#include <obs-module.h>
#include <ladspa.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr unsigned max_ports=256;
constexpr unsigned block_size=1024;
struct Control {
    unsigned port;
    std::string name;
    float min,max,initial;
    bool integer,toggle;
};
std::string key(unsigned port) { return "port_"+std::to_string(port); }
Control control(const LADSPA_Descriptor* d,unsigned p,unsigned rate) {
    auto h=d->PortRangeHints[p];
    auto f=h.HintDescriptor;
    const float scale=LADSPA_IS_HINT_SAMPLE_RATE(f)?float(rate):1.f;
    float lo=LADSPA_IS_HINT_BOUNDED_BELOW(f)?h.LowerBound*scale:-10000.f;
    float hi=LADSPA_IS_HINT_BOUNDED_ABOVE(f)?h.UpperBound*scale:10000.f;
    if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi) {lo=0;hi=1;}
    float initial=std::clamp(0.f,lo,hi);
    auto interp=[&](float t) {
        return LADSPA_IS_HINT_LOGARITHMIC(f)&&lo>0&&hi>0
            ? std::exp(std::log(lo)*(1-t)+std::log(hi)*t):lo*(1-t)+hi*t;
    };
    switch(f & LADSPA_HINT_DEFAULT_MASK) {
        case LADSPA_HINT_DEFAULT_MINIMUM: initial=lo;break;
        case LADSPA_HINT_DEFAULT_LOW: initial=interp(.25f);break;
        case LADSPA_HINT_DEFAULT_MIDDLE: initial=interp(.5f);break;
        case LADSPA_HINT_DEFAULT_HIGH: initial=interp(.75f);break;
        case LADSPA_HINT_DEFAULT_MAXIMUM: initial=hi;break;
        case LADSPA_HINT_DEFAULT_0: initial=0;break;
        case LADSPA_HINT_DEFAULT_1: initial=1;break;
        case LADSPA_HINT_DEFAULT_100: initial=100;break;
        case LADSPA_HINT_DEFAULT_440: initial=440;break;
    }
    bool toggle=LADSPA_IS_HINT_TOGGLED(f), integer=LADSPA_IS_HINT_INTEGER(f);
    if(toggle){lo=0;hi=1;initial=initial>0?1:0;}
    return {p,d->PortNames[p],lo,hi,std::clamp(initial,lo,hi),integer,toggle};
}
struct Instance {
    void* library=nullptr;
    const LADSPA_Descriptor* descriptor=nullptr;
    struct Voice {
        LADSPA_Handle handle=nullptr;
        std::array<float,max_ports> values{};
        std::array<std::array<float,block_size>,2> input{},scratch{};
        bool activated=false;
    };
    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<unsigned> in,out;
    std::vector<Control> controls;
    unsigned rate=0, channels=0;
    std::string path,label;
    ~Instance() {
        if(descriptor)for(auto& v:voices)if(v->handle){
            if(v->activated&&descriptor->deactivate)descriptor->deactivate(v->handle);
            descriptor->cleanup(v->handle);
        }
        if(library)dlclose(library);
    }
    void parameters(obs_data_t* settings) {
        for(auto& c:controls){
            auto k=key(c.port);
            if(c.toggle)obs_data_set_default_bool(settings,k.c_str(),c.initial!=0);
            else obs_data_set_default_double(settings,k.c_str(),c.initial);
            double raw=obs_data_has_user_value(settings,k.c_str())?
                (c.toggle?double(obs_data_get_bool(settings,k.c_str())):obs_data_get_double(settings,k.c_str())):c.initial;
            float value=std::isfinite(raw)?float(std::clamp(raw,double(c.min),double(c.max))):c.initial;
            if(c.integer||c.toggle)value=std::clamp(std::round(value),c.min,c.max);
            for(auto& v:voices)v->values[c.port]=value;
        }
    }
    bool open(obs_data_t* settings,unsigned sr,unsigned ch) {
        path=obs_data_get_string(settings,"library");
        label=obs_data_get_string(settings,"label");
        rate=sr;channels=ch;
        if(path.empty()||path.front()!='/'||label.empty()||ch==0||ch>MAX_AUDIO_CHANNELS)return false;
        library=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
        if(!library)return false;
        auto entry=reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(library,"ladspa_descriptor"));
        if(!entry)return false;
        for(unsigned i=0;i<1024;++i){
            auto* d=entry(i);if(!d)break;
            if(d->Label&&label==d->Label){descriptor=d;break;}
        }
        auto* d=descriptor;
        if(!d||!LADSPA_IS_HARD_RT_CAPABLE(d->Properties)||!d->instantiate||!d->connect_port||
           !d->run||!d->cleanup||d->PortCount>max_ports||!d->PortDescriptors||!d->PortNames||!d->PortRangeHints)return false;
        for(unsigned p=0;p<d->PortCount;++p){
            auto type=d->PortDescriptors[p];
            if(LADSPA_IS_PORT_AUDIO(type)){
                if(LADSPA_IS_PORT_INPUT(type))in.push_back(p);
                else if(LADSPA_IS_PORT_OUTPUT(type))out.push_back(p);
                else return false;
            }else if(LADSPA_IS_PORT_CONTROL(type)){
                if(LADSPA_IS_PORT_INPUT(type))controls.push_back(control(d,p,sr));
            }else return false;
        }
        // One mono instance per OBS channel; stereo only for stereo layouts.
        if(in.size()!=out.size()||(in.size()!=1&&(in.size()!=2||ch!=2)))return false;
        unsigned count=in.size()==1?ch:1;
        for(unsigned i=0;i<count;++i){
            auto v=std::make_unique<Voice>();
            v->handle=d->instantiate(d,sr);
            if(!v->handle)return false;
            for(unsigned p=0;p<d->PortCount;++p)
                if(LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p]))d->connect_port(v->handle,p,&v->values[p]);
            // Provide valid audio buffers even during activate().
            for(unsigned c=0;c<in.size();++c){
                d->connect_port(v->handle,in[c],v->input[c].data());
                d->connect_port(v->handle,out[c],v->scratch[c].data());
            }
            voices.push_back(std::move(v));
        }
        parameters(settings);
        for(auto& v:voices){if(d->activate)d->activate(v->handle);v->activated=true;}
        return true;
    }
    void process(obs_audio_data* audio) {
        for(unsigned c=0;c<channels;++c)if(!audio->data[c])return;
        for(unsigned off=0;off<audio->frames;){
            unsigned n=std::min(block_size,audio->frames-off);
            for(unsigned i=0;i<voices.size();++i){
                auto& v=*voices[i];
                for(unsigned c=0;c<in.size();++c){
                    auto* samples=reinterpret_cast<float*>(audio->data[i*in.size()+c])+off;
                    std::copy_n(samples,n,v.input[c].data());
                }
                descriptor->run(v.handle,n);
                for(unsigned c=0;c<out.size();++c){
                    auto* samples=reinterpret_cast<float*>(audio->data[i*in.size()+c])+off;
                    for(unsigned j=0;j<n;++j)if(std::isfinite(v.scratch[c][j]))samples[j]=v.scratch[c][j];
                }
            }
            off+=n;
        }
    }
};
struct Filter {std::mutex mutex;std::unique_ptr<Instance> active;};
bool format(unsigned& rate,unsigned& channels){
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai))return false;
    rate=ai.samples_per_sec;channels=get_audio_channels(ai.speakers);
    return rate&&channels;
}
const char* name(void*){return "HyperDSP LADSPA";}
void update(void* ptr,obs_data_t* settings){
    auto* f=static_cast<Filter*>(ptr);if(!f)return;
    unsigned rate=0,ch=0;
    if(!format(rate,ch))return;
    {
        std::lock_guard lock(f->mutex);
        if(f->active&&f->active->path==obs_data_get_string(settings,"library")&&
           f->active->label==obs_data_get_string(settings,"label")&&f->active->rate==rate&&f->active->channels==ch){
            f->active->parameters(settings);return;
        }
    }
    // Load, instantiate and destroy native code off the audio callback.
    auto next=std::make_unique<Instance>();
    if(!next->open(settings,rate,ch))next.reset();
    {std::lock_guard lock(f->mutex);f->active.swap(next);}
}
void* create(obs_data_t* s,obs_source_t*){auto f=std::make_unique<Filter>();update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Filter*>(p);}
void add_controls(obs_properties_t* props,const std::vector<Control>& controls){
    for(auto& c:controls){
        auto k=key(c.port);
        if(c.toggle)obs_properties_add_bool(props,k.c_str(),c.name.c_str());
        else obs_properties_add_float(props,k.c_str(),c.name.c_str(),c.min,c.max,c.integer?1.0:0.001);
    }
}
bool modified(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    for(unsigned p=0;p<max_ports;++p)obs_properties_remove_by_name(props,key(p).c_str());
    unsigned rate=0,ch=0;Instance inspect;
    if(format(rate,ch)&&inspect.open(settings,rate,ch)){
        for(auto& c:inspect.controls){
            auto k=key(c.port);
            if(c.toggle)obs_data_set_default_bool(settings,k.c_str(),c.initial!=0);
            else obs_data_set_default_double(settings,k.c_str(),c.initial);
        }
        add_controls(props,inspect.controls);
    }
    return true;
}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* path=obs_properties_add_path(props,"library","Trusted LADSPA library",OBS_PATH_FILE,"Shared library (*.so)",nullptr);
    obs_property_set_long_description(path,"Native code executes inside OBS. Only choose plugins you trust.");
    auto* label=obs_properties_add_text(props,"label","Plugin label",OBS_TEXT_DEFAULT);
    obs_property_set_modified_callback(path,modified);
    obs_property_set_modified_callback(label,modified);
    if(p){auto* f=static_cast<Filter*>(p);std::lock_guard lock(f->mutex);
        if(f->active)add_controls(props,f->active->controls);}
    return props;
}
obs_audio_data* process(void* p,obs_audio_data* audio){
    auto* f=static_cast<Filter*>(p);if(!f||!audio)return audio;
    std::unique_lock lock(f->mutex,std::try_to_lock);
    unsigned rate=0,ch=0;
    if(lock.owns_lock()&&f->active&&format(rate,ch)&&rate==f->active->rate&&ch==f->active->channels)f->active->process(audio);
    return audio;
}
}
extern "C" {
struct obs_source_info ladspa_filter={
    .id="hyperdsp_ladspa_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_properties=properties,.update=update,.filter_audio=process,
};
}
