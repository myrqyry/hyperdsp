// Actual OBS audio routing test: load the built module, instantiate its
// production native filter, attach it to a synthetic stereo source, and capture
// source output after real filter execution. No fake filter callbacks.
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>


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
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    const char* graph=R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"level","name":"Level","default":0.5,"bindings":[{"node":0,"parameter":"gain_db","low":-24,"high":0}]}]})";
    const char* invalid=R"({"schema":1,"nodes":[42],"output":0})";
    const char* empty=R"({"schema":1,"nodes":[],"output":-1})";
    obs_data_set_string(settings,"graph_json",graph);
    auto* filter=obs_source_create_private("hyperdsp_graph_filter","recovery",settings);
    if(!source||!filter)return 5;
    obs_source_filter_add(source,filter);obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    auto measure=[&](){
        stream(source,20,1500);Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,30,1500);obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load()));
    };
    auto edit=[&](const char* raw,double value){
        auto* data=obs_source_get_settings(filter);
        obs_data_set_string(data,"graph_json",raw);obs_data_set_double(data,"graph_control_level",value);
        obs_source_update(filter,data);obs_data_release(data);
    };
    auto recreate=[&](){
        auto* saved=obs_save_source(filter);
        auto* roundtrip=obs_data_create_from_json(obs_data_get_json(saved));obs_data_release(saved);
        auto* data=obs_data_get_obj(roundtrip,"settings");
        obs_source_filter_remove(source,filter);obs_source_release(filter);
        filter=obs_source_create_private("hyperdsp_graph_filter","restored",data);
        obs_source_filter_add(source,filter);obs_data_release(data);obs_data_release(roundtrip);
    };
    edit(graph,.75);auto good=measure();
    edit(invalid,.1);auto live=measure();
    recreate();auto recovered=measure();
    auto* saved=obs_source_get_settings(filter);
    bool draft=std::string_view(obs_data_get_string(saved,"graph_json"))==invalid;
    obs_data_release(saved);
    auto* props=obs_source_properties(filter);
    bool slider=props&&obs_properties_get(props,"graph_control_level");
    auto* restore=props?obs_properties_get(props,"restore_working_graph"):nullptr;
    bool restore_clicked=restore&&obs_property_button_clicked(restore,nullptr);
    if(props)obs_properties_destroy(props);
    saved=obs_source_get_settings(filter);
    bool restored_draft=std::string_view(obs_data_get_string(saved,"graph_json"))==graph&&
        std::abs(obs_data_get_double(saved,"graph_control_level")-.75)<1e-8;
    obs_data_release(saved);
    auto restored_button=measure();
    edit(graph,.25);auto changed=measure();
    edit(graph,2);recreate();auto macro=measure();
    edit(empty,0);edit(invalid,0);recreate();auto bypass=measure();
    // An invalid snapshot must not load arbitrary state, and a new filter
    // must not inherit recovery from a different filter instance.
    obs_source_filter_remove(source,filter);obs_source_release(filter);
    auto* corrupt=obs_data_create();auto* snapshot=obs_data_create();auto* values=obs_data_create();
    obs_data_set_int(snapshot,"schema",1);obs_data_set_string(snapshot,"graph",empty);
    obs_data_set_string(values,"level","not numeric");obs_data_set_obj(snapshot,"values",values);
    obs_data_set_obj(corrupt,"graph_last_good",snapshot);obs_data_release(values);obs_data_release(snapshot);
    obs_data_set_string(corrupt,"graph_json",invalid);
    filter=obs_source_create_private("hyperdsp_graph_filter","corrupt",corrupt);
    obs_source_filter_add(source,filter);auto corrupt_audio=measure();
    props=obs_source_properties(filter);
    bool no_restore=props&&!obs_properties_get(props,"restore_working_graph");
    if(props)obs_properties_destroy(props);
    auto* saved_corrupt=obs_save_source(filter);
    auto* corrupt_settings=obs_data_get_obj(saved_corrupt,"settings");
    auto* kept=obs_data_get_obj(corrupt_settings,"graph_last_good");
    auto* kept_values=kept?obs_data_get_obj(kept,"values"):nullptr;
    bool kept_record=kept_values&&std::string_view(obs_data_get_string(kept_values,"level"))=="not numeric";
    if(kept_values)obs_data_release(kept_values);
    if(kept)obs_data_release(kept);
    obs_data_release(corrupt_settings);obs_data_release(saved_corrupt);obs_data_release(corrupt);
    bool ok=restore_clicked&&restored_draft&&std::abs(restored_button-good)<.002&&
        no_restore&&kept_record&&std::abs(corrupt_audio-.282843)<.002&&draft&&slider&&std::abs(good-.141757)<.002&&std::abs(live-good)<.002&&
        std::abs(recovered-good)<.002&&std::abs(changed-.0356078)<.002&&
        std::abs(macro-changed)<.002&&std::abs(bypass-.282843)<.002;
    std::printf("GRAPH_RESTORE_BUTTON clicked=%d draft=%d rms=%g corrupt=%g record_preserved=%d\n",restore_clicked,restored_draft,restored_button,corrupt_audio,kept_record);
    std::printf("GRAPH_RECOVERY good=%g live=%g restored=%g changed=%g macro=%g empty=%g draft=%d slider=%d pass=%d\n",good,live,recovered,changed,macro,bypass,draft,slider,ok);
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
