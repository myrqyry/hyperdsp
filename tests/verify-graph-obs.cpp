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
#include "sound-signal-fixture.hpp"
#include "latency-contention-fixture.hpp"
#include "sight-signal-fixture.hpp"


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
        std::this_thread::sleep_until(release+std::chrono::milliseconds(10*(k+1)));
    }
}
int main(int argc,char**argv){
    if((argc!=3&&argc!=4)||!obs_startup("en-US",nullptr,nullptr))return 2;
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
    const char* mixed=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_native_filter:1","params":{"frequency":1500,"alignment":1,"stages":2},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_dynamics_filter:4","params":{"threshold":-20},"inputs":[{"source":0,"weight":1}]},{"effect":"native:hyperdsp_time_filter:1","params":{"delay_ms":2,"feedback":0,"mix":1},"inputs":[{"source":1,"weight":1}]}],"output":2})";
    obs_data_set_string(settings,"graph_json",mixed);obs_source_update(filter,settings);auto chain=measure();
    // Linked limiter caps the wet branch at .1 and preserves the stereo ratio.
    bool mixed_ok=chain[0]>.055&&chain[0]<=.1001&&std::abs(chain[1]/chain[0]-.4)<.003;
    std::printf("GRAPH_MIXED left=%g right=%g pass=%d\n",chain[0],chain[1],mixed_ok);

    obs_data_set_string(settings,"graph_json",parallel);obs_source_update(filter,settings);auto b=measure();
    obs_data_set_string(settings,"graph_json",invalid);obs_source_update(filter,settings);auto c=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":99},"inputs":[{"source":-1,"weight":1}]}],"output":0})");obs_source_update(filter,settings);auto d=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);auto duplicate=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[42],"output":0})");obs_source_update(filter,settings);auto malformed=measure();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);auto bypass=measure();
    const char* macro=R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":1}]}],"output":1,"controls":[{"id":"level","name":"Level","default":0.5,"bindings":[{"node":0,"parameter":"gain_db","low":-12,"high":0},{"node":1,"parameter":"gain_db","low":-12,"high":0}]}]})";
    obs_data_set_string(settings,"graph_json",macro);obs_source_update(filter,settings);auto macro_default=measure();
    props=obs_source_properties(filter);
    auto* slider=props?obs_properties_get(props,"graph_control_level"):nullptr;
    bool macro_ok=slider&&obs_property_get_type(slider)==OBS_PROPERTY_FLOAT&&
        obs_property_float_min(slider)==0&&obs_property_float_max(slider)==1;
    if(props)obs_properties_destroy(props);
    obs_data_set_double(settings,"graph_control_level",1);obs_source_update(filter,settings);auto macro_high=measure();
    obs_data_set_double(settings,"graph_control_level",0);obs_source_update(filter,settings);auto macro_low=measure();
    obs_data_set_double(settings,"graph_control_level",2);obs_source_update(filter,settings);auto macro_bad=measure();
    macro_ok=macro_ok&&std::abs(macro_default[0]-.0710469)<.002&&std::abs(macro_high[0]-.282843)<.002&&
        std::abs(macro_low[0]-.0178462)<.002&&std::abs(macro_bad[0]-macro_low[0])<.002;
    std::printf("GRAPH_MACRO_OBS default=%g high=%g low=%g invalid_retained=%g pass=%d\n",macro_default[0],macro_high[0],macro_low[0],macro_bad[0],macro_ok);
    obs_register_source(&sound_fixture::info);
    obs_register_source(&sight_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    std::string channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    const std::string base=R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":1}]}],"output":1,"controls":[{"id":"reactive","name":"Reactive level","default":0,"bindings":[{"node":0,"parameter":"gain_db","low":-12,"high":0},{"node":1,"parameter":"gain_db","low":-12,"high":0}]}],"routes":[)";
    const std::string sound=base+R"({"control":"reactive","source":6,"amount":1,"channel":")"+channel+R"(","category":"Speech"}]})";
    obs_data_set_string(settings,"graph_json",sound.c_str());obs_source_update(filter,settings);
    auto sound_active=measure();
    sound_fixture::available=false;auto sound_fallback=measure();
    const std::string sight=base+R"({"control":"reactive","source":8,"amount":1,"channel":"test-hand"}]})";
    obs_data_set_string(settings,"graph_json",sight.c_str());obs_source_update(filter,settings);
    auto sight_active=measure();
    sight_fixture::available=false;auto sight_fallback=measure();
    obs_source_filter_remove(source,producer);obs_source_release(producer);
    bool reactive_ok=std::abs(sound_active[0]-.0710469)<.002&&std::abs(sound_fallback[0]-.0178462)<.002&&
        std::abs(sight_active[0]-.142014)<.003&&std::abs(sight_fallback[0]-.0178462)<.002&&
        sound_fixture::calls>0&&sight_fixture::calls>0;
    std::printf("GRAPH_REACTIVE_OBS sound=%g fallback=%g sight=%g fallback=%g pass=%d\n",sound_active[0],sound_fallback[0],sight_active[0],sight_fallback[0],reactive_ok);
    bool external_ok=true;
    if(argc==4){
        const std::string id=std::string("ladspa:")+argv[3]+"\tamp_mono";
        auto* id_data=obs_data_create();obs_data_set_string(id_data,"effect",id.c_str());
        std::string node=obs_data_get_json(id_data);obs_data_release(id_data);node.pop_back();
        node+=R"(,"params":{"port_0":0.5},"inputs":[{"source":-1,"weight":1}]})";
        std::string graph=R"({"schema":1,"nodes":[)"+node+
            R"(,{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":0,"weight":1}]}],"output":1})";
        obs_data_set_string(settings,"graph_json",graph.c_str());obs_source_update(filter,settings);auto external=measure();
        auto unavailable=graph;auto pos=unavailable.find("amp_mono");unavailable.replace(pos,8,"unscanned");
        obs_data_set_string(settings,"graph_json",unavailable.c_str());obs_source_update(filter,settings);auto retained=measure();
        auto bad=graph;pos=bad.find("port_0");bad.replace(pos,6,"port_999");
        obs_data_set_string(settings,"graph_json",bad.c_str());obs_source_update(filter,settings);auto rejected=measure();
        external_ok=std::abs(external[0]-.0708786)<.002&&std::abs(external[1]/external[0]-.4)<.002&&
            std::abs(retained[0]-external[0])<.002&&std::abs(rejected[0]-external[0])<.002;
        std::printf("LADSPA_GRAPH_OBS serial=%g right=%g unscanned_retained=%g invalid_retained=%g pass=%d\n",external[0],external[1],retained[0],rejected[0],external_ok);
    }
    bool presets_ok=true;
    for(auto file:{"parallel-color.json","vowel-space.json"}){
        std::ifstream input(std::string(argv[2])+"/examples/graphs/"+file);
        std::string json((std::istreambuf_iterator<char>(input)),{});
        if(json.empty()){presets_ok=false;continue;}
        obs_data_set_string(settings,"graph_json",json.c_str());obs_source_update(filter,settings);
        props=obs_source_properties(filter);
        const char* key=std::string_view(file)=="parallel-color.json"?"graph_control_color":"graph_control_vowel";
        presets_ok=presets_ok&&props&&obs_properties_get(props,key);
        if(props)obs_properties_destroy(props);
        auto result=measure();presets_ok=presets_ok&&std::isfinite(result[0])&&result[0]>.001;
    }
    bool ok=reactive_ok&&presets_ok&&macro_ok&&external_ok&&mixed_ok&&controls&&std::abs(a[0]-.071046)<.002&&std::abs(a[1]/a[0]-.4)<.001&&b[0]<1e-5&&c[0]<1e-5&&d[0]<1e-5&&duplicate[0]<1e-5&&malformed[0]<1e-5&&std::abs(bypass[0]-.282843)<.002;
    std::printf("GRAPH_OBS serial=%g parallel=%g invalid=%g invalid_param=%g bypass=%g pass=%d\n",a[0],b[0],c[0],d[0],bypass[0],ok);
    const char* latency_graph=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_spectral_filter:1","params":{"mix":0},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"threshold","name":"Threshold","default":0.5,"bindings":[{"node":0,"parameter":"threshold","low":-50,"high":-30}]}]})";
    obs_data_set_string(settings,"graph_json",latency_graph);obs_source_update(filter,settings);
    bool continuous=latency_contention::verify(source,filter,settings,1024,"graph_control_threshold",0,1,true);
    ok=ok&&continuous;
    obs_set_output_source(1,nullptr);obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();return ok?0:1;
}
