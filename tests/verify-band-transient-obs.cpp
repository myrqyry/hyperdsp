#include "history-contention-fixture.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <numbers>
#include <cstdio>
struct Probe {double onset=0,tail=0;unsigned frames=0;bool linked=true;};
static Probe probe;
static const char* name(void*){return "Band transient test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(!a||!a->data[0]||!a->data[1])return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    for(unsigned j=0;j<a->frames;++j){
        unsigned n=probe.frames++;
        if(n>=4800&&n<5760)probe.onset+=double(l[j])*l[j];
        if(n>=26400&&n<28800)probe.tail+=double(l[j])*l[j];
        probe.linked=probe.linked&&std::isfinite(l[j])&&std::abs(r[j]+l[j]*.5f)<1e-6;
    }
    return a;
}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_source_info fixture{.id="band_transient_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="band_transient_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("band_transient_test_source","Band transient",nullptr,nullptr);
    auto* watcher=obs_source_create_private("band_transient_test_probe","downstream",nullptr);
    obs_source_filter_add(source,watcher);
    unsigned samples=0;auto start=os_gettime_ns();
    auto stream=[&](double hz){
        probe={};std::array<float,480> l{},r{};
        for(unsigned k=0;k<250;++k){
            double amp=k<10?0:k<50?.2:k<80?.04:0;
            for(unsigned j=0;j<480;++j){l[j]=amp*std::sin(2*std::numbers::pi*hz*(k*480+j)/48000);r[j]=-.5f*l[j];}
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
            a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
            a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
        }
        auto result=probe;result.onset=std::sqrt(result.onset/960);result.tail=std::sqrt(result.tail/2400);return result;
    };
    auto* s=obs_data_create();obs_data_set_int(s,"mode",2);
    auto* f=obs_source_create_private("hyperdsp_transient_filter","Transient",s);
    obs_source_filter_add(source,f);obs_source_filter_set_order(source,f,OBS_ORDER_MOVE_TOP);
    auto* props=obs_source_properties(f);
    auto visible=[&](const char* key){auto* p=props?obs_properties_get(props,key):nullptr;return p&&obs_property_visible(p);};
    bool ok=props&&visible("low_attack_db")&&visible("high_hz")&&!visible("attack_db")&&visible("attack_ms");
    auto baseline=stream(80),highBase=stream(6000);
    obs_data_set_double(s,"low_attack_db",12);obs_data_set_double(s,"low_sustain_db",-12);obs_source_update(f,s);
    auto low=stream(80),high=stream(6000);
    ok=ok&&baseline.frames==120000&&low.linked&&high.linked&&low.onset/baseline.onset>1.5&&low.tail/baseline.tail<.7&&std::abs(high.onset/highBase.onset-1)<.01;
    obs_data_set_int(s,"mode",1);obs_source_update(f,s);
    if(props)obs_properties_apply_settings(props,s);
    ok=ok&&visible("attack_db")&&!visible("low_attack_db")&&!visible("high_hz");
    auto broad=stream(80);
    obs_data_set_int(s,"mode",0);obs_source_update(f,s);auto bypass=stream(80);
    ok=ok&&std::abs(broad.onset-bypass.onset)<1e-7&&std::abs(broad.tail-bypass.tail)<1e-7;
    obs_data_set_int(s,"mode",2);obs_source_update(f,s);auto restored=stream(80);
    ok=ok&&std::abs(restored.onset-low.onset)<1e-5;
    if(props)obs_properties_destroy(props);
    obs_source_filter_remove(source,f);obs_source_release(f);obs_data_release(s);
    auto* graph=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);
    obs_source_filter_add(source,graph);obs_source_filter_set_order(source,graph,OBS_ORDER_MOVE_TOP);
    calldata_t d;calldata_init(&d);
    calldata_set_string(&d,"graph",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_transient_filter:2","params":{"low_sustain_db":-12},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"bass","name":"Bass attack","default":1,"bindings":[{"node":0,"parameter":"low_attack_db","low":0,"high":12}]}]})");
    calldata_set_bool(&d,"validate_only",false);
    bool applied=proc_handler_call(obs_source_get_proc_handler(graph),"hyperdsp_graph_apply_v1",&d)&&calldata_bool(&d,"ok");calldata_free(&d);
    auto g=stream(80);
    ok=ok&&applied&&g.linked&&std::abs(g.onset-low.onset)<1e-5&&std::abs(g.tail-low.tail)<1e-5;
    s=obs_source_get_settings(graph);obs_data_set_double(s,"graph_control_bass",0);obs_source_update(graph,s);obs_data_release(s);
    auto neutral=stream(80);ok=ok&&std::abs(neutral.onset-baseline.onset)<1e-5;
    std::printf("BAND_TRANSIENT_OBS_PASS=%d onset_ratio=%g tail_ratio=%g high_ratio=%g graph_onset=%g macro_off=%g\n",ok,low.onset/baseline.onset,low.tail/baseline.tail,high.onset/highBase.onset,g.onset,neutral.onset);
    obs_source_filter_remove(source,graph);obs_source_filter_remove(source,watcher);obs_source_release(graph);obs_source_release(watcher);auto* history=obs_data_create();
    obs_data_set_int(history,"mode",2);obs_data_set_double(history,"low_attack_db",12);obs_data_set_double(history,"high_sustain_db",-12);
    bool continuous=history_contention::verify(source,"hyperdsp_transient_filter",history,"attack_db",0,12);
    obs_data_release(history);ok=ok&&continuous;
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
