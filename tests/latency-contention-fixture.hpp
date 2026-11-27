#pragma once
#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

namespace latency_contention {
inline float sample(uint64_t i){
    uint32_t x=uint32_t(i)*747796405u+2891336453u;
    x=((x>>((x>>28)+4))^x)*277803737u;
    return (float((x^(x>>22))&65535)/32768.f-1.f)*.4f;
}
struct Probe {unsigned delay;uint64_t samples=0,errors=0;};
inline Probe* active=nullptr;
inline const char* name(void*){return "Latency contention probe";}
inline void* create(obs_data_t*,obs_source_t*){return active;}
inline void destroy(void*){}
inline obs_audio_data* inspect(void* ptr,obs_audio_data* a){
    auto& p=*static_cast<Probe*>(ptr);
    if(!a)return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    for(unsigned j=0;j<a->frames;++j,++p.samples){
        if(p.samples<4800)continue;
        float expected=sample(p.samples-p.delay);
        if(!l||!r||!std::isfinite(l[j])||!std::isfinite(r[j])||
           std::abs(l[j]-expected)>1e-5f||std::abs(r[j]+.4f*expected)>1e-5f)++p.errors;
    }
    return a;
}
// Caller selects a full-dry, latency-aligned mode and constant unity trims.
// Concurrent changes to an irrelevant parameter must not disturb any sample.
inline bool verify(obs_source_t* source,obs_source_t* filter,obs_data_t* settings,
                   unsigned delay,const char* key,double a,double b){
    Probe probe{delay};active=&probe;
    obs_source_info info{.id="hyperdsp_latency_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
        .get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&info);
    auto* watcher=obs_source_create_private(info.id,"latency probe",nullptr);
    if(!watcher){active=nullptr;return false;}
    obs_source_filter_add(source,watcher);obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    std::atomic<bool> ready{false},stop{false};
    unsigned updates=0;
    std::thread writer([&]{
        while(!stop.load()){
            obs_data_set_double(settings,key,updates%2?a:b);
            obs_source_update(filter,settings);++updates;ready=true;
        }
    });
    while(!ready.load())std::this_thread::yield();
    constexpr unsigned blocks=2000,frames=480;
    std::array<float,frames> l{},r{};auto start=os_gettime_ns();
    for(unsigned k=0;k<blocks;++k){
        for(unsigned j=0;j<frames;++j){l[j]=sample(uint64_t(k)*frames+j);r[j]=-.4f*l[j];}
        obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(l.data());audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
        audio.frames=frames;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=48000;
        audio.timestamp=start+uint64_t(k)*10000000ULL;obs_source_output_audio(source,&audio);
    }
    stop=true;writer.join();
    obs_source_filter_remove(source,watcher);obs_source_release(watcher);active=nullptr;
    bool ok=updates>10&&probe.samples==uint64_t(blocks)*frames&&!probe.errors;
    std::printf("LATENCY_CONTENTION delay=%u updates=%u samples=%llu errors=%llu pass=%d\n",delay,updates,
        (unsigned long long)probe.samples,(unsigned long long)probe.errors,ok);
    return ok;
}
}
