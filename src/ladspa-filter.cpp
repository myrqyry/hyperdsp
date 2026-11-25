#include <obs-module.h>
#include "ladspa-catalog.hpp"
#include "framework/dsp/modulation.hpp"
#include "framework/dsp/modulation_matrix.hpp"
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
constexpr unsigned route_count=hyperdsp::modulation_slots;
std::string route_key(unsigned slot,const char* field){
    return (slot==0?std::string("react_"):"react"+std::to_string(slot+1)+"_")+field;
}
struct Control {
    unsigned port;
    std::string name;
    float min,max,initial;
    bool integer,toggle;
};
std::string key(unsigned port) { return "port_"+std::to_string(port); }
std::string target_id(const std::string& path,const std::string& label,unsigned port){
    return "port:"+path+"\t"+label+"\t"+std::to_string(port);
}
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
    hyperdsp::MixModulator modulation;
    std::array<hyperdsp::MixSettings,block_size> mix_frames{};
    bool initialized=false;
    struct Route {
        int source=0,port=-1;
        bool mix=false,output=false;
        float min=0,max=0;
        std::string saved_target;
    };
    std::array<Route,route_count> routes;
    hyperdsp::ModulationMatrix matrix;
    bool any_route=false,any_port=false;
    std::array<float,max_ports> base_values{};
    std::array<float,MAX_AUDIO_CHANNELS> detector_input{};
    std::string path,label;
    ~Instance() {
        if(descriptor)for(auto& v:voices)if(v->handle){
            if(v->activated&&descriptor->deactivate)descriptor->deactivate(v->handle);
            descriptor->cleanup(v->handle);
        }
        if(library)dlclose(library);
    }
    void parameters(obs_data_t* settings) {
        auto mix=hyperdsp::MixSettings::from(obs_data_get_double(settings,"mix"),
            obs_data_get_double(settings,"input_db"),obs_data_get_double(settings,"output_db"));
        const auto target=obs_data_get_int(settings,"lfo_target");
        hyperdsp::LfoSettings lfo{target>=0&&target<=2?int(target):0,
            obs_data_get_double(settings,"lfo_rate"),obs_data_get_double(settings,"lfo_depth")};
        const bool first=!initialized;
        if(initialized)modulation.set(mix,lfo);
        else {modulation.reset(rate,mix,lfo);initialized=true;}
        any_route=any_port=false;
        for(unsigned slot=0;slot<route_count;++slot){
            auto& r=routes[slot];
            auto integer=[&](const char* key){return obs_data_get_int(settings,route_key(slot,key).c_str());};
            auto number=[&](const char* key){return obs_data_get_double(settings,route_key(slot,key).c_str());};
            const auto source=integer("source"),shape=integer("shape");
            const std::string target=obs_data_get_string(settings,route_key(slot,"target").c_str());
            r.saved_target=target;
            r.source=source>=0&&source<=5?int(source):0;
            r.port=-1;r.mix=target=="mix";r.output=target=="output";
            for(const auto& c:controls)
                if(!c.integer&&!c.toggle&&c.max>c.min&&target==target_id(path,label,c.port)){
                    r.port=int(c.port);r.min=c.min;r.max=c.max;
                }
            if(r.port<0&&!r.mix&&!r.output)r.source=0;
            const double limit=r.port>=0?10000.:1.;
            const float amount=hyperdsp::MixSettings::bounded(number("amount"),-limit,limit,0);
            const int destination=r.port>=0?r.port+2:r.mix?0:r.output?1:-1;
            matrix.configure(slot,rate,{r.source,destination,shape>=0&&shape<=3?int(shape):0,
                number("rate"),amount,number("attack"),number("release"),number("sensitivity")},first);
            any_route=any_route||r.source!=0;
            any_port=any_port||(r.source!=0&&r.port>=0);
        }
        for(auto& c:controls){
            auto k=key(c.port);
            if(c.toggle)obs_data_set_default_bool(settings,k.c_str(),c.initial!=0);
            else obs_data_set_default_double(settings,k.c_str(),c.initial);
            double raw=obs_data_has_user_value(settings,k.c_str())?
                (c.toggle?double(obs_data_get_bool(settings,k.c_str())):obs_data_get_double(settings,k.c_str())):c.initial;
            float value=std::isfinite(raw)?float(std::clamp(raw,double(c.min),double(c.max))):c.initial;
            if(c.integer||c.toggle)value=std::clamp(std::round(value),c.min,c.max);
            base_values[c.port]=value;
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
            unsigned n=std::min(any_port?64u:block_size,audio->frames-off);
            std::array<float,route_count> port_sums{};
            // All routes see the same pre-trim input and advance once per sample.
            // Sum contributions to each destination before applying its bounds.
            for(unsigned j=0;j<n;++j){
                auto mix=modulation.next();
                if(any_route){
                    for(unsigned c=0;c<channels;++c)detector_input[c]=reinterpret_cast<float*>(audio->data[c])[off+j];
                    const auto frame=matrix.next(detector_input.data(),channels);
                    for(unsigned slot=0;slot<route_count;++slot)
                        if(frame.entries[slot].destination>=2)port_sums[slot]+=frame.entries[slot].value;
                    mix.wet=std::clamp(mix.wet+frame.sum(0),0.f,1.f);
                    mix.output_gain*=std::max(0.f,1.f+frame.sum(1));
                }
                mix_frames[j]=mix;
            }
            if(any_port)for(unsigned slot=0;slot<route_count;++slot){
                const auto& r=routes[slot];
                if(!r.source||r.port<0)continue;
                bool seen=false;
                for(unsigned k=0;k<slot;++k)if(routes[k].source&&routes[k].port==r.port)seen=true;
                if(seen)continue;
                float sum=port_sums[slot];
                for(unsigned k=slot+1;k<route_count;++k)
                    if(routes[k].source&&routes[k].port==r.port)sum+=port_sums[k];
                const float value=std::clamp(base_values[r.port]+sum/float(n),r.min,r.max);
                for(auto& v:voices)v->values[r.port]=value;
            }
            for(unsigned i=0;i<voices.size();++i){
                auto& v=*voices[i];
                for(unsigned c=0;c<in.size();++c){
                    auto* samples=reinterpret_cast<float*>(audio->data[i*in.size()+c])+off;
                    for(unsigned j=0;j<n;++j)v.input[c][j]=mix_frames[j].input(samples[j]);
                }
                descriptor->run(v.handle,n);
                for(unsigned c=0;c<out.size();++c){
                    auto* samples=reinterpret_cast<float*>(audio->data[i*in.size()+c])+off;
                    for(unsigned j=0;j<n;++j)samples[j]=mix_frames[j].output(samples[j],mix_frames[j].input(samples[j]),v.scratch[c][j]);
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
void defaults(obs_data_t* settings){
    obs_data_set_default_double(settings,"mix",1.);
    obs_data_set_default_double(settings,"input_db",0.);
    obs_data_set_default_double(settings,"output_db",0.);
    obs_data_set_default_int(settings,"lfo_target",0);
    obs_data_set_default_double(settings,"lfo_rate",1.);
    obs_data_set_default_double(settings,"lfo_depth",0.);
    for(unsigned slot=0;slot<route_count;++slot){
        auto integer=[&](const char* field,int value){obs_data_set_default_int(settings,route_key(slot,field).c_str(),value);};
        auto number=[&](const char* field,double value){obs_data_set_default_double(settings,route_key(slot,field).c_str(),value);};
        integer("source",0);integer("shape",0);
        obs_data_set_default_string(settings,route_key(slot,"target").c_str(),"output");
        number("amount",0.);number("rate",1.);number("attack",10.);number("release",100.);number("sensitivity",0.);
    }
}
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
void fill_targets(obs_properties_t* props,const Instance* instance,obs_data_t* settings=nullptr){
    for(unsigned slot=0;slot<route_count;++slot){
        const std::string saved=settings?obs_data_get_string(settings,route_key(slot,"target").c_str()):instance?instance->routes[slot].saved_target:"";
        auto* target=obs_properties_get(props,route_key(slot,"target").c_str());if(!target)continue;
        obs_property_list_clear(target);
        obs_property_list_add_string(target,"Wet / dry","mix");
        obs_property_list_add_string(target,"Output amplitude","output");
        bool found=saved.empty()||saved=="mix"||saved=="output";
        if(instance)for(const auto& c:instance->controls)if(!c.integer&&!c.toggle&&c.max>c.min){
            auto id=target_id(instance->path,instance->label,c.port);
            obs_property_list_add_string(target,c.name.c_str(),id.c_str());
            found=found||id==saved;
        }
        if(!found)obs_property_list_add_string(target,"Unavailable saved destination (inactive)",saved.c_str());
    }
}
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
    fill_targets(props,nullptr,settings);
    if(format(rate,ch)&&inspect.open(settings,rate,ch)){
        fill_targets(props,&inspect,settings);
        for(auto& c:inspect.controls){
            auto k=key(c.port);
            if(c.toggle)obs_data_set_default_bool(settings,k.c_str(),c.initial!=0);
            else obs_data_set_default_double(settings,k.c_str(),c.initial);
        }
        add_controls(props,inspect.controls);
    }
    return true;
}
bool browse(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    auto* list=obs_properties_get(props,"catalog_effect");
    auto* catalog=static_cast<hyperdsp::Catalog*>(obs_properties_get_param(props));
    if(!list||!catalog)return false;
    auto query=hyperdsp::lower(obs_data_get_string(settings,"catalog_search"));
    std::string category=obs_data_get_string(settings,"catalog_category");
    std::string current=std::string(obs_data_get_string(settings,"library"))+"\t"+obs_data_get_string(settings,"label");
    unsigned rate=0,channels=0;format(rate,channels);
    obs_property_list_clear(list);
    obs_property_list_add_string(list,"Choose effect…","");
    for(auto& e:*catalog){
        const bool selected=e.id()==current;
        if(!selected && ((e.channels==2&&channels!=2)||
           (!category.empty()&&category!=e.category)||
           (!query.empty()&&hyperdsp::lower(e.name+" "+e.label+" "+e.maker).find(query)==std::string::npos)))continue;
        auto title=e.name+" · "+(e.channels==1?"Mono":"Stereo")+(e.maker.empty()?"":" · "+e.maker);
        obs_property_list_add_string(list,title.c_str(),e.id().c_str());
    }
    return true;
}
bool select_effect(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    auto* catalog=static_cast<hyperdsp::Catalog*>(obs_properties_get_param(props));
    std::string selected=obs_data_get_string(settings,"catalog_effect");
    if(!catalog||selected.empty())return false;
    for(auto& e:*catalog)if(e.id()==selected){
        if(e.path==obs_data_get_string(settings,"library")&&e.label==obs_data_get_string(settings,"label"))return false;
        for(unsigned p=0;p<max_ports;++p)obs_data_unset_user_value(settings,key(p).c_str());
        obs_data_set_string(settings,"library",e.path.c_str());
        obs_data_set_string(settings,"label",e.label.c_str());
        return modified(props,nullptr,settings);
    }
    return false;
}
bool manual_modified(obs_properties_t* props,obs_property_t* prop,obs_data_t* settings){
    std::string current=std::string(obs_data_get_string(settings,"library"))+"\t"+obs_data_get_string(settings,"label");
    if(current!=obs_data_get_string(settings,"catalog_effect"))obs_data_set_string(settings,"catalog_effect","");
    return modified(props,prop,settings);
}
bool reactive_visibility(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    for(unsigned slot=0;slot<route_count;++slot){
        const auto source=obs_data_get_int(settings,route_key(slot,"source").c_str());
        auto show=[&](const char* field,bool yes){if(auto* p=obs_properties_get(props,route_key(slot,field).c_str()))obs_property_set_visible(p,yes);};
        show("target",source!=0);show("amount",source!=0);
        show("shape",source==1);show("rate",source==1);
        show("attack",source>=2);show("release",source>=2);show("sensitivity",source>=3);
    }
    return true;
}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* catalog=new hyperdsp::Catalog(hyperdsp::read_catalog());
    obs_properties_set_param(props,catalog,[](void* v){delete static_cast<hyperdsp::Catalog*>(v);});
    auto* search=obs_properties_add_text(props,"catalog_search","Search effects",OBS_TEXT_DEFAULT);
    auto* category=obs_properties_add_list(props,"catalog_category","Category",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(category,"All effects","");
    std::vector<std::string> categories;
    for(auto& e:*catalog)if(std::find(categories.begin(),categories.end(),e.category)==categories.end())categories.push_back(e.category);
    std::sort(categories.begin(),categories.end());
    for(auto& c:categories)obs_property_list_add_string(category,c.c_str(),c.c_str());
    auto* list=obs_properties_add_list(props,"catalog_effect","Effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(list,catalog->empty()?"No catalog found. Run scan-ladspa.py, then reopen properties. Manual selection remains available.":"Only trusted native effects. Discovery is isolated; selected effects run in OBS.");
    obs_property_set_modified_callback(search,browse);
    obs_property_set_modified_callback(category,browse);
    obs_property_set_modified_callback(list,select_effect);
    auto* initial=obs_data_create();
    browse(props,nullptr,initial);
    obs_data_release(initial);
    auto* path=obs_properties_add_path(props,"library","Trusted LADSPA library",OBS_PATH_FILE,"Shared library (*.so)",nullptr);
    obs_property_set_long_description(path,"Native code executes inside OBS. Only choose plugins you trust.");
    auto* label=obs_properties_add_text(props,"label","Plugin label",OBS_TEXT_DEFAULT);
    auto* input=obs_properties_add_float_slider(props,"input_db","Input gain",-24.,24.,.1);
    auto* wet=obs_properties_add_float_slider(props,"mix","Wet / dry",0.,1.,.01);
    auto* output=obs_properties_add_float_slider(props,"output_db","Output gain",-24.,24.,.1);
    obs_property_float_set_suffix(input," dB");
    obs_property_float_set_suffix(output," dB");
    const char* sources[]={"Off","LFO","Input envelope","Bass energy","Mid energy","Treble energy"};
    const char* shapes[]={"Sine","Triangle","Square","Saw"};
    for(unsigned slot=0;slot<route_count;++slot){
        const std::string title="Route "+std::to_string(slot+1)+" · ";
        auto field=[&](const char* name){return route_key(slot,name);};
        auto* reactive=obs_properties_add_list(props,field("source").c_str(),(title+"Source").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        for(int i=0;i<6;++i)obs_property_list_add_int(reactive,sources[i],i);
        obs_properties_add_list(props,field("target").c_str(),(title+"Destination").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        auto* amount=obs_properties_add_float(props,field("amount").c_str(),(title+"Amount").c_str(),-10000,10000,.01);
        obs_property_set_long_description(amount,"Signed parameter units. Mix/output amount is bounded to ±1. Contributions to the same destination are summed before clamping.");
        auto* shape=obs_properties_add_list(props,field("shape").c_str(),(title+"Waveform").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        for(int i=0;i<4;++i)obs_property_list_add_int(shape,shapes[i],i);
        obs_properties_add_float_slider(props,field("rate").c_str(),(title+"Rate (Hz)").c_str(),.05,20,.05);
        obs_properties_add_float_slider(props,field("attack").c_str(),(title+"Attack (ms)").c_str(),1,200,1);
        obs_properties_add_float_slider(props,field("release").c_str(),(title+"Release (ms)").c_str(),10,2000,10);
        obs_properties_add_float_slider(props,field("sensitivity").c_str(),(title+"Sensitivity (dB)").c_str(),-24,24,.1);
        obs_property_set_modified_callback(reactive,reactive_visibility);
    }
    fill_targets(props,nullptr);
    auto* lfo=obs_properties_add_list(props,"lfo_target","LFO target",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(lfo,"Off",0);
    obs_property_list_add_int(lfo,"Wet / dry",1);
    obs_property_list_add_int(lfo,"Output amplitude (tremolo)",2);
    auto* rate=obs_properties_add_float_slider(props,"lfo_rate","LFO rate",.05,20.,.05);
    obs_property_float_set_suffix(rate," Hz");
    auto* depth=obs_properties_add_float_slider(props,"lfo_depth","LFO depth",0.,1.,.01);
    obs_property_set_long_description(depth,"Sine modulation, shared phase across channels. Wet/dry is clamped to 0–1; amplitude modulation multiplies the output by 1 + depth × sine.");
    obs_property_set_long_description(wet,"0 = dry, 1 = wet. Input and output gain remain active with a valid effect; missing effects bypass unmodified. No latency compensation.");
    obs_property_set_modified_callback(path,manual_modified);
    obs_property_set_modified_callback(label,manual_modified);
    if(p){auto* f=static_cast<Filter*>(p);std::lock_guard lock(f->mutex);
        if(f->active){add_controls(props,f->active->controls);fill_targets(props,f->active.get());}}
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
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process,
};
}
