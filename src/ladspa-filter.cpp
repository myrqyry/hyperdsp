#include <obs-module.h>
#include "ladspa-catalog.hpp"
#include "framework/dsp/modulation.hpp"
#include "framework/dsp/ladspa_engine.hpp"
#include "framework/dsp/realtime_handoff.hpp"
#include "framework/dsp/modulation_matrix.hpp"
#include "interop/external_inputs.hpp"
#include <ladspa.h>

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
using hyperdsp::Control;
using hyperdsp::key;
using hyperdsp::target_id;
struct Parameters {
    hyperdsp::MixSettings mix{};
    hyperdsp::LfoSettings lfo{};
    std::array<hyperdsp::LadspaEngine::Route,route_count> routes{};
    std::array<hyperdsp::RouteConfig,route_count> configs{};
    std::array<std::string,route_count> targets{};
    std::array<float,max_ports> values{};
    hyperdsp::ExternalInputs external;
};
// Metadata (path, label, rate, channels, controls) is immutable after open.
// All other engine state belongs exclusively to the audio consumer once published.
struct Instance : hyperdsp::LadspaEngine {
    Parameters read_parameters(obs_data_t* settings,const hyperdsp::ExternalInputs& previous={},bool subscribe=true)const{
        Parameters p;p.external=previous;
        p.mix=hyperdsp::MixSettings::from(obs_data_get_double(settings,"mix"),
            obs_data_get_double(settings,"input_db"),obs_data_get_double(settings,"output_db"));
        const auto target=obs_data_get_int(settings,"lfo_target");
        p.lfo={target>=0&&target<=2?int(target):0,
            obs_data_get_double(settings,"lfo_rate"),obs_data_get_double(settings,"lfo_depth")};
        for(unsigned slot=0;slot<route_count;++slot){
            auto& r=p.routes[slot];
            auto integer=[&](const char* key){return obs_data_get_int(settings,route_key(slot,key).c_str());};
            auto number=[&](const char* key){return obs_data_get_double(settings,route_key(slot,key).c_str());};
            auto& target=p.targets[slot];target=obs_data_get_string(settings,route_key(slot,"target").c_str());
            const auto source=integer("source"),shape=integer("shape");
            r.source=source>=0&&source<=10?int(source):0;
            r.mix=target=="mix";r.output=target=="output";
            for(const auto& c:controls)
                if(!c.integer&&!c.toggle&&c.max>c.min&&target==target_id(path,label,c.port)){
                    r.port=int(c.port);r.min=c.min;r.max=c.max;
                }
            if(r.port<0&&!r.mix&&!r.output)r.source=0;
            const double limit=r.port>=0?10000.:1.;
            const float amount=hyperdsp::MixSettings::bounded(number("amount"),-limit,limit,0);
            const int destination=r.port>=0?r.port+2:r.mix?0:r.output?1:-1;
            p.configs[slot]={r.source,destination,shape>=0&&shape<=3?int(shape):0,
                number("rate"),amount,number("attack"),number("release"),number("sensitivity")};
            if(subscribe)p.external.configure(slot,r.source,obs_data_get_string(settings,route_key(slot,r.source>=7?"sight_channel":"sound_channel").c_str()),
                obs_data_get_string(settings,route_key(slot,"sound_category").c_str()));
        }
        for(const auto& c:controls){
            auto k=key(c.port);
            if(c.toggle)obs_data_set_default_bool(settings,k.c_str(),c.initial!=0);
            else obs_data_set_default_double(settings,k.c_str(),c.initial);
            double raw=obs_data_has_user_value(settings,k.c_str())?
                (c.toggle?double(obs_data_get_bool(settings,k.c_str())):obs_data_get_double(settings,k.c_str())):c.initial;
            float value=std::isfinite(raw)?float(std::clamp(raw,double(c.min),double(c.max))):c.initial;
            if(c.integer||c.toggle)value=std::clamp(std::round(value),c.min,c.max);
            p.values[c.port]=value;
        }
        return p;
    }
    void apply(const Parameters& p){
        const bool first=!initialized;
        if(first){modulation.reset(rate,p.mix,p.lfo);initialized=true;}
        else modulation.set(p.mix,p.lfo);
        routes=p.routes;any_route=any_port=false;
        for(unsigned slot=0;slot<route_count;++slot){
            const auto& r=routes[slot];matrix.configure(slot,rate,p.configs[slot],first);
            any_route=any_route||r.source!=0;any_port=any_port||(r.source!=0&&r.port>=0);
        }
        for(const auto& c:controls)set_control(c,p.values[c.port]);
    }
    bool open(obs_data_t* settings,unsigned sr,unsigned ch,Parameters& parameters,bool subscribe=true){
        return LadspaEngine::open(obs_data_get_string(settings,"library"),obs_data_get_string(settings,"label"),sr,ch,
            [&](auto&){parameters=read_parameters(settings,parameters.external,subscribe);apply(parameters);return true;});
    }
    void process(obs_audio_data* audio,const Parameters& parameters){
        std::array<float*,8> p{};
        for(unsigned c=0;c<channels;++c)p[c]=reinterpret_cast<float*>(audio->data[c]);
        matrix.external_values(parameters.external.snapshot());
        LadspaEngine::process(p.data(),channels,audio->frames);
    }
};
struct Packet {
    std::shared_ptr<Instance> instance;
    Parameters parameters;
    uint64_t generation=0;
    bool initial=false;
};
struct Filter {
    obs_source_t* source=nullptr;
    std::mutex control;
    std::shared_ptr<Instance> accepted;
    hyperdsp::ExternalInputs external;
    std::array<std::string,route_count> targets;
    uint64_t generation=0,applied=0;
    hyperdsp::RealtimeHandoff<Packet> handoff;
};
bool format(unsigned& rate,unsigned& channels){
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai))return false;
    rate=ai.samples_per_sec;channels=get_audio_channels(ai.speakers);
    return rate&&channels;
}
const char* name(void*){return "HyperDSP LADSPA";}
void defaults(obs_data_t* settings){
    obs_data_set_default_string(settings,"catalog_sources",
        "On Mint / Ubuntu / Debian, look for LADSPA packages in your software manager: "
        "lsp-plugins-ladspa, swh-plugins, caps, tap-plugins, invada-studio-plugins-ladspa, dpf-plugins-ladspa. "
        "Install trusted packages, then scan and reload. CLAP/LV2/VST/JSFX are not hosted here yet.");
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
    try{
        std::lock_guard lock(f->control);
        auto next=std::make_unique<Packet>();
        if(f->accepted&&f->accepted->path==obs_data_get_string(settings,"library")&&
           f->accepted->label==obs_data_get_string(settings,"label")&&f->accepted->rate==rate&&f->accepted->channels==ch){
            next->instance=f->accepted;
            next->parameters=next->instance->read_parameters(settings,f->external);
        }else{
            // Instantiate and activate with initial port values off audio.
            auto instance=std::make_shared<Instance>();
            if(instance->open(settings,rate,ch,next->parameters)){
                next->instance=std::move(instance);next->initial=true;
            }else next->parameters={};
        }
        f->targets=next->parameters.targets;
        f->external=next->parameters.external;
        f->accepted=next->instance;
        next->generation=++f->generation;
        f->handoff.publish(std::move(next));
    }catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP LADSPA] Settings preparation failed: %s",e.what());}
}
void* create(obs_data_t* s,obs_source_t* source){auto f=std::make_unique<Filter>();f->source=source;update(f.get(),s);return f.release();}
void destroy(void* p){delete static_cast<Filter*>(p);}
void fill_targets(obs_properties_t* props,const Instance* instance,obs_data_t* settings=nullptr,const std::array<std::string,route_count>* targets=nullptr){
    for(unsigned slot=0;slot<route_count;++slot){
        const std::string saved=settings?obs_data_get_string(settings,route_key(slot,"target").c_str()):targets?(*targets)[slot]:"";
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
    unsigned rate=0,ch=0;Instance inspect;Parameters parameters;
    fill_targets(props,nullptr,settings);
    if(format(rate,ch)&&inspect.open(settings,rate,ch,parameters,false)){
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
struct CatalogBrowser {
    hyperdsp::Catalog entries=hyperdsp::read_catalog();
    std::string query,category,current;
};
void fill_categories(obs_properties_t* props,const CatalogBrowser& browser){
    auto* category=obs_properties_get(props,"catalog_category");if(!category)return;
    obs_property_list_clear(category);
    obs_property_list_add_string(category,"All effects","");
    std::vector<std::string> categories;
    for(auto& e:browser.entries)if(std::find(categories.begin(),categories.end(),e.category)==categories.end())categories.push_back(e.category);
    if(!browser.category.empty()&&std::find(categories.begin(),categories.end(),browser.category)==categories.end())categories.push_back(browser.category);
    std::sort(categories.begin(),categories.end());
    for(auto& c:categories)obs_property_list_add_string(category,c.c_str(),c.c_str());
}
bool fill_results(obs_properties_t* props,CatalogBrowser& browser){
    auto* list=obs_properties_get(props,"catalog_effect");if(!list)return false;
    unsigned rate=0,channels=0;format(rate,channels);
    obs_property_list_clear(list);
    obs_property_list_add_string(list,"Choose effect…","");
    unsigned matches=0;bool current_found=false;
    for(auto& e:browser.entries){
        const bool selected=e.id()==browser.current;
        current_found|=selected;
        if(!selected&&((e.channels==2&&channels!=2)||
            (!browser.category.empty()&&browser.category!=e.category)||!hyperdsp::catalog_matches(e,browser.query)))continue;
        auto title=e.name+" · "+(e.channels==1?"Mono":"Stereo")+(e.maker.empty()?"":" · "+e.maker);
        obs_property_list_add_string(list,title.c_str(),e.id().c_str());++matches;
    }
    if(!current_found&&browser.current!="	"&&!browser.current.empty()){
        auto index=obs_property_list_add_string(list,"Current effect (not in catalog)",browser.current.c_str());
        obs_property_list_item_disable(list,index,true);
    }
    auto description=std::to_string(matches)+" displayed / "+std::to_string(browser.entries.size())+" installed descriptors. ";
    description+=browser.entries.empty()?"Scan installed effects to begin.":"Only trusted native effects; eligibility is not audio qualification.";
    obs_property_set_long_description(list,description.c_str());
    return true;
}
bool browse(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    auto* browser=static_cast<CatalogBrowser*>(obs_properties_get_param(props));if(!browser)return false;
    browser->query=obs_data_get_string(settings,"catalog_search");
    browser->category=obs_data_get_string(settings,"catalog_category");
    browser->current=std::string(obs_data_get_string(settings,"library"))+"	"+obs_data_get_string(settings,"label");
    return fill_results(props,*browser);
}
bool start_scan(obs_properties_t* props,obs_property_t*,void*){
    auto message=hyperdsp::CatalogScan::start();
    auto* button=obs_properties_get(props,"catalog_reload");
    if(button){
        obs_property_set_long_description(button,message.c_str());
        obs_property_set_description(button,message.starts_with("Scanning")?"Reload catalog (scan running)":"Reload catalog");
    }
    return true;
}
bool reload_catalog(obs_properties_t* props,obs_property_t*,void*){
    auto* browser=static_cast<CatalogBrowser*>(obs_properties_get_param(props));if(!browser)return false;
    auto status=hyperdsp::CatalogScan::status();
    browser->entries=hyperdsp::read_catalog();fill_categories(props,*browser);fill_results(props,*browser);
    auto* button=obs_properties_get(props,"catalog_reload");
    if(button){
        if(status.starts_with("Scan finished"))status="Catalog reloaded.";
        obs_property_set_long_description(button,status.c_str());
        obs_property_set_description(button,status.starts_with("Scanning")?"Reload catalog (scan running)":"Reload catalog");
    }
    return true;
}
bool select_effect(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    auto* browser=static_cast<CatalogBrowser*>(obs_properties_get_param(props));
    std::string selected=obs_data_get_string(settings,"catalog_effect");
    if(!browser||selected.empty())return false;
    for(auto& e:browser->entries)if(e.id()==selected){
        if(e.path==obs_data_get_string(settings,"library")&&e.label==obs_data_get_string(settings,"label"))return false;
        for(unsigned p=0;p<max_ports;++p)obs_data_unset_user_value(settings,key(p).c_str());
        obs_data_set_string(settings,"library",e.path.c_str());
        obs_data_set_string(settings,"label",e.label.c_str());
        browser->current=selected;return modified(props,nullptr,settings);
    }
    return false;
}
bool manual_modified(obs_properties_t* props,obs_property_t* prop,obs_data_t* settings){
    std::string current=std::string(obs_data_get_string(settings,"library"))+"\t"+obs_data_get_string(settings,"label");
    if(current!=obs_data_get_string(settings,"catalog_effect"))obs_data_set_string(settings,"catalog_effect","");
    browse(props,prop,settings);return modified(props,prop,settings);
}
bool reactive_visibility(obs_properties_t* props,obs_property_t*,obs_data_t* settings){
    for(unsigned slot=0;slot<route_count;++slot){
        const auto source=obs_data_get_int(settings,route_key(slot,"source").c_str());
        auto show=[&](const char* field,bool yes){if(auto* p=obs_properties_get(props,route_key(slot,field).c_str()))obs_property_set_visible(p,yes);};
        show("target",source!=0);show("amount",source!=0);
        show("shape",source==1);show("rate",source==1);
        show("attack",source>=2&&source<=5);show("release",source>=2&&source<=5);show("sensitivity",source>=3&&source<=5);
        show("sound_channel",source==6);show("sound_category",source==6);
        show("sight_channel",source>=7);
        if(source>=7)hyperdsp::sight_channel_choices(obs_properties_get(props,route_key(slot,"sight_channel").c_str()),obs_data_get_string(settings,route_key(slot,"sight_channel").c_str()),source);
        if(source==6)hyperdsp::sound_channel_choices(obs_properties_get(props,route_key(slot,"sound_channel").c_str()),obs_data_get_string(settings,route_key(slot,"sound_channel").c_str()));
    }
    return true;
}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* browser=new CatalogBrowser;
    obs_properties_set_param(props,browser,[](void* v){delete static_cast<CatalogBrowser*>(v);});
    auto* scan=obs_properties_add_button2(props,"catalog_scan","Scan installed effects",start_scan,nullptr);
    obs_property_set_long_description(scan,"Explicitly probe installed LADSPA libraries in separate processes. Only scan trusted locations; this is not a security sandbox.");
    auto* reload=obs_properties_add_button2(props,"catalog_reload","Reload catalog",reload_catalog,nullptr);
    obs_property_set_long_description(reload,hyperdsp::CatalogScan::status().c_str());
    auto* guide=obs_properties_add_text(props,"catalog_sources","Find more effects",OBS_TEXT_INFO);
    obs_property_text_set_info_word_wrap(guide,true);
    auto* collections=obs_properties_create();
    auto add_collection=[&](const char* key,const char* title,const char* url){
        auto* link=obs_properties_add_button2(collections,key,title,nullptr,nullptr);
        obs_property_button_set_type(link,OBS_BUTTON_URL);
        // libobs copies the URL; the API takes char* for historical reasons.
        std::string copy(url);obs_property_button_set_url(link,copy.data());
    };
    add_collection("catalog_lsp","Linux Studio Plugins (LSP)","https://lsp-plug.in/");
    add_collection("catalog_tap","Tom's Audio Processing (TAP)","https://tap-plugins.sourceforge.io/");
    add_collection("catalog_dpf","DISTRHO Plugins","https://github.com/DISTRHO/DPF-Plugins");
    obs_properties_add_group(props,"catalog_collections","External effect collections",OBS_GROUP_NORMAL,collections);
    auto* search=obs_properties_add_text(props,"catalog_search","Search effects",OBS_TEXT_DEFAULT);
    auto* category=obs_properties_add_list(props,"catalog_category","Category",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    fill_categories(props,*browser);
    auto* list=obs_properties_add_list(props,"catalog_effect","Effect",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    obs_property_set_modified_callback(search,browse);
    obs_property_set_modified_callback(category,browse);
    obs_property_set_modified_callback(list,select_effect);
    auto* initial=p?obs_source_get_settings(static_cast<Filter*>(p)->source):obs_data_create();
    browse(props,nullptr,initial);
    fill_categories(props,*browser);
    obs_data_release(initial);
    auto* path=obs_properties_add_path(props,"library","Trusted LADSPA library",OBS_PATH_FILE,hyperdsp::platform::library_filter,nullptr);
    obs_property_set_long_description(path,"Native code executes inside OBS. Only choose plugins you trust.");
    auto* label=obs_properties_add_text(props,"label","Plugin label",OBS_TEXT_DEFAULT);
    auto* input=obs_properties_add_float_slider(props,"input_db","Input gain",-24.,24.,.1);
    auto* wet=obs_properties_add_float_slider(props,"mix","Wet / dry",0.,1.,.01);
    auto* output=obs_properties_add_float_slider(props,"output_db","Output gain",-24.,24.,.1);
    obs_property_float_set_suffix(input," dB");
    obs_property_float_set_suffix(output," dB");
    const char* sources[]={"Off","LFO","Input envelope","Bass energy","Mid energy","Treble energy","Sound category confidence","Sight motion energy","Sight hand X","Sight hand Y","Sight pinch closeness"};
    const char* shapes[]={"Sine","Triangle","Square","Saw"};
    for(unsigned slot=0;slot<route_count;++slot){
        const std::string title="Route "+std::to_string(slot+1)+" · ";
        auto field=[&](const char* name){return route_key(slot,name);};
        auto* reactive=obs_properties_add_list(props,field("source").c_str(),(title+"Source").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        for(int i=0;i<11;++i)obs_property_list_add_int(reactive,sources[i],i);
        obs_properties_add_list(props,field("target").c_str(),(title+"Destination").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        auto* amount=obs_properties_add_float(props,field("amount").c_str(),(title+"Amount").c_str(),-10000,10000,.01);
        obs_property_set_long_description(amount,"Signed parameter units. Mix/output amount is bounded to ±1. Contributions to the same destination are summed before clamping.");
        auto* shape=obs_properties_add_list(props,field("shape").c_str(),(title+"Waveform").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
        for(int i=0;i<4;++i)obs_property_list_add_int(shape,shapes[i],i);
        obs_properties_add_float_slider(props,field("rate").c_str(),(title+"Rate (Hz)").c_str(),.05,20,.05);
        obs_properties_add_float_slider(props,field("attack").c_str(),(title+"Attack (ms)").c_str(),1,200,1);
        obs_properties_add_float_slider(props,field("release").c_str(),(title+"Release (ms)").c_str(),10,2000,10);
        obs_properties_add_float_slider(props,field("sensitivity").c_str(),(title+"Sensitivity (dB)").c_str(),-24,24,.1);
        auto* ch=obs_properties_add_list(props,field("sound_channel").c_str(),(title+"Sound classifier").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(ch,"Choose Sound classifier","");
        obs_properties_add_text(props,field("sound_category").c_str(),(title+"Sound category (exact label)").c_str(),OBS_TEXT_DEFAULT);
        obs_properties_add_list(props,field("sight_channel").c_str(),(title+"Sight input").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
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
    if(p){
        auto* f=static_cast<Filter*>(p);
        std::shared_ptr<Instance> accepted;std::array<std::string,route_count> targets;
        {std::lock_guard lock(f->control);accepted=f->accepted;targets=f->targets;}
        if(accepted){add_controls(props,accepted->controls);fill_targets(props,accepted.get(),nullptr,&targets);}
    }
    return props;
}
obs_audio_data* process(void* p,obs_audio_data* audio){
    auto* f=static_cast<Filter*>(p);if(!f||!audio)return audio;
    auto* packet=f->handoff.consume();
    unsigned rate=0,ch=0;
    if(!packet||!packet->instance||!format(rate,ch)||rate!=packet->instance->rate||ch!=packet->instance->channels)return audio;
    auto& instance=*packet->instance;
    if(f->applied!=packet->generation){
        if(!packet->initial)instance.apply(packet->parameters);
        f->applied=packet->generation;
    }
    instance.process(audio,packet->parameters);
    return audio;
}
}
extern "C" {
struct obs_source_info ladspa_filter={
    .id="hyperdsp_ladspa_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process,
};
}
