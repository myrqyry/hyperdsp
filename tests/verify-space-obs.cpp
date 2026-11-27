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
#include "history-contention-fixture.hpp"


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
static void stream(obs_source_t* source, int rounds, double hz) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_space_filter")){obs_shutdown();return 5;}
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"center",1000);
    obs_data_set_double(settings,"depth",0);obs_data_set_double(settings,"feedback",0);
    obs_data_set_int(settings,"stages",2);obs_data_set_double(settings,"mix",.5);
    auto* filter=obs_source_create("hyperdsp_space_filter","space",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool controls=props&&obs_property_list_item_count(obs_properties_get(props,"mode"))==3&&obs_properties_get(props,"route4_source");
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    auto measure=[&](){
        stream(source,50,1000);
        Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40,1000);obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto notch=measure();
    obs_data_set_double(settings,"mix",1);obs_source_update(filter,settings);auto allpass=measure();
    obs_data_set_int(settings,"mode",2);obs_data_set_double(settings,"decay",.8);obs_source_update(filter,settings);auto reverb=measure();
    obs_data_set_int(settings,"mode",0);obs_source_update(filter,settings);auto bypass=measure();
    obs_register_source(&sound_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    auto channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    obs_data_set_int(settings,"mode",1);obs_data_set_double(settings,"mix",0);
    obs_data_set_int(settings,"route1_source",6);obs_data_set_int(settings,"route1_target",4);
    obs_data_set_double(settings,"route1_amount",1);
    obs_data_set_string(settings,"route1_sound_channel",channel.c_str());obs_data_set_string(settings,"route1_sound_category","Speech");
    obs_source_update(filter,settings);auto reactive=measure();sound_fixture::available=false;auto fallback=measure();
    obs_source_filter_remove(source,producer);obs_source_release(producer);
    bool ok=controls&&notch[0]<.002&&std::abs(allpass[0]-.282843)<.003&&
        reverb[0]>.01&&reverb[0]<1&&reverb[1]>.01&&std::abs(bypass[0]-.282843)<.003&&reactive[0]<.003&&std::abs(fallback[0]-bypass[0])<.004;
    std::printf("SPACE_OBS notch=%g allpass=%g reverb=%g,%g bypass=%g reactive=%g fallback=%g pass=%d\n",notch[0],allpass[0],reverb[0],reverb[1],bypass[0],reactive[0],fallback[0],ok);
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    auto* history=obs_data_create();
    obs_data_set_int(history,"mode",2);obs_data_set_double(history,"mix",1);
    obs_data_set_double(history,"decay",1.2);
    bool continuous=history_contention::verify(source,"hyperdsp_space_filter",history,"center",400,1600);
    obs_data_release(history);ok=ok&&continuous;
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
