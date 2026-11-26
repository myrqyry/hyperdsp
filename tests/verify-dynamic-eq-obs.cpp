#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <numbers>
#include <cstdio>
struct Probe{double l=0,r=0;unsigned frames=0;bool linked=true;};
static Probe probe;
static const char* name(void*){return "Dynamic EQ test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(!a||!a->data[0]||!a->data[1])return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    for(unsigned j=0;j<a->frames;++j){
        probe.l+=double(l[j])*l[j];probe.r+=double(r[j])*r[j];
        probe.linked=probe.linked&&std::isfinite(l[j])&&std::abs(r[j]+.5f*l[j])<1e-6;
    }
    probe.frames+=a->frames;return a;
}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_dynamic_eq_filter")){obs_shutdown();return 5;}
    obs_source_info fixture{.id="dynamic_eq_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="dynamic_eq_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("dynamic_eq_test_source","Dynamic EQ",nullptr,nullptr);
    auto* watcher=obs_source_create_private("dynamic_eq_test_probe","downstream",nullptr);obs_source_filter_add(source,watcher);
    unsigned samples=0;auto start=os_gettime_ns();bool ok=true;
    auto stream=[&](double hz,double amp){
        std::array<float,480> l{},r{};
        for(unsigned k=0;k<150;++k){
            if(k==120)probe={};
            for(unsigned j=0;j<480;++j){l[j]=amp*std::sin(2*std::numbers::pi*hz*(samples+j)/48000);r[j]=-.5f*l[j];}
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
            a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
            a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
        }
        ok=ok&&probe.linked&&probe.frames==14400;
        return 20*std::log10(std::sqrt(probe.l/probe.frames)/(amp/std::sqrt(2.)));
    };
    auto* s=obs_data_create();obs_data_set_double(s,"frequency",6500);obs_data_set_double(s,"threshold",-30);obs_data_set_double(s,"range_db",-9);
    auto* filter=obs_source_create_private("hyperdsp_dynamic_eq_filter","Dynamic EQ",s);
    obs_source_filter_add(source,filter);obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    double cut=stream(6500,.5),quiet=stream(6500,.01),low=stream(180,.5);
    ok=ok&&std::abs(cut+9)<.02&&std::abs(quiet)<.02&&std::abs(low)<.02;
    obs_data_set_double(s,"range_db",6);obs_source_update(filter,s);double boost=stream(6500,.5);
    ok=ok&&std::abs(boost-6)<.02;
    obs_data_set_int(s,"mode",0);obs_source_update(filter,s);double bypass=stream(6500,.5);ok=ok&&std::abs(bypass)<.0001;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_data_release(s);
    auto* graph=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);
    obs_source_filter_add(source,graph);obs_source_filter_set_order(source,graph,OBS_ORDER_MOVE_TOP);
    calldata_t d;calldata_init(&d);
    calldata_set_string(&d,"graph",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamic_eq_filter:1","params":{"frequency":6500,"threshold":-30},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"depth","name":"De-ess depth","default":1,"bindings":[{"node":0,"parameter":"range_db","low":0,"high":-9}]}]})");
    calldata_set_bool(&d,"validate_only",false);
    bool applied=proc_handler_call(obs_source_get_proc_handler(graph),"hyperdsp_graph_apply_v1",&d)&&calldata_bool(&d,"ok");calldata_free(&d);
    double graphCut=stream(6500,.5);ok=ok&&applied&&std::abs(graphCut-cut)<.02;
    s=obs_source_get_settings(graph);obs_data_set_double(s,"graph_control_depth",0);obs_source_update(graph,s);obs_data_release(s);
    double neutral=stream(6500,.5);ok=ok&&std::abs(neutral)<.02;
    std::printf("DYNAMIC_EQ_OBS_PASS=%d cut=%g quiet=%g low=%g boost=%g bypass=%g graph=%g macro_off=%g\n",ok,cut,quiet,low,boost,bypass,graphCut,neutral);
    obs_source_filter_remove(source,graph);obs_source_filter_remove(source,watcher);obs_source_release(graph);obs_source_release(watcher);obs_source_release(source);obs_shutdown();return ok?0:1;
}
