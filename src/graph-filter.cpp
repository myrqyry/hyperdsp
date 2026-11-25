#include <obs-module.h>
#include <jansson.h>
#include "framework/dsp/effect_graph.hpp"
#include <mutex>
#include <cstring>
namespace {
using namespace hyperdsp;
constexpr const char* empty_graph=R"({"schema":1,"nodes":[],"output":-1})";
struct Source {
    std::mutex mutex,control;
    std::unique_ptr<PreparedGraph> active;
    unsigned rate=0,channels=0;
    std::string accepted,error;
};
bool integer(json_t* j,int& out){
    if(!json_is_integer(j))return false;
    auto v=json_integer_value(j);if(v<-1||v>16)return false;out=int(v);return true;
}
bool keys(json_t* obj,std::initializer_list<const char*> allowed){
    if(!json_is_object(obj))return false;
    const char* key;json_t* value;
    json_object_foreach(obj,key,value){
        bool known=false;for(auto k:allowed)if(std::strcmp(k,key)==0){known=true;break;}
        if(!known)return false;
    }
    return true;
}
bool parse(const std::string& raw,GraphSpec& spec,std::string& error){
    if(raw.size()>65536){error="Graph exceeds 64 KiB";return false;}
    json_error_t detail{};
    auto root=std::unique_ptr<json_t,decltype(&json_decref)>(json_loadb(raw.data(),raw.size(),JSON_REJECT_DUPLICATES,&detail),json_decref);
    if(!root){error="Invalid JSON: "+std::string(detail.text);return false;}
    auto* j=root.get();int schema=0;
    auto* nodes=json_object_get(j,"nodes");
    if(!keys(j,{"schema","nodes","output"})||!integer(json_object_get(j,"schema"),schema)||schema!=1||
        !integer(json_object_get(j,"output"),spec.output)||!json_is_array(nodes)||json_array_size(nodes)>16){
        error="Invalid graph schema";return false;
    }
    size_t index;json_t* n;
    json_array_foreach(nodes,index,n){
        auto* effect=json_object_get(n,"effect");auto* inputs=json_object_get(n,"inputs");
        auto* params=json_object_get(n,"params");
        if(!keys(n,{"effect","inputs","params"})||!json_is_string(effect)||json_string_length(effect)>128||
            !json_is_array(inputs)||json_array_size(inputs)>8||(params&&(!json_is_object(params)||json_object_size(params)>24))){
            error="Invalid node "+std::to_string(index);return false;
        }
        GraphNode node;node.effect=json_string_value(effect);
        if(params){
            const char* key;json_t* value;
            json_object_foreach(params,key,value){
                if(!json_is_number(value)){error="Non-numeric parameter";return false;}
                node.params.emplace(key,json_number_value(value));
            }
        }
        size_t ei;json_t* e;
        json_array_foreach(inputs,ei,e){
            GraphEdge edge;
            auto* weight=json_object_get(e,"weight");
            if(!keys(e,{"source","weight"})||!integer(json_object_get(e,"source"),edge.source)||!json_is_number(weight)){
                error="Invalid edge";return false;
            }
            edge.weight=json_number_value(weight);node.inputs.push_back(edge);
        }
        spec.nodes.push_back(std::move(node));
    }
    return true;
}
void update(void* ptr,obs_data_t* settings){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    std::lock_guard control(f->control);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    const unsigned rate=ai.samples_per_sec,channels=get_audio_channels(ai.speakers);
    const std::string raw=obs_data_get_string(settings,"graph_json");
    {
        std::lock_guard lock(f->mutex);
        if(f->active&&f->accepted==raw&&f->rate==rate&&f->channels==channels){f->error.clear();return;}
    }
    GraphSpec spec;std::string error;
    std::unique_ptr<PreparedGraph> next;
    try{if(parse(raw,spec,error))next=PreparedGraph::prepare(spec,rate,channels,error);}
    catch(const std::exception& ex){error=ex.what();}
    if(!next){
        {std::lock_guard lock(f->mutex);f->error=error;}
        blog(LOG_WARNING,"[HyperDSP Graph] Rejected graph: %s",error.c_str());return;
    }
    {
        std::lock_guard lock(f->mutex);
        f->active.swap(next);f->rate=rate;f->channels=channels;f->accepted=raw;f->error.clear();
    }
}
void defaults(obs_data_t* s){obs_data_set_default_string(s,"graph_json",empty_graph);}
void* create(obs_data_t* s,obs_source_t*){auto p=std::make_unique<Source>();update(p.get(),s);return p.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Graph";}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* json=obs_properties_add_text(props,"graph_json","Effect graph (schema 1)",OBS_TEXT_MULTILINE);
    std::string status="All native effects and gain nodes. Invalid edits retain the previous active graph in this session.";
    if(p){
        auto* f=static_cast<Source*>(p);std::lock_guard lock(f->mutex);
        if(!f->error.empty())status+=" Last error: "+f->error;
        else if(f->active)status+=" Active latency: "+std::to_string(f->active->latency())+" samples.";
    }
    obs_property_set_long_description(json,status.c_str());return props;
}
obs_audio_data* process(void* ptr,obs_audio_data* audio){
    auto* f=static_cast<Source*>(ptr);if(!f||!audio)return audio;
    std::unique_lock lock(f->mutex,std::try_to_lock);
    if(!lock.owns_lock()||!f->active)return audio;
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||get_audio_channels(ai.speakers)!=f->channels)return audio;
    std::array<float*,8> p{};for(unsigned c=0;c<f->channels;++c)p[c]=reinterpret_cast<float*>(audio->data[c]);
    f->active->process(p.data(),f->channels,audio->frames);return audio;
}
}
extern "C" {
struct obs_source_info graph_filter={
    .id="hyperdsp_graph_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
