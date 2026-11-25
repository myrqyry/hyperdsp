#include "effect-library.hpp"
#include "framework/dsp/parameter_schema.hpp"
#include <iostream>
#include <charconv>
#include <cstdlib>
namespace {
void number_parameter(obs_data_array_t* items,const char* key,const char* name,double min,double max,double step,double initial,const char* unit){
    auto* item=obs_data_create();obs_data_set_string(item,"key",key);obs_data_set_string(item,"name",name);
    obs_data_set_string(item,"type","number");obs_data_set_string(item,"unit",unit);
    obs_data_set_double(item,"min",min);obs_data_set_double(item,"max",max);
    obs_data_set_double(item,"step",step);obs_data_set_double(item,"default",initial);
    obs_data_array_push_back(items,item);obs_data_release(item);
}
template<class S,size_t N> void parameter_list(obs_data_array_t* items,const S& settings,const hyperdsp::FloatParameter<S>(&spec)[N]){
    for(auto& p:spec)number_parameter(items,p.key,p.name,p.min,p.max,p.step,settings.*p.member,p.unit);
}
void graph_metadata(obs_data_t* item,const hyperdsp::EffectEntry& e){
    const bool color=e.format=="native"&&e.source=="hyperdsp_color_filter";
    const bool spectral=e.format=="native"&&e.source=="hyperdsp_spectral_filter";
    obs_data_set_bool(item,"graph_supported",color||spectral);
    if(!color&&!spectral)return;
    auto* params=obs_data_array_create();
    if(color){
        parameter_list(params,hyperdsp::ColorSettings{},hyperdsp::color_parameters);
        auto* p=obs_data_create();obs_data_set_string(p,"key","oversample");obs_data_set_string(p,"name","Oversampling");
        obs_data_set_string(p,"type","choice");obs_data_set_int(p,"default",1);
        auto* choices=obs_data_array_create();
        for(auto v:hyperdsp::oversampling_choices){
            auto* c=obs_data_create();obs_data_set_int(c,"value",v);
            obs_data_array_push_back(choices,c);obs_data_release(c);
        }
        obs_data_set_array(p,"choices",choices);obs_data_array_release(choices);
        obs_data_array_push_back(params,p);obs_data_release(p);
    }else parameter_list(params,hyperdsp::SpectralSettings{},hyperdsp::spectral_parameters);
    obs_data_set_array(item,"parameters",params);obs_data_array_release(params);
}
std::string default_catalog(){
    if(const char* override_path=std::getenv("HYPERDSP_CATALOG"))return override_path;
    if(const char* config=std::getenv("XDG_CONFIG_HOME");config&&*config)
        return std::string(config)+"/obs-studio/plugin_config/hyperdsp/ladspa-catalog.json";
    if(const char* home=std::getenv("HOME");home&&*home)
        return std::string(home)+"/.config/obs-studio/plugin_config/hyperdsp/ladspa-catalog.json";
    return {};
}
}
int main(int argc,char** argv){
    std::string path=default_catalog(),query,format;
    unsigned channels=2;
    bool json=false;
    for(int i=1;i<argc;++i){
        std::string_view arg=argv[i];
        if(arg=="--help"){
            std::cout<<"Usage: hyperdsp-effects [--catalog FILE] [--search WORDS] [--format native|ladspa] [--channels 1..8] [--json]\n";
            return 0;
        }
        if(arg=="--json"){json=true;continue;}
        if(i+1>=argc){std::cerr<<"Missing argument: "<<arg<<"\n";return 2;}
        std::string value=argv[++i];
        if(arg=="--catalog")path=value;
        else if(arg=="--search")query=value;
        else if(arg=="--format"&&(value=="native"||value=="ladspa"))format=value;
        else if(arg=="--channels"){
            auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),channels);
            if(error!=std::errc{}||end!=value.data()+value.size()||channels<1||channels>8)return 2;
        }else{std::cerr<<"Invalid option: "<<arg<<"\n";return 2;}
    }
    auto entries=hyperdsp::search_effects(hyperdsp::effect_library(hyperdsp::read_catalog(path),channels),query,format);
    if(!json){
        for(auto& e:entries)std::cout<<e.name<<" | "<<e.category<<" | "<<e.format<<" | "
            <<(e.compatible?"eligible":"unavailable for layout/path")<<" | "<<e.description<<"\n";
        std::cout<<entries.size()<<" entries (metadata only)\n";return 0;
    }
    auto* root=obs_data_create();auto* items=obs_data_array_create();
    obs_data_set_int(root,"schema",1);obs_data_set_int(root,"channels",channels);
    for(auto& e:entries){
        auto* item=obs_data_create();
        auto field=[&](const char* key,const std::string& value){obs_data_set_string(item,key,value.c_str());};
        field("id",e.id);field("name",e.name);field("category",e.category);field("description",e.description);
        field("format",e.format);field("source",e.source);field("maker",e.maker);
        obs_data_set_bool(item,"compatible",e.compatible);
        if(e.format=="native"){field("mode_key",e.mode_key);obs_data_set_int(item,"mode",e.mode);}
        else{field("path",e.path);field("label",e.label);obs_data_set_int(item,"channels",e.channels);}
        graph_metadata(item,e);
        obs_data_array_push_back(items,item);obs_data_release(item);
    }
    obs_data_set_array(root,"effects",items);obs_data_array_release(items);
    auto* utilities=obs_data_array_create();auto* gain=obs_data_create();auto* params=obs_data_array_create();
    obs_data_set_string(gain,"id","gain");obs_data_set_string(gain,"name","Gain / weighted sum");
    number_parameter(params,hyperdsp::gain_key,"Gain",hyperdsp::gain_min,hyperdsp::gain_max,hyperdsp::gain_step,0," dB");
    obs_data_set_array(gain,"parameters",params);obs_data_array_release(params);
    obs_data_array_push_back(utilities,gain);obs_data_release(gain);
    obs_data_set_array(root,"graph_utilities",utilities);obs_data_array_release(utilities);
    std::cout<<obs_data_get_json(root)<<"\n";obs_data_release(root);
}
