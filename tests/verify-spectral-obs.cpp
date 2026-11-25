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
static void stream(obs_source_t* source, int rounds, double hz, float amplitude=1) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
    uint64_t start=os_gettime_ns();
    for (int k=0;k<rounds;++k) {
        for (unsigned i=0;i<FRAMES;++i) {
            const float phase=float((uint64_t(k)*FRAMES+i)*6.283185307179586*hz/48000.0);
            l[i]=amplitude*.4f*std::sin(phase);
            r[i]=amplitude*.16f*std::sin(phase);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_spectral_filter")){obs_shutdown();return 5;}
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"threshold",-3);
    obs_data_set_double(settings,"range",60);obs_data_set_double(settings,"attack",1);
    obs_data_set_double(settings,"release",10);
    auto* filter=obs_source_create("hyperdsp_spectral_filter","spectral",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool controls=props&&obs_property_list_item_count(obs_properties_get(props,"mode"))==4&&obs_properties_get(props,"route4_source");
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    auto measure=[&](float amp=1.f,int warm=30,int frames=40){
        stream(source,warm,1500,amp);
        Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,frames,1500,amp);obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto gated=measure();
    obs_data_set_double(settings,"range",0);obs_source_update(filter,settings);auto reconstructed=measure();
    obs_data_set_int(settings,"mode",2);obs_source_update(filter,settings);measure();
    obs_data_set_double(settings,"hold",1);obs_source_update(filter,settings);measure();
    auto frozen=measure(0);
    obs_data_set_double(settings,"hold",0);obs_source_update(filter,settings);auto released=measure(0);
    obs_data_set_int(settings,"mode",3);obs_data_set_double(settings,"smear_ms",250);obs_source_update(filter,settings);
    measure(1,60,40);auto smeared=measure(0,5,20);
    obs_data_set_int(settings,"mode",0);obs_source_update(filter,settings);auto bypass=measure();
    obs_register_source(&sound_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    auto channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"range",60);obs_data_set_double(settings,"threshold",-40);
    obs_data_set_int(settings,"route1_source",6);obs_data_set_int(settings,"route1_target",0);
    obs_data_set_double(settings,"route1_amount",80);
    obs_data_set_string(settings,"route1_sound_channel",channel.c_str());obs_data_set_string(settings,"route1_sound_category","Speech");
    obs_source_update(filter,settings);auto reactive=measure(1,60);sound_fixture::available=false;auto fallback=measure(1,60);
    obs_source_filter_remove(source,producer);obs_source_release(producer);
    bool ok=controls&&std::abs(gated[0]-.000282843)<.00002&&std::abs(reconstructed[0]-.282843)<.002&&
        std::abs(frozen[0]-.282843)<.003&&std::abs(frozen[1]/frozen[0]-.4)<.003&&released[0]<.00001&&
        smeared[0]>.08&&smeared[0]<.28&&std::abs(bypass[0]-.282843)<.002&&reactive[0]<.001&&
        std::abs(fallback[0]-bypass[0])<.003;
    std::printf("SPECTRAL_OBS gate=%g reconstruction=%g freeze=%g,%g release=%g smear=%g bypass=%g reactive=%g fallback=%g pass=%d\n",gated[0],reconstructed[0],frozen[0],frozen[1],released[0],smeared[0],bypass[0],reactive[0],fallback[0],ok);
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
