#include <obs-module.h>
#include <jansson.h>
#include "ladspa-catalog.hpp"
#include "interop/external_inputs.hpp"
#include "framework/dsp/graph_json.hpp"
#include <mutex>
#include <util/platform.h>
#include "framework/dsp/graph_monitor.hpp"
#include <cstring>
namespace {
using namespace hyperdsp;
constexpr const char* empty_graph=R"({"schema":1,"nodes":[],"output":-1})";
struct Source {
    std::mutex mutex,control;
    std::unique_ptr<PreparedGraph> active;
    ExternalInputs external;
    GraphMonitor monitor;
    unsigned rate=0,channels=0;
    std::string accepted,error;
    obs_source_t* source=nullptr;
};
using graph_json::integer;
using graph_json::keys;
using graph_json::parse;
std::string control_key(const GraphControl& c){return "graph_control_"+c.id;}
Parameters control_values(obs_data_t* settings,const std::vector<GraphControl>& controls){
    Parameters values;
    for(auto& c:controls){
        auto key=control_key(c);
        values.emplace(c.id,obs_data_has_user_value(settings,key.c_str())?obs_data_get_double(settings,key.c_str()):c.initial);
    }
    return values;
}
constexpr const char* recovery_key="graph_last_good";
struct Candidate {
    std::unique_ptr<PreparedGraph> graph;
    ExternalInputs external;
};
Candidate prepare(const std::string& raw,unsigned rate,unsigned channels,obs_data_t* settings,
                  std::string& error,const Parameters* recovered=nullptr){
    Candidate candidate;
    GraphSpec spec;
    try{
        if(!parse(raw,spec,error))return candidate;
        std::vector<std::string> allowed;
        if(std::any_of(spec.nodes.begin(),spec.nodes.end(),[](auto& n){return n.effect.starts_with("ladspa:");}))
            for(auto& e:read_catalog())allowed.push_back("ladspa:"+e.id());
        if(recovered&&recovered->size()!=spec.controls.size()){
            error="Recovery control set does not match graph";return candidate;
        }
        auto values=recovered?*recovered:control_values(settings,spec.controls);
        candidate.graph=PreparedGraph::prepare(spec,rate,channels,error,allowed,values);
        if(candidate.graph)for(unsigned i=0;i<spec.routes.size();++i){
            const auto& r=spec.routes[i];
            candidate.external.configure(i,r.config.source,r.channel.c_str(),r.category.c_str());
        }
    }catch(const std::exception& ex){candidate.graph.reset();error=ex.what();}
    return candidate;
}
bool read_recovery(obs_data_t* settings,std::string& raw,Parameters& values,std::string& error){
    auto* snapshot=obs_data_get_obj(settings,recovery_key);
    if(!snapshot){error="No saved accepted graph";return false;}
    std::unique_ptr<obs_data_t,decltype(&obs_data_release)> hold(snapshot,obs_data_release);
    const char* serialized=obs_data_get_json(snapshot);
    if(!serialized||std::strlen(serialized)>400000){error="Invalid recovery size";return false;}
    auto root=std::unique_ptr<json_t,decltype(&json_decref)>(json_loads(serialized,JSON_REJECT_DUPLICATES,nullptr),json_decref);
    auto* j=root.get();int schema=0;
    if(!j||!keys(j,{"schema","graph","values"})||!integer(json_object_get(j,"schema"),schema)||schema!=1){
        error="Invalid recovery schema";return false;
    }
    auto* graph=json_object_get(j,"graph");auto* controls=json_object_get(j,"values");
    if(!json_is_string(graph)||json_string_length(graph)>65536||!json_is_object(controls)||json_object_size(controls)>8){
        error="Invalid recovery graph or values";return false;
    }
    const char* key;json_t* value;
    json_object_foreach(controls,key,value){
        if(!graph_control_id(key)||!json_is_number(value)||!std::isfinite(json_number_value(value))||
           json_number_value(value)<0||json_number_value(value)>1){
            error="Invalid recovery control";return false;
        }
        values.emplace(key,json_number_value(value));
    }
    raw=json_string_value(graph);return true;
}
void save(void* ptr,obs_data_t* settings){
    auto* f=static_cast<Source*>(ptr);if(!f||!settings)return;
    std::string graph;Parameters values;
    {
        std::lock_guard lock(f->mutex);
        if(!f->active)return; // preserve an existing recovery record if neither graph could load
        graph=f->accepted;values=f->active->base_values();
    }
    auto* snapshot=obs_data_create();auto* controls=obs_data_create();
    obs_data_set_int(snapshot,"schema",1);obs_data_set_string(snapshot,"graph",graph.c_str());
    for(auto& [key,value]:values)obs_data_set_double(controls,key.c_str(),value);
    obs_data_set_obj(snapshot,"values",controls);obs_data_release(controls);
    obs_data_set_obj(settings,recovery_key,snapshot);obs_data_release(snapshot);
}
void update(void* ptr,obs_data_t* settings){
    auto* f=static_cast<Source*>(ptr);if(!f)return;
    std::unique_lock control(f->control);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    const unsigned rate=ai.samples_per_sec,channels=get_audio_channels(ai.speakers);
    const std::string raw=obs_data_get_string(settings,"graph_json");
    bool has_active=false;
    {
        std::lock_guard lock(f->mutex);
        has_active=bool(f->active);
        if(f->active&&f->accepted==raw&&f->rate==rate&&f->channels==channels){
            auto values=control_values(settings,f->active->controls());
            if(!f->active->set_controls(values,f->error))blog(LOG_WARNING,"[HyperDSP Graph] Rejected control values: %s",f->error.c_str());
            return;
        }
    }
    std::string accepted=raw,error;
    auto next=prepare(raw,rate,channels,settings,error);
    bool recovered=false;
    if(!next.graph&&!has_active){
        std::string recovery_error,recovery_graph;
        Parameters values;
        if(read_recovery(settings,recovery_graph,values,recovery_error)){
            next=prepare(recovery_graph,rate,channels,settings,recovery_error,&values);
            if(next.graph){accepted=std::move(recovery_graph);recovered=true;}
        }
        if(recovered)error="Recovered last saved accepted graph. Rejected draft: "+error;
        else error+="; recovery unavailable: "+recovery_error;
    }
    if(!next.graph){
        {std::lock_guard lock(f->mutex);f->error=error;}
        blog(LOG_WARNING,"[HyperDSP Graph] Rejected graph: %s",error.c_str());return;
    }
    for(auto& c:next.graph->controls())obs_data_set_default_double(settings,control_key(c).c_str(),c.initial);
    bool refresh=false;
    {
        std::lock_guard lock(f->mutex);
        refresh=bool(f->active)&&f->accepted!=accepted;
        f->active.swap(next.graph);std::swap(f->external,next.external);f->monitor.invalidate();
        f->rate=rate;f->channels=channels;f->accepted=std::move(accepted);f->error=std::move(error);
    }
    if(recovered)blog(LOG_WARNING,"[HyperDSP Graph] Recovered last saved accepted graph; draft left unchanged");
    control.unlock();
    if(refresh&&f->source)obs_source_update_properties(f->source);
}
// Explicit builder transaction: prepare once, publish only on success.
void builder_apply(void* ptr,calldata_t* data){
    auto* f=static_cast<Source*>(ptr);
    calldata_set_bool(data,"ok",false);
    const char* raw_ptr=nullptr;
    if(!f||!f->source||!calldata_get_string(data,"graph",&raw_ptr)||!raw_ptr){
        calldata_set_string(data,"status","Missing graph");return;
    }
    std::string raw(raw_ptr),error;
    bool validate_only=calldata_bool(data,"validate_only");
    std::unique_lock control(f->control);
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai)){calldata_set_string(data,"status","Audio format unavailable");return;}
    auto* settings=obs_source_get_settings(f->source);
    if(!settings){calldata_set_string(data,"status","Settings unavailable");return;}
    std::unique_ptr<obs_data_t,decltype(&obs_data_release)> hold(settings,obs_data_release);
    auto next=prepare(raw,ai.samples_per_sec,get_audio_channels(ai.speakers),settings,error);
    if(!next.graph){calldata_set_string(data,"status",error.c_str());return;}
    std::string status="Validated · "+std::to_string(next.graph->latency())+" samples known latency";
    if(!next.graph->latency_known())status+="; external latency unknown";
    if(!validate_only){
        for(auto& c:next.graph->controls())obs_data_set_default_double(settings,control_key(c).c_str(),c.initial);
        obs_data_set_string(settings,"graph_json",raw.c_str());
        {
            std::lock_guard lock(f->mutex);
            f->active.swap(next.graph);std::swap(f->external,next.external);f->monitor.invalidate();
            f->rate=ai.samples_per_sec;f->channels=get_audio_channels(ai.speakers);
            f->accepted=raw;f->error.clear();
        }
        control.unlock();
        obs_source_update(f->source,settings);
        obs_source_update_properties(f->source);
        status="Applied · "+status.substr(std::string("Validated · ").size());
    }
    calldata_set_bool(data,"ok",true);calldata_set_string(data,"status",status.c_str());
}
void defaults(obs_data_t* s){obs_data_set_default_string(s,"graph_json",empty_graph);}
void monitor_read(void* ptr,calldata_t* data){
    auto* f=static_cast<Source*>(ptr);calldata_set_bool(data,"available",false);
    void* raw=nullptr;
    if(!f||!calldata_get_ptr(data,"frame",&raw)||!raw)return;
    auto& frame=*static_cast<GraphMonitorFrame*>(raw);
    if(frame.version!=1||frame.size!=sizeof(GraphMonitorFrame))return;
    if(!obs_source_enabled(f->source)){f->monitor.invalidate();return;}
    calldata_set_bool(data,"available",f->monitor.read(frame,os_gettime_ns()));
}
void monitor_read_v2(void* ptr,calldata_t* data){
    auto* f=static_cast<Source*>(ptr);calldata_set_bool(data,"available",false);
    void* raw=nullptr;
    if(!f||!calldata_get_ptr(data,"frame",&raw)||!raw)return;
    auto& frame=*static_cast<GraphMonitorFrameV2*>(raw);
    if(frame.version!=2||frame.size!=sizeof(GraphMonitorFrameV2))return;
    if(!obs_source_enabled(f->source)){f->monitor.invalidate();return;}
    calldata_set_bool(data,"available",f->monitor.read(frame,os_gettime_ns()));
}
void* create(obs_data_t* s,obs_source_t* source){auto p=std::make_unique<Source>();p->source=source;proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_graph_apply_v1(in string graph, in bool validate_only, out bool ok, out string status)",builder_apply,p.get());proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_graph_monitor_v1(in ptr frame, out bool available)",monitor_read,p.get());proc_handler_add(obs_source_get_proc_handler(source),"void hyperdsp_graph_monitor_v2(in ptr frame, out bool available)",monitor_read_v2,p.get());update(p.get(),s);return p.release();}
void destroy(void* p){delete static_cast<Source*>(p);}
const char* name(void*){return "HyperDSP Graph";}
bool restore_working_graph(obs_properties_t*,obs_property_t*,void* ptr){
    auto* f=static_cast<Source*>(ptr);if(!f||!f->source)return false;
    std::string graph;Parameters values;
    {
        std::lock_guard lock(f->mutex);
        if(!f->active)return false;
        graph=f->accepted;values=f->active->base_values();
    }
    auto* settings=obs_source_get_settings(f->source);
    if(!settings)return false;
    obs_data_set_string(settings,"graph_json",graph.c_str());
    for(auto& [id,value]:values)obs_data_set_double(settings,("graph_control_"+id).c_str(),value);
    obs_source_update(f->source,settings);obs_data_release(settings);
    return true;
}
obs_properties_t* properties(void* p){
    auto* props=obs_properties_create();
    auto* json=obs_properties_add_text(props,"graph_json","Effect graph (schema 1)",OBS_TEXT_MULTILINE);
    std::string status="All native effects, gain and explicitly scanned trusted LADSPA nodes. External latency is unknown. Invalid edits retain the active graph. Scene saves preserve its last accepted definition and base controls for recovery.";
    if(p){
        auto* f=static_cast<Source*>(p);std::lock_guard lock(f->mutex);
        if(f->active){
            auto* restore=obs_properties_add_button2(props,"restore_working_graph","Restore working graph",restore_working_graph,f);
            obs_property_set_long_description(restore,"Replace the draft and its exposed control values with the currently accepted graph. Does not rebuild an unchanged graph.");
        }
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
    if(!lock.owns_lock()||!f->active){f->monitor.invalidate();return audio;}
    obs_audio_info ai{};
    if(!obs_get_audio_info(&ai)||ai.samples_per_sec!=f->rate||get_audio_channels(ai.speakers)!=f->channels){f->monitor.invalidate();return audio;}
    std::array<float*,8> p{};for(unsigned c=0;c<f->channels;++c)p[c]=reinterpret_cast<float*>(audio->data[c]);
    const float* right=f->channels==2?p[1]:nullptr;
    bool capture=f->monitor.begin(p[0],audio->frames,f->rate,os_gettime_ns(),right);
    f->active->external_values(f->external.snapshot());
    f->active->process(p.data(),f->channels,audio->frames);
    if(capture){
        std::array<GraphMonitorControl,8> controls{};
        unsigned count=std::min<unsigned>(f->active->controls().size(),8);
        for(unsigned i=0;i<count;++i){
            auto& id=f->active->controls()[i].id;
            std::copy_n(id.data(),std::min<size_t>(id.size(),32),controls[i].id.data());
            auto& name=f->active->controls()[i].name;
            std::copy_n(name.data(),std::min<size_t>(name.size(),80),controls[i].name.data());
            controls[i].base=f->active->base_value(i);controls[i].effective=f->active->effective_value(i);
        }
        f->monitor.end(p[0],audio->frames,os_gettime_ns(),controls,count,right);
    }
    return audio;
}
}
extern "C" {
struct obs_source_info graph_filter={
    .id="hyperdsp_graph_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.filter_audio=process,.save=save
};
}
