#include <obs-module.h>
#include <jansson.h>
#include "ladspa-catalog.hpp"
#include "interop/external_inputs.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <mutex>
#include <cstring>
namespace {
using namespace hyperdsp;
constexpr const char* empty_graph=R"({"schema":1,"nodes":[],"output":-1})";
struct Source {
    std::mutex mutex,control;
    std::unique_ptr<PreparedGraph> active;
    ExternalInputs external;
    unsigned rate=0,channels=0;
    std::string accepted,error;
    obs_source_t* source=nullptr;
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
    if(!keys(j,{"schema","nodes","output","controls","routes"})||!integer(json_object_get(j,"schema"),schema)||schema!=1||
        !integer(json_object_get(j,"output"),spec.output)||!json_is_array(nodes)||json_array_size(nodes)>16){
        error="Invalid graph schema";return false;
    }
    size_t index;json_t* n;
    json_array_foreach(nodes,index,n){
        auto* effect=json_object_get(n,"effect");auto* inputs=json_object_get(n,"inputs");
        auto* params=json_object_get(n,"params");
        if(!keys(n,{"effect","inputs","params"})||!json_is_string(effect)||json_string_length(effect)>4096||
            !json_is_array(inputs)||json_array_size(inputs)>8||(params&&(!json_is_object(params)||json_object_size(params)>259))){
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
    if(auto* controls=json_object_get(j,"controls")){
        if(!json_is_array(controls)||json_array_size(controls)>8){error="Invalid exposed controls";return false;}
        size_t ci;json_t* c;
        json_array_foreach(controls,ci,c){
            auto* id=json_object_get(c,"id");auto* name=json_object_get(c,"name");
            auto* initial=json_object_get(c,"default");auto* bindings=json_object_get(c,"bindings");
            if(!keys(c,{"id","name","default","bindings"})||!json_is_string(id)||!json_is_string(name)||
                !json_is_number(initial)||!json_is_array(bindings)||json_array_size(bindings)>16||
                json_string_length(id)>32||json_string_length(name)>80){error="Invalid exposed control";return false;}
            GraphControl control{json_string_value(id),json_string_value(name),json_number_value(initial),{}};
            size_t bi;json_t* b;
            json_array_foreach(bindings,bi,b){
                int node;auto* param=json_object_get(b,"parameter");
                auto* low=json_object_get(b,"low");auto* high=json_object_get(b,"high");
                if(!keys(b,{"node","parameter","low","high"})||!integer(json_object_get(b,"node"),node)||
                    !json_is_string(param)||json_string_length(param)>64||!json_is_number(low)||!json_is_number(high)){
                    error="Invalid exposed control binding";return false;
                }
                control.bindings.push_back({node,json_string_value(param),json_number_value(low),json_number_value(high)});
            }
            spec.controls.push_back(std::move(control));
        }
    }
    if(auto* routes=json_object_get(j,"routes")){
        if(!json_is_array(routes)||json_array_size(routes)>modulation_slots){error="Invalid graph routes";return false;}
        size_t ri;json_t* r;
        json_array_foreach(routes,ri,r){
            GraphRoute route;
            auto* control=json_object_get(r,"control");
            auto number=[&](const char* key,double& value){
                auto* v=json_object_get(r,key);if(!v)return true;
                if(!json_is_number(v))return false;
                value=json_number_value(v);return true;
            };
            auto text=[&](const char* key,std::string& value,size_t max){
                auto* v=json_object_get(r,key);if(!v)return true;
                if(!json_is_string(v)||json_string_length(v)>max)return false;
                value=json_string_value(v);return true;
            };
            auto* shape=json_object_get(r,"shape");
            if(!keys(r,{"control","source","shape","rate","amount","attack","release","sensitivity","channel","category"})||
                !json_is_string(control)||json_string_length(control)>32||
                !integer(json_object_get(r,"source"),route.config.source)||
                (shape&&!integer(shape,route.config.shape))||
                !json_is_number(json_object_get(r,"amount"))||
                !number("amount",route.config.amount)||!number("rate",route.config.hz)||
                !number("attack",route.config.attack)||!number("release",route.config.release)||
                !number("sensitivity",route.config.sensitivity)||!text("channel",route.channel,160)||!text("category",route.category,256)){
                error="Invalid graph modulation route";return false;
            }
            route.control=json_string_value(control);spec.routes.push_back(std::move(route));
        }
    }
    return true;
}
std::string control_key(const GraphControl& c){return "graph_control_"+c.id;}
Parameters control_values(obs_data_t* settings,const std::vector<GraphControl>& controls){
    Parameters values;
    for(auto& c:controls){
        auto key=control_key(c);
        values.emplace(c.id,obs_data_has_user_value(settings,key.c_str())?obs_data_get_double(settings,key.c_str()):c.initial);
    }
    return values;
}
void update(void* ptr,obs_data_t* settings){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    std::unique_lock control(f->control);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    const unsigned rate=ai.samples_per_sec,channels=get_audio_channels(ai.speakers);
    const std::string raw=obs_data_get_string(settings,"graph_json");
    {
        std::lock_guard lock(f->mutex);
        if(f->active&&f->accepted==raw&&f->rate==rate&&f->channels==channels){
            auto values=control_values(settings,f->active->controls());
            if(!f->active->set_controls(values,f->error))blog(LOG_WARNING,"[HyperDSP Graph] Rejected control values: %s",f->error.c_str());
            return;
        }
    }
    GraphSpec spec;std::string error;
    std::unique_ptr<PreparedGraph> next;
    ExternalInputs external;
    try{if(parse(raw,spec,error)){
        std::vector<std::string> allowed;
        if(std::any_of(spec.nodes.begin(),spec.nodes.end(),[](auto& n){return n.effect.starts_with("ladspa:");}))
            for(auto& e:read_catalog())allowed.push_back("ladspa:"+e.id());
        next=PreparedGraph::prepare(spec,rate,channels,error,allowed,control_values(settings,spec.controls));
        if(next)for(unsigned i=0;i<spec.routes.size();++i){
            const auto& r=spec.routes[i];external.configure(i,r.config.source,r.channel.c_str(),r.category.c_str());
        }
    }}
    catch(const std::exception& ex){next.reset();error=ex.what();}
    if(!next){
        {std::lock_guard lock(f->mutex);f->error=error;}
        blog(LOG_WARNING,"[HyperDSP Graph] Rejected graph: %s",error.c_str());return;
    }
    for(auto& c:spec.controls)obs_data_set_default_double(settings,control_key(c).c_str(),c.initial);
    bool refresh=false;
    {
        std::lock_guard lock(f->mutex);
        refresh=bool(f->active)&&f->accepted!=raw;
        f->active.swap(next);std::swap(f->external,external);f->rate=rate;f->channels=channels;f->accepted=raw;f->error.clear();
    }
    control.unlock();
    if(refresh&&f->source)obs_source_update_properties(f->source);
}
void defaults(obs_data_t* s){obs_data_set_default_string(s,"graph_json",empty_graph);}
void* create(obs_data_t* s,obs_source_t* source){auto p=std::make_unique<Source>();p->source=source;update(p.get(),s);return p.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Graph";}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* json=obs_properties_add_text(props,"graph_json","Effect graph (schema 1)",OBS_TEXT_MULTILINE);
    std::string status="All native effects, gain and explicitly scanned trusted LADSPA nodes. External latency is unknown. Invalid edits retain the previous active graph in this session.";
    if(p){
        auto* f=static_cast<Source*>(p);std::lock_guard lock(f->mutex);
        if(f->active)for(auto& c:f->active->controls()){
            auto key=control_key(c);
            auto* slider=obs_properties_add_float_slider(props,key.c_str(),c.name.c_str(),0,1,.01);
            obs_property_set_long_description(slider,"Exposed graph control: mapped to one or more internal parameters. Reactive routes add to this base value. No graph rebuild.");
        }
        if(!f->error.empty())status+=" Last error: "+f->error;
        else if(f->active)status+=" Known fixed latency: "+std::to_string(f->active->latency())+" samples."+(f->active->latency_known()?"":" Additional LADSPA latency is unknown and uncompensated.");
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
    f->active->external_values(f->external.snapshot());
    f->active->process(p.data(),f->channels,audio->frames);return audio;
}
}
extern "C" {
struct obs_source_info graph_filter={
    .id="hyperdsp_graph_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process
};
}
