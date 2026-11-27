#include "history-contention-fixture.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <string>
struct Probe{double l=0,r=0;unsigned frames=0;bool finite=true;};
static Probe probe;
static const char* name(void*){return "Multiband test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(!a)return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    if(!l||!r){probe.finite=false;return a;}
    for(unsigned j=0;j<a->frames;++j){probe.finite=probe.finite&&std::isfinite(l[j])&&std::isfinite(r[j]);probe.l+=l[j]*l[j];probe.r+=r[j]*r[j];}
    probe.frames+=a->frames;return a;
}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_multiband_filter")){obs_shutdown();return 5;}
    obs_source_info fixture{.id="multiband_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="multiband_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("multiband_test_source","multiband test",nullptr,nullptr);
    auto* watcher=obs_source_create_private("multiband_test_probe","downstream",nullptr);
    obs_source_filter_add(source,watcher);
    unsigned samples=0;auto start=os_gettime_ns();
    auto stream=[&](double hz){
        std::array<float,480> l{},r{};
        for(unsigned k=0;k<100;++k){
            if(k==70)probe={};
            for(unsigned j=0;j<480;++j){l[j]=.5*std::sin(2*std::numbers::pi*hz*(samples+j)/48000);r[j]=-.5*l[j];}
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
            a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
            a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
        }
        return probe.frames?std::sqrt(probe.l/probe.frames):0.;
    };
    auto* settings=obs_data_create();
    obs_data_set_double(settings,"low_ratio",8);obs_data_set_double(settings,"mid_ratio",1);obs_data_set_double(settings,"high_ratio",1);
    obs_data_set_double(settings,"low_threshold",-30);obs_data_set_double(settings,"attack",1);obs_data_set_double(settings,"release",80);
    auto* filter=obs_source_create_private("hyperdsp_multiband_filter","Multiband",settings);
    obs_source_filter_add(source,filter);obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    double low=stream(80),high=stream(6000);
    bool ok=probe.finite&&low<.09&&low>.01&&high>.34&&high<.36&&std::abs(probe.r/probe.l-.25)<1e-5;
    obs_data_set_int(settings,"mode",0);obs_source_update(filter,settings);double bypass=stream(80);
    ok=ok&&std::abs(bypass-.353553)<.003;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_data_release(settings);
    auto* graph=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);
    obs_source_filter_add(source,graph);obs_source_filter_set_order(source,graph,OBS_ORDER_MOVE_TOP);
    calldata_t d;calldata_init(&d);
    calldata_set_string(&d,"graph",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_multiband_filter:1","params":{"low_threshold":-30,"mid_ratio":1,"high_ratio":1,"attack":1,"release":80},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"bass","name":"Bass compression","default":1,"bindings":[{"node":0,"parameter":"low_ratio","low":1,"high":8}]}]})");
    calldata_set_bool(&d,"validate_only",false);
    ok=ok&&proc_handler_call(obs_source_get_proc_handler(graph),"hyperdsp_graph_apply_v1",&d)&&calldata_bool(&d,"ok");calldata_free(&d);
    auto* props=obs_source_properties(graph);ok=ok&&props&&obs_properties_get(props,"graph_control_bass");if(props)obs_properties_destroy(props);
    double graphLow=stream(80);ok=ok&&std::abs(low-graphLow)<.003;
    settings=obs_source_get_settings(graph);obs_data_set_double(settings,"graph_control_bass",0);obs_source_update(graph,settings);obs_data_release(settings);
    double uncompressed=stream(80);ok=ok&&std::abs(uncompressed-.353553)<.003;
    std::printf("MULTIBAND_OBS low=%g high=%g bypass=%g graph=%g macro_off=%g pass=%d\n",low,high,bypass,graphLow,uncompressed,ok);
    obs_source_filter_remove(source,graph);obs_source_filter_remove(source,watcher);obs_source_release(graph);obs_source_release(watcher);auto* history=obs_data_create();
    obs_data_set_int(history,"mode",1);obs_data_set_double(history,"low_threshold",-40);obs_data_set_double(history,"low_ratio",8);
    bool continuous=history_contention::verify(source,"hyperdsp_multiband_filter",history,"unused_test_control",0,1);
    obs_data_release(history);ok=ok&&continuous;
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
