#include "sound_inputs.hpp"
#include <callback/proc.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
namespace hyperdsp {
using Clock=std::chrono::steady_clock;
static uint64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
struct SoundSubscription {
    const std::string channel,category;
    std::atomic<uint64_t> snapshot{0};
    SoundSubscription(std::string c,std::string k):channel(std::move(c)),category(std::move(k)){}
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
namespace {
std::mutex guard;
std::condition_variable wake;
std::vector<std::weak_ptr<SoundSubscription>> jobs;
std::thread worker;
bool stopping=false;
bool classifier(obs_source_t* s){
    const char* id=s?obs_source_get_unversioned_id(s):nullptr;
    return id&&!strcmp(id,"yamnet_classifier_source");
}
struct Search{std::string uuid;obs_source_t* ref=nullptr;};
void find_filter(obs_source_t*,obs_source_t* filter,void* p){
    auto& f=*static_cast<Search*>(p);
    const char* id=obs_source_get_uuid(filter);
    if(!f.ref&&id&&f.uuid==id&&classifier(filter)&&obs_source_enabled(filter)&&!obs_source_removed(filter))
        f.ref=obs_source_get_ref(filter);
}
float read_score(const SoundSubscription& sub){
    auto slash=sub.channel.find('/');
    if(slash==std::string::npos)return 0;
    auto* parent=obs_get_source_by_uuid(sub.channel.substr(0,slash).c_str());
    if(!parent)return 0;
    Search f{sub.channel.substr(slash+1)};
    if(obs_source_enabled(parent)&&!obs_source_removed(parent))obs_source_enum_filters(parent,find_filter,&f);
    obs_source_release(parent);
    if(!f.ref)return 0;
    calldata_t args{};
    calldata_set_string(&args,"channel",sub.channel.c_str());
    calldata_set_string(&args,"stream","category");
    calldata_set_string(&args,"category",sub.category.c_str());
    auto* handler=obs_source_get_proc_handler(f.ref);
    float score=0;
    if(handler&&proc_handler_call(handler,"sound_signal_read_v1",&args)){
        const char* kind=calldata_string(&args,"kind");
        double value=calldata_float(&args,"score");
        if(kind&&!strcmp(kind,"classification")&&calldata_bool(&args,"connected")&&
            calldata_bool(&args,"available")&&std::isfinite(value))score=float(std::clamp(value,0.,1.));
    }
    calldata_free(&args);obs_source_release(f.ref);return score;
}
void poll(){
    std::unique_lock lock(guard);
    while(!stopping){
        std::vector<std::shared_ptr<SoundSubscription>> work;
        for(auto it=jobs.begin();it!=jobs.end();){
            if(auto job=it->lock()){work.push_back(std::move(job));++it;}
            else it=jobs.erase(it);
        }
        if(work.empty()){wake.wait(lock,[&]{return stopping||!jobs.empty();});continue;}
        lock.unlock();
        for(auto& job:work){
            auto start=now_ms();
            auto score=read_score(*job);
            // A stalled producer cannot refresh an old read.
            auto packed=(start<<16)|uint64_t(std::lround(score*65535.f));
            job->snapshot.store(packed,std::memory_order_release);
        }
        work.clear();
        lock.lock();
        wake.wait_for(lock,std::chrono::milliseconds(100));
    }
}
struct Choices {obs_property_t* prop;std::string saved;bool found=false;};
void list_filter(obs_source_t* parent,obs_source_t* filter,void* p){
    if(!classifier(filter)||obs_source_removed(filter))return;
    auto& c=*static_cast<Choices*>(p);
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
void start_sound_polling(){
    std::lock_guard lock(guard);
    if(!worker.joinable()){stopping=false;worker=std::thread(poll);}
}
void stop_sound_polling(){
    {std::lock_guard lock(guard);stopping=true;}
    wake.notify_all();
    if(worker.joinable())worker.join();
    std::lock_guard lock(guard);jobs.clear();
}
void SoundInputs::configure(unsigned slot,int source,const char* channel,const char* category){
    if(slot>=slots_.size())return;
    auto& s=slots_[slot];
    std::string c=channel?channel:"",k=category?category:"";
    if(source!=6||c.empty()||c.size()>160||k.empty()||k.size()>256){s.reset();return;}
    if(s&&s->channel==c&&s->category==k)return;
    s=std::make_shared<SoundSubscription>(std::move(c),std::move(k));
    {std::lock_guard lock(guard);jobs.emplace_back(s);}
    wake.notify_one();
}
std::array<float,modulation_slots> SoundInputs::snapshot()const{
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
}
