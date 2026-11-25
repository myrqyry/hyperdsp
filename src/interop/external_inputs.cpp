#include "external_inputs.hpp"
#include <callback/proc.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include "sight/sight_snapshot_v1.h"
#include "sight/sight_motion_v1.h"
namespace hyperdsp {
using Clock=std::chrono::steady_clock;
static uint64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
struct ExternalSubscription {
    const int source;
    const std::string channel,category;
    std::atomic<uint64_t> snapshot{0};
    ExternalSubscription(int s,std::string c,std::string k):source(s),channel(std::move(c)),category(std::move(k)){}
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
namespace {
std::mutex guard;
std::condition_variable wake;
std::vector<std::weak_ptr<ExternalSubscription>> jobs;
std::thread worker;
bool stopping=false;
bool producer(obs_source_t* s,int source){
    const char* id=s?obs_source_get_unversioned_id(s):nullptr;
    return id&&!strcmp(id,source==6?"yamnet_classifier_source":"sight_u2net_filter");
}
struct Search{std::string uuid;int source;obs_source_t* ref=nullptr;};
void find_filter(obs_source_t*,obs_source_t* filter,void* p){
    auto& f=*static_cast<Search*>(p);
    const char* id=obs_source_get_uuid(filter);
    if(!f.ref&&id&&f.uuid==id&&producer(filter,f.source)&&obs_source_enabled(filter)&&!obs_source_removed(filter))
        f.ref=obs_source_get_ref(filter);
}
obs_source_t* resolve(const ExternalSubscription& sub){
    auto slash=sub.channel.find('/');
    if(slash==std::string::npos)return nullptr;
    auto* parent=obs_get_source_by_uuid(sub.channel.substr(0,slash).c_str());
    if(!parent)return nullptr;
    Search f{sub.channel.substr(slash+1),sub.source};
    if(obs_source_enabled(parent)&&!obs_source_removed(parent))obs_source_enum_filters(parent,find_filter,&f);
    obs_source_release(parent);
    return f.ref;
}
float read_score(const ExternalSubscription& sub){
    auto* ref=resolve(sub);if(!ref)return 0;
    calldata_t args{};
    calldata_set_string(&args,"channel",sub.channel.c_str());
    calldata_set_string(&args,"stream","category");
    calldata_set_string(&args,"category",sub.category.c_str());
    auto* handler=obs_source_get_proc_handler(ref);
    float score=0;
    if(handler&&proc_handler_call(handler,"sound_signal_read_v1",&args)){
        const char* kind=calldata_string(&args,"kind");
        double value=calldata_float(&args,"score");
        if(kind&&!strcmp(kind,"classification")&&calldata_bool(&args,"connected")&&
            calldata_bool(&args,"available")&&std::isfinite(value))score=float(std::clamp(value,0.,1.));
    }
    calldata_free(&args);obs_source_release(ref);return score;
}
// The public Sight receiver is a procedure-only private instance here.
// Never attach it to a scene or request rendering/inference.
obs_source_t* tracking_reader(){
    if(!obs_source_get_display_name("sight_tracking_receiver_filter"))return nullptr;
    return obs_source_create_private("sight_tracking_receiver_filter","HyperDSP tracking reader",nullptr);
}
obs_data_array_t* tracking_inventory(obs_source_t* reader){
    if(!reader)return nullptr;
    auto* data=obs_data_create();
    calldata_t call{};calldata_set_ptr(&call,"inventory",data);
    auto* h=obs_source_get_proc_handler(reader);
    const bool ok=h&&proc_handler_call(h,"sight_tracking_list_v1",&call)&&obs_data_get_int(data,"version")==1;
    auto* rows=ok?obs_data_get_array(data,"channels"):nullptr;
    calldata_free(&call);obs_data_release(data);return rows;
}
float tracking_value(const ExternalSubscription& sub){
    auto* reader=tracking_reader();if(!reader)return 0;
    auto* rows=tracking_inventory(reader);
    unsigned matches=0;bool allowed=false;
    if(rows){
        for(size_t i=0;i<obs_data_array_count(rows);++i){
            auto* r=obs_data_array_item(rows,i);
            if(sub.channel==obs_data_get_string(r,"channel")){
                ++matches;
                allowed=obs_data_get_bool(r,"enabled")&&!obs_data_get_bool(r,"ambiguous")&&obs_data_get_int(r,"kind")==SIGHT_SNAPSHOT_HAND;
            }
            obs_data_release(r);
        }
        obs_data_array_release(rows);
    }
    float value=0;
    if(matches==1&&allowed){
        sight_snapshot_v1 snap{};snap.abi_version=SIGHT_SNAPSHOT_ABI_V1;snap.struct_size=sizeof(snap);
        calldata_t call{};calldata_set_string(&call,"channel",sub.channel.c_str());
        calldata_set_int(&call,"max_age_ms",500);calldata_set_ptr(&call,"snapshot",&snap);
        auto* h=obs_source_get_proc_handler(reader);
        if(h&&proc_handler_call(h,"sight_tracking_read_v1",&call)&&calldata_bool(&call,"available")&&
            snap.abi_version==SIGHT_SNAPSHOT_ABI_V1&&snap.struct_size==sizeof(snap)&&snap.sequence&&
            snap.kind==SIGHT_SNAPSHOT_HAND&&snap.source_width&&snap.source_height&&
            snap.point_count<=SIGHT_SNAPSHOT_MAX_POINTS&&snap.hands_count>0&&snap.hands_count<=SIGHT_SNAPSHOT_MAX_HANDS&&
            snap.blendshape_count<=SIGHT_SNAPSHOT_MAX_BLENDSHAPES){
            // First reported hand, not a stable handedness identity.
            const auto& wrist=snap.hands[0][0];
            const float raw=sub.source==8?wrist.x:sub.source==9?wrist.y:snap.pinch[0];
            if(std::isfinite(raw)&&std::isfinite(wrist.confidence)&&wrist.confidence>0&&
                (sub.source!=10||(std::isfinite(snap.pinch[0])&&snap.pinch[0]>=0&&snap.pinch[0]<=1)))
                value=std::clamp(raw,0.f,1.f);
        }
        calldata_free(&call);
    }
    obs_source_release(reader);return value;
}
float motion_value(const ExternalSubscription& sub){
    auto* ref=resolve(sub);if(!ref)return 0;
    sight_motion_v1 snap{};snap.abi_version=SIGHT_MOTION_ABI_V1;snap.struct_size=sizeof(snap);
    calldata_t call{};calldata_set_int(&call,"max_age_ms",500);calldata_set_ptr(&call,"snapshot",&snap);
    auto* h=obs_source_get_proc_handler(ref);
    bool ok=h&&proc_handler_call(h,"sight_motion_read_v1",&call)&&calldata_bool(&call,"available");
    auto w=obs_source_get_base_width(ref),height=obs_source_get_base_height(ref);
    calldata_free(&call);obs_source_release(ref);
    return ok&&snap.abi_version==SIGHT_MOTION_ABI_V1&&snap.struct_size==sizeof(snap)&&snap.sequence&&
        w&&height&&snap.source_width==w&&snap.source_height==height&&
        std::isfinite(snap.energy)&&snap.energy>=0&&snap.energy<=1?snap.energy:0.f;
}
void poll(){
    std::unique_lock lock(guard);
    while(!stopping){
        std::vector<std::shared_ptr<ExternalSubscription>> work;
        for(auto it=jobs.begin();it!=jobs.end();){
            if(auto job=it->lock()){work.push_back(std::move(job));++it;}
            else it=jobs.erase(it);
        }
        if(work.empty()){wake.wait(lock,[&]{return stopping||!jobs.empty();});continue;}
        lock.unlock();
        for(auto& job:work){
            auto start=now_ms();
            auto score=job->source==6?read_score(*job):job->source==7?motion_value(*job):tracking_value(*job);
            // A stalled producer cannot refresh an old read.
            auto packed=(start<<16)|uint64_t(std::lround(score*65535.f));
            job->snapshot.store(packed,std::memory_order_release);
        }
        work.clear();
        lock.lock();
        wake.wait_for(lock,std::chrono::milliseconds(100));
    }
}
struct Choices {obs_property_t* prop;std::string saved;bool found=false;int source=6;};
void list_filter(obs_source_t* parent,obs_source_t* filter,void* p){
    auto& c=*static_cast<Choices*>(p);
    if(!producer(filter,c.source)||obs_source_removed(filter))return;
    const char* a=obs_source_get_uuid(parent),*b=obs_source_get_uuid(filter);
    if(!a||!b)return;
    std::string key=std::string(a)+"/"+b;
    std::string label=std::string(obs_source_get_name(parent))+" / "+obs_source_get_name(filter);
    if(!obs_source_enabled(parent)||!obs_source_enabled(filter))label+=" (disabled)";
    obs_property_list_add_string(c.prop,label.c_str(),key.c_str());
    c.found=c.found||key==c.saved;
}
bool list_source(void* p,obs_source_t* source){
    if(!obs_source_removed(source))obs_source_enum_filters(source,list_filter,p);
    return true;
}
}
void start_external_polling(){
    std::lock_guard lock(guard);
    if(!worker.joinable()){stopping=false;worker=std::thread(poll);}
}
void stop_external_polling(){
    {std::lock_guard lock(guard);stopping=true;}
    wake.notify_all();
    if(worker.joinable())worker.join();
    std::lock_guard lock(guard);jobs.clear();
}
void ExternalInputs::configure(unsigned slot,int source,const char* channel,const char* category){
    if(slot>=slots_.size())return;
    auto& s=slots_[slot];
    std::string c=channel?channel:"",k=category?category:"";
    if(source<6||source>10||c.empty()||c.size()>(source>=8?96u:160u)||(source==6&&(k.empty()||k.size()>256))){s.reset();return;}
    if(s&&s->source==source&&s->channel==c&&s->category==k)return;
    s=std::make_shared<ExternalSubscription>(source,std::move(c),std::move(k));
    {std::lock_guard lock(guard);jobs.emplace_back(s);}
    wake.notify_one();
}
std::array<float,modulation_slots> ExternalInputs::snapshot()const{
    std::array<float,modulation_slots> result{};
    if(std::none_of(slots_.begin(),slots_.end(),[](const auto& s){return bool(s);}))return result;
    const auto now=now_ms();
    for(unsigned i=0;i<slots_.size();++i)if(slots_[i]){
        const auto v=slots_[i]->snapshot.load(std::memory_order_acquire),stamp=v>>16;
        if(stamp&&now>=stamp&&now-stamp<=500)result[i]=float(v&65535)/65535.f;
    }
    return result;
}
void sound_channel_choices(obs_property_t* p,const char* saved){
    if(!p)return;
    obs_property_list_clear(p);obs_property_list_add_string(p,"Choose Sound classifier","");
    Choices c{p,saved?saved:""};
    obs_enum_sources(list_source,&c);
    if(!c.saved.empty()&&!c.found)obs_property_list_add_string(p,"Unavailable saved classifier",c.saved.c_str());
}
void sight_channel_choices(obs_property_t* p,const char* saved,int source){
    if(!p)return;
    obs_property_list_clear(p);
    obs_property_list_add_string(p,source==7?"Choose Sight motion producer":"Choose Sight hand channel","");
    Choices c{p,saved?saved:"",false,source};
    if(source==7)obs_enum_sources(list_source,&c);
    else {
        auto* reader=tracking_reader();
        auto* rows=tracking_inventory(reader);
        if(rows){
            for(size_t i=0;i<obs_data_array_count(rows);++i){
                auto* r=obs_data_array_item(rows,i);
                const char* key=obs_data_get_string(r,"channel");
                if(*key&&strlen(key)<=96&&obs_data_get_int(r,"kind")==SIGHT_SNAPSHOT_HAND){
                    size_t index=obs_property_list_add_string(p,obs_data_get_string(r,"label"),key);
                    if(obs_data_get_bool(r,"ambiguous")||!obs_data_get_bool(r,"enabled"))obs_property_list_item_disable(p,index,true);
                    c.found=c.found||c.saved==key;
                }
                obs_data_release(r);
            }
            obs_data_array_release(rows);
        }
        if(reader)obs_source_release(reader);
    }
    if(!c.saved.empty()&&!c.found)obs_property_list_add_string(p,"Unavailable saved Sight input",c.saved.c_str());
}
}
