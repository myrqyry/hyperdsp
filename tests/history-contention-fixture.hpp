#pragma once
#include "latency-contention-fixture.hpp"
#include <vector>
#include <string>

namespace history_contention {
struct Probe {
    std::vector<std::array<float,2>> reference;
    size_t samples=0,errors=0;
    bool compare=false;
    double energy=0;
};
inline Probe* active=nullptr;
inline const char* name(void*){return "DSP history contention probe";}
inline void* create(obs_data_t*,obs_source_t*){return active;}
inline void destroy(void*){}
inline obs_audio_data* inspect(void* ptr,obs_audio_data* a){
    auto& p=*static_cast<Probe*>(ptr);
    if(!a)return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    for(unsigned j=0;j<a->frames;++j,++p.samples){
        if(!l||!r||!std::isfinite(l[j])||!std::isfinite(r[j])){++p.errors;continue;}
        if(!p.compare){p.reference.push_back({l[j],r[j]});p.energy+=double(l[j])*l[j];}
        else if(p.samples>=p.reference.size()||
            std::abs(l[j]-p.reference[p.samples][0])>1e-6f||
            std::abs(r[j]-p.reference[p.samples][1])>1e-6f)++p.errors;
    }
    return a;
}
// Compare two freshly constructed real OBS filters: identical audio, identical
// active controls, but one receives concurrent changes to a mode-inactive control.
// The input alternates signal/silence to exercise state and effect tails.
inline bool verify(obs_source_t* source,const char* id,obs_data_t* settings,const char* key,double a,double b,const char* replacement_label=nullptr){
    constexpr unsigned blocks=1000,frames=480;
    Probe probe;probe.reference.reserve(blocks*frames);active=&probe;
    obs_source_info info{.id="hyperdsp_history_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
        .get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    static bool registered=false;
    if(!registered){obs_register_source(&info);registered=true;}
    auto* watcher=obs_source_create_private(info.id,"history probe",nullptr);
    if(!watcher){active=nullptr;return false;}
    obs_source_filter_add(source,watcher);
    unsigned updates=0,property_reads=0;bool metadata_ok=true;
    const std::string initial_label=obs_data_get_string(settings,"label");
    auto run=[&](bool concurrent){
        obs_data_set_double(settings,key,a);
        if(replacement_label)obs_data_set_string(settings,"label",initial_label.c_str());
        auto* filter=obs_source_create_private(id,"history test",settings);
        if(!filter)return false;
        obs_source_filter_add(source,filter);obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
        probe.compare=concurrent;probe.samples=0;
        std::atomic<bool> ready{false},stop{false};std::thread writer;
        if(concurrent){
            writer=std::thread([&]{
                while(!stop.load()){
                    obs_data_set_double(settings,key,updates%2?a:b);
                    if(replacement_label)obs_data_set_string(settings,"label",updates%2?initial_label.c_str():replacement_label);
                    obs_source_update(filter,settings);++updates;ready=true;
                    if(replacement_label&&updates%16==0){
                        auto* props=obs_source_properties(filter);
                        metadata_ok=metadata_ok&&props&&obs_properties_get(props,"port_0");
                        if(props)obs_properties_destroy(props);
                        ++property_reads;
                    }
                }
            });
            while(!ready.load())std::this_thread::yield();
        }
        std::array<float,frames> l{},r{};auto start=os_gettime_ns();
        for(unsigned k=0;k<blocks;++k){
            for(unsigned j=0;j<frames;++j){
                l[j]=k%40<10?latency_contention::sample(uint64_t(k)*frames+j):0;r[j]=-.4f*l[j];
            }
            obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(l.data());audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
            audio.frames=frames;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=48000;
            audio.timestamp=start+uint64_t(k)*10000000ULL;obs_source_output_audio(source,&audio);
        }
        stop=true;if(writer.joinable())writer.join();
        obs_source_filter_remove(source,filter);obs_source_release(filter);
        return probe.samples==uint64_t(blocks)*frames;
    };
    bool ok=run(false)&&run(true)&&probe.reference.size()==uint64_t(blocks)*frames&&probe.energy>1&&updates>10&&!probe.errors&&metadata_ok&&(!replacement_label||property_reads>0);
    obs_source_filter_remove(source,watcher);obs_source_release(watcher);active=nullptr;
    std::printf("HISTORY_CONTENTION id=%s updates=%u samples=%zu errors=%zu energy=%g property_reads=%u pass=%d\n",id,updates,probe.samples,probe.errors,probe.energy,property_reads,ok);
    return ok;
}
}
