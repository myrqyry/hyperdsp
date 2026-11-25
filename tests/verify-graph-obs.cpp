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
    if(!obs_get_source_output_flags("hyperdsp_graph_filter")){obs_shutdown();return 5;}
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    const char* serial=R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":0,"weight":1}]}],"output":1})";
    const char* parallel=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_spectral_filter:1","params":{"range":0},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":-1},{"source":-1,"weight":1}]}],"output":1})";
    const char* invalid=R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":0,"weight":1}]}],"output":0})";
    obs_data_set_string(settings,"graph_json",serial);
    auto* filter=obs_source_create("hyperdsp_graph_filter","graph",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool controls=props&&obs_properties_get(props,"graph_json");
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    auto measure=[&](){
        stream(source,30,1500);
        Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40,1500);obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto a=measure();
    obs_data_set_string(settings,"graph_json",parallel);obs_source_update(filter,settings);auto b=measure();
    obs_data_set_string(settings,"graph_json",invalid);obs_source_update(filter,settings);auto c=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":99},"inputs":[{"source":-1,"weight":1}]}],"output":0})");obs_source_update(filter,settings);auto d=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);auto duplicate=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[42],"output":0})");obs_source_update(filter,settings);auto malformed=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);auto bypass=measure();
    bool ok=controls&&std::abs(a[0]-.071046)<.002&&std::abs(a[1]/a[0]-.4)<.001&&b[0]<1e-5&&c[0]<1e-5&&d[0]<1e-5&&duplicate[0]<1e-5&&malformed[0]<1e-5&&std::abs(bypass[0]-.282843)<.002;
    std::printf("GRAPH_OBS serial=%g parallel=%g invalid=%g invalid_param=%g bypass=%g pass=%d\n",a[0],b[0],c[0],d[0],bypass[0],ok);
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
