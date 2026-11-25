#pragma once
#include "framework/dsp/modulation_matrix.hpp"
#include "interop/external_inputs.hpp"
#include <obs-module.h>
#include <string>
#include <initializer_list>
namespace hyperdsp::obs_routes {
inline std::string key(unsigned i,const char* field){return "route"+std::to_string(i+1)+"_"+field;}
inline void defaults(obs_data_t* s){
    for(unsigned i=0;i<modulation_slots;++i){
        for(auto f:{"source","target","shape"})obs_data_set_default_int(s,key(i,f).c_str(),0);
        for(auto f:{"amount","sensitivity"})obs_data_set_default_double(s,key(i,f).c_str(),0);
        obs_data_set_default_double(s,key(i,"rate").c_str(),1);
        obs_data_set_default_double(s,key(i,"attack").c_str(),10);
        obs_data_set_default_double(s,key(i,"release").c_str(),100);
    }
}
inline RouteConfig read(obs_data_t* s,unsigned i,int max_target){
    auto integer=[&](const char* f,int max){auto v=obs_data_get_int(s,key(i,f).c_str());return v>=0&&v<=max?int(v):0;};
    auto number=[&](const char* f){return obs_data_get_double(s,key(i,f).c_str());};
    return {integer("source",10),integer("target",max_target),integer("shape",3),number("rate"),number("amount"),number("attack"),number("release"),number("sensitivity")};
}
inline void connect(ExternalInputs& external,obs_data_t* s,unsigned i,int source){
    external.configure(i,source,obs_data_get_string(s,key(i,source>=7?"sight_channel":"sound_channel").c_str()),
        obs_data_get_string(s,key(i,"sound_category").c_str()));
}
inline bool visibility(obs_properties_t* p,obs_property_t*,obs_data_t* s){
    for(unsigned i=0;i<modulation_slots;++i){
        auto k=[&](const char* f){return key(i,f);};
        auto src=obs_data_get_int(s,k("source").c_str());
        auto show=[&](const char* f,bool v){if(auto* prop=obs_properties_get(p,k(f).c_str()))obs_property_set_visible(prop,v);};
        show("target",src!=0);show("amount",src!=0);
        show("shape",src==1);show("rate",src==1);
        show("attack",src>=2&&src<=5);show("release",src>=2&&src<=5);show("sensitivity",src>=3&&src<=5);
        show("sound_channel",src==6);show("sound_category",src==6);show("sight_channel",src>=7);
        if(src==6)sound_channel_choices(obs_properties_get(p,k("sound_channel").c_str()),obs_data_get_string(s,k("sound_channel").c_str()));
        if(src>=7)sight_channel_choices(obs_properties_get(p,k("sight_channel").c_str()),obs_data_get_string(s,k("sight_channel").c_str()),int(src));
    }
    return true;
}
inline void add(obs_properties_t* p,std::initializer_list<const char*> targets,const char* amount_description){
    for(unsigned i=0;i<modulation_slots;++i){
        auto k=[&](const char* f){return key(i,f);};
        auto prefix="Route "+std::to_string(i+1)+" · ";
        auto list=[&](const char* f,const char* title,std::initializer_list<const char*> values){
            auto* prop=obs_properties_add_list(p,k(f).c_str(),(prefix+title).c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
            int n=0;for(auto value:values)obs_property_list_add_int(prop,value,n++);return prop;
        };
        auto slider=[&](const char* f,const char* title,double lo,double hi,double step){
            return obs_properties_add_float_slider(p,k(f).c_str(),(prefix+title).c_str(),lo,hi,step);
        };
        auto* source=list("source","Source",{"Off","LFO","Input envelope","Bass energy","Mid energy","Treble energy",
            "Sound category confidence","Sight motion energy","Sight hand X","Sight hand Y","Sight pinch closeness"});
        list("target","Destination",targets);
        auto* amount=obs_properties_add_float(p,k("amount").c_str(),(prefix+"Amount").c_str(),-10000,10000,.01);
        obs_property_set_long_description(amount,amount_description);
        list("shape","Waveform",{"Sine","Triangle","Square","Saw"});
        slider("rate","Rate (Hz)",.05,20,.05);
        slider("attack","Detector attack (ms)",1,200,1);
        slider("release","Detector release (ms)",10,2000,10);
        slider("sensitivity","Band sensitivity (dB)",-24,24,.1);
        auto* sound=obs_properties_add_list(p,k("sound_channel").c_str(),(prefix+"Sound classifier").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(sound,"Choose Sound classifier","");
        obs_properties_add_text(p,k("sound_category").c_str(),(prefix+"Sound category (exact label)").c_str(),OBS_TEXT_DEFAULT);
        auto* sight=obs_properties_add_list(p,k("sight_channel").c_str(),(prefix+"Sight input").c_str(),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
        obs_property_list_add_string(sight,"Choose Sight input","");
        obs_property_set_modified_callback(source,visibility);
    }
}
}
