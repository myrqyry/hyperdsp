#pragma once
#include "effect_graph.hpp"
#include <jansson.h>
#include <cstring>
namespace hyperdsp::graph_json {
inline bool integer(json_t* j,int& out){
    if(!json_is_integer(j))return false;
    auto v=json_integer_value(j);if(v<-1||v>16)return false;out=int(v);return true;
}
inline bool keys(json_t* obj,std::initializer_list<const char*> allowed){
    if(!json_is_object(obj))return false;
    const char* key;json_t* value;
    json_object_foreach(obj,key,value){
        bool known=false;for(auto k:allowed)if(std::strcmp(k,key)==0){known=true;break;}
        if(!known)return false;
    }
    return true;
}
inline bool parse(const std::string& raw,GraphSpec& spec,std::string& error){
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
}
