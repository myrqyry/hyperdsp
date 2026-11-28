// Actual OBS audio routing test: load the built module, instantiate its
// production native filter, attach it to a synthetic stereo source, and capture
// source output after real filter execution. No fake filter callbacks.
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <algorithm>
#include "sound-signal-fixture.hpp"


struct Stats {
    std::atomic<uint64_t> samples{0};
    std::atomic<double> sum_l{0}, sum_r{0};
};
static void add(std::atomic<double>& val, double x) {
    double old = val.load();
    while (!val.compare_exchange_weak(old, old+x)) {}
}
static void capture(void* param, obs_source_t*, const struct audio_data* data, bool) {
    auto& s = *static_cast<Stats*>(param);
    auto* l = reinterpret_cast<float*>(data->data[0]);
    auto* r = reinterpret_cast<float*>(data->data[1]);
    if (!l || !r) return;
    double sl = 0, sr = 0;
    for (unsigned i=0;i<data->frames;++i) {
        sl += l[i]*l[i];
        sr += r[i]*r[i];
    }
    add(s.sum_l, sl); add(s.sum_r, sr);
    s.samples += data->frames;
}
static const char* fixture_name(void*) { return "Verification stereo generator"; }
static void* fixture_create(obs_data_t*,obs_source_t*) { return reinterpret_cast<void*>(1); }
static void fixture_destroy(void*) {}
static obs_source_info fixture_source = {
    .id="test_mic_audio_generator",
    .type=OBS_SOURCE_TYPE_INPUT,
    .output_flags=OBS_SOURCE_AUDIO,
    .get_name=fixture_name,
    .create=fixture_create,
    .destroy=fixture_destroy,
};
struct PeakProbe {unsigned blocks=0, violations=0;float peak=0;};
static PeakProbe peak_probe;
static void* peak_create(obs_data_t*,obs_source_t*){return &peak_probe;}
static obs_audio_data* peak_inspect(void* ptr,obs_audio_data* audio){
    auto& probe=*static_cast<PeakProbe*>(ptr);
    if(!audio||!audio->data[0])return audio;
    auto* samples=reinterpret_cast<float*>(audio->data[0]);
    for(unsigned i=0;i<audio->frames;++i){
        float sample=std::abs(samples[i]);
        probe.peak=std::max(probe.peak,sample);
        if(!std::isfinite(sample)||sample>.101f)++probe.violations;
    }
    ++probe.blocks;return audio;
}
static void stream(obs_source_t* source, int rounds, double hz,bool paced=true) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
    const auto release=std::chrono::steady_clock::now();
    uint64_t start=os_gettime_ns();
    for (int k=0;k<rounds;++k) {
        for (unsigned i=0;i<FRAMES;++i) {
            const float phase=float((uint64_t(k)*FRAMES+i)*6.283185307179586*hz/48000.0);
            l[i]=.4f*std::sin(phase);
            r[i]=.16f*std::sin(phase);
        }
        obs_source_audio audio{};
        audio.data[0]=reinterpret_cast<uint8_t*>(l.data());
        audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
        audio.frames=FRAMES;
        audio.speakers=SPEAKERS_STEREO;
        audio.format=AUDIO_FORMAT_FLOAT_PLANAR;
        audio.samples_per_sec=48000;
        audio.timestamp=start+uint64_t(k)*10000000ULL;
        obs_source_output_audio(source,&audio);
        if(paced)std::this_thread::sleep_until(release+std::chrono::milliseconds(10*(k+1)));
    }
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_dynamics_filter"))return 5;
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"threshold",-30);
    obs_data_set_double(settings,"ratio",4);obs_data_set_double(settings,"knee",0);
    obs_data_set_double(settings,"attack",1);obs_data_set_double(settings,"release",100);
    auto* filter=obs_source_create("hyperdsp_dynamics_filter","dynamics",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool controls=props&&obs_property_list_item_count(obs_properties_get(props,"mode"))==5&&
        obs_properties_get(props,"threshold")&&obs_properties_get(props,"route4_source");
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    auto measure=[&](){
        stream(source,60,1000);
        Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40,1000);obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto comp=measure();
    obs_data_set_int(settings,"mode",2);obs_data_set_double(settings,"threshold",-3);obs_data_set_double(settings,"ratio",2);obs_source_update(filter,settings);
    auto exp=measure();
    obs_data_set_int(settings,"mode",3);obs_data_set_double(settings,"range",60);obs_source_update(filter,settings);
    auto gate=measure();
    obs_data_set_int(settings,"mode",4);obs_data_set_double(settings,"threshold",-20);obs_source_update(filter,settings);
    auto limit=measure();
    obs_data_set_int(settings,"mode",0);obs_source_update(filter,settings);
    auto dry=measure();
    obs_register_source(&sound_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    const auto channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"threshold",0);
    obs_data_set_int(settings,"route1_source",6);obs_data_set_int(settings,"route1_target",5);
    obs_data_set_double(settings,"route1_amount",-.8);
    obs_data_set_string(settings,"route1_sound_channel",channel.c_str());obs_data_set_string(settings,"route1_sound_category","Speech");
    obs_source_update(filter,settings);auto reactive=measure();
    sound_fixture::available=false;auto fallback=measure();
    obs_source_filter_remove(source,producer);obs_source_release(producer);
    bool ok=controls&&comp[0]>.035&&comp[0]<.09&&exp[0]>.08&&exp[0]<.20&&gate[0]<.001&&
        limit[0]>.04&&limit[0]<.101&&std::abs(dry[0]-.282843)<.002&&
        std::abs(reactive[0]-.169706)<.004&&std::abs(fallback[0]-dry[0])<.004&&
        std::abs(comp[1]/comp[0]-.4)<.001&&std::abs(limit[1]/limit[0]-.4)<.001;
    // Constant full-wet limiter ceiling while the control writer floods
    // unrelated controls. Every output sample must remain limited.
    obs_data_set_int(settings,"mode",4);obs_data_set_double(settings,"threshold",-20);
    obs_data_set_int(settings,"route1_source",0);obs_source_update(filter,settings);
    stream(source,60,1000,false);
    obs_source_info monitor{.id="dynamics_peak_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
        .get_name=fixture_name,.create=peak_create,.destroy=fixture_destroy,.filter_audio=peak_inspect};
    obs_register_source(&monitor);
    auto* watcher=obs_source_create_private("dynamics_peak_probe","downstream",nullptr);
    obs_source_filter_add(source,watcher);
    obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    std::atomic<bool> ready{false},stop{false};
    unsigned updates=0;
    std::thread writer([&]{
        while(!stop.load()){
            obs_data_set_double(settings,"release",updates%2?80:120);
            obs_source_update(filter,settings);++updates;ready=true;
        }
    });
    while(!ready.load())std::this_thread::yield();
    stream(source,3000,1000,false);
    stop=true;writer.join();
    bool uninterrupted=updates>10&&peak_probe.blocks==3000&&!peak_probe.violations;
    ok=ok&&uninterrupted;
    std::printf("DYNAMICS_CONTENTION updates=%u blocks=%u peak=%g violations=%u pass=%d\n",updates,peak_probe.blocks,peak_probe.peak,peak_probe.violations,uninterrupted);
    obs_source_filter_remove(source,watcher);obs_source_release(watcher);
    std::printf("DYNAMICS_OBS comp=%g exp=%g gate=%g limit=%g dry=%g reactive=%g fallback=%g pass=%d\n",comp[0],exp[0],gate[0],limit[0],dry[0],reactive[0],fallback[0],ok);
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
