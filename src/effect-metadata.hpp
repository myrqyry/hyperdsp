#pragma once
#include "effect-library.hpp"
#include "framework/dsp/parameter_schema.hpp"
namespace hyperdsp {
inline void number_parameter(obs_data_array_t* items,const char* key,const char* name,double min,double max,double step,double initial,const char* unit,bool bindable=true){
    auto* item=obs_data_create();obs_data_set_string(item,"key",key);obs_data_set_string(item,"name",name);
    obs_data_set_string(item,"type","number");obs_data_set_bool(item,"bindable",bindable);obs_data_set_string(item,"unit",unit);
    obs_data_set_double(item,"min",min);obs_data_set_double(item,"max",max);
    obs_data_set_double(item,"step",step);obs_data_set_double(item,"default",initial);
    obs_data_array_push_back(items,item);obs_data_release(item);
}
template<class S,size_t N> inline void parameter_list(obs_data_array_t* items,const S& settings,const hyperdsp::FloatParameter<S>(&spec)[N]){
    for(auto& p:spec)number_parameter(items,p.key,p.name,p.min,p.max,p.step,p.get(settings),p.unit);
}
template<size_t N> inline void choice_parameter(obs_data_array_t* items,const char* key,const char* name,unsigned initial,const unsigned(&values)[N]){
    auto* p=obs_data_create();obs_data_set_string(p,"key",key);obs_data_set_string(p,"name",name);
    obs_data_set_string(p,"type","choice");obs_data_set_int(p,"default",initial);
    auto* choices=obs_data_array_create();
    for(auto v:values){
        auto* c=obs_data_create();obs_data_set_int(c,"value",v);
        obs_data_array_push_back(choices,c);obs_data_release(c);
    }
    obs_data_set_array(p,"choices",choices);obs_data_array_release(choices);
    obs_data_array_push_back(items,p);obs_data_release(p);
}
inline void graph_metadata(obs_data_t* item,const hyperdsp::EffectEntry& e,unsigned rate){
    using namespace hyperdsp;
    const bool supported=e.format=="native"||e.format=="ladspa";
    obs_data_set_bool(item,"latency_known",e.format=="native");
    obs_data_set_bool(item,"graph_supported",supported);
    if(!supported)return;
    auto* params=obs_data_array_create();
    if(e.format=="ladspa"){
        obs_data_set_bool(item,"controls_complete",e.controls_complete);
        number_parameter(params,"mix","Wet / dry",0,1,.01,1,"");
        number_parameter(params,"input_db","Input gain",-24,24,.1,0," dB");
        number_parameter(params,"output_db","Output gain",-24,24,.1,0," dB");
        for(auto& h:e.controls){
            const auto c=control_from_hint(h.port,h.name,h.hint,rate);
            auto initial=c.integer||c.toggle?std::clamp(std::round(c.initial),c.min,c.max):c.initial;
            auto k=key(c.port);
            if(c.toggle){const unsigned choices[]={0,1};choice_parameter(params,k.c_str(),c.name.c_str(),unsigned(initial),choices);}
            else number_parameter(params,k.c_str(),c.name.c_str(),c.min,c.max,c.integer?1:.001,initial,"",!c.integer);
        }
    }else if(e.source=="hyperdsp_color_filter"){
        parameter_list(params,ColorSettings{},color_parameters);
        choice_parameter(params,"oversample","Oversampling",1,oversampling_choices);
    }else if(e.source=="hyperdsp_spectral_filter"){
        parameter_list(params,SpectralSettings{},spectral_parameters);
    }else if(e.source=="hyperdsp_mic_filter"){
        parameter_list(params,MicSettings{},mic_parameters);
    }else if(e.source=="hyperdsp_native_filter"){
        parameter_list(params,FilterSettings{},native_parameters);
        choice_parameter(params,"stages","Slope",1,filter_stages);
        choice_parameter(params,"alignment","Response",0,alignment_choices);
    }else if(e.source=="hyperdsp_dynamics_filter"){
        parameter_list(params,DynamicsSettings{},dynamics_parameters);
    }else if(e.source=="hyperdsp_dynamic_eq_filter"){
        parameter_list(params,DynamicEqSettings{},dynamic_eq_parameters);
    }else if(e.source=="hyperdsp_transient_filter"){
        if(e.mode==2){
            BandTransientSettings settings;
            for(auto& p:band_transient_crossovers)number_parameter(params,p.key,p.name,p.min,p.max,p.step,p.get(settings),p.unit,false);
            parameter_list(params,settings,band_transient_parameters);
        }else parameter_list(params,TransientSettings{},transient_parameters);
    }else if(e.source=="hyperdsp_stereo_filter"){
        parameter_list(params,StereoSettings{},stereo_parameters);
    }else if(e.source=="hyperdsp_multiband_filter"){
        MultibandSettings settings;
        for(auto& p:multiband_crossovers)number_parameter(params,p.key,p.name,p.min,p.max,p.step,p.get(settings),p.unit,false);
        parameter_list(params,settings,multiband_parameters);
    }else if(e.source=="hyperdsp_time_filter"){
        TimeSettings settings;
        for(auto& p:time_parameters){
            if(std::string_view(p.key)=="delay_ms"&&e.mode>=3)
                number_parameter(params,p.key,p.name,.1,short_delay_max(e.mode),.1,short_delay_default(e.mode),p.unit);
            else number_parameter(params,p.key,p.name,p.min,p.max,p.step,p.get(settings),p.unit);
        }
    }else if(e.source=="hyperdsp_space_filter"){
        parameter_list(params,SpaceSettings{},space_parameters);
        choice_parameter(params,"stages","All-pass stages",6,space_stages);
    }else if(e.source=="hyperdsp_formant_filter"){
        parameter_list(params,FormantSettings{},formant_parameters);
        choice_parameter(params,"from","From vowel",0,vowel_choices);
        choice_parameter(params,"to","To vowel",2,vowel_choices);
    }
    obs_data_set_array(item,"parameters",params);obs_data_array_release(params);
}
}
