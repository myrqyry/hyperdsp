#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <cstdio>
struct Probe{float onset=0,tail=0,steady=0;unsigned frames=0;bool linked=true;};
static Probe probe;
static const char* name(void*){return "Transient test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(!a||!a->data[0]||!a->data[1])return a;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    for(unsigned j=0;j<a->frames;++j){
        auto n=probe.frames++;
        if(n==4800)probe.onset=l[j];if(n==19200)probe.steady=l[j];if(n==26400)probe.tail=l[j];
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
    if(!obs_get_source_output_flags("hyperdsp_transient_filter")){obs_shutdown();return 5;}
    obs_source_info fixture{.id="transient_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="transient_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("transient_test_source","transient test",nullptr,nullptr);
    auto* watcher=obs_source_create_private("transient_test_probe","downstream",nullptr);obs_source_filter_add(source,watcher);
    unsigned samples=0;auto start=os_gettime_ns();
    auto stream=[&](){
        probe={};
        std::array<float,480> l{},r{};
        for(unsigned k=0;k<200;++k){
            float x=k<10?0:k<50?.2f:k<80?.04f:0;
            l.fill(x);r.fill(-x*.5f);
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
            a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
            a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
        }
        return probe;
    };
    auto* s=obs_data_create();obs_data_set_double(s,"attack_db",12);obs_data_set_double(s,"sustain_db",12);
    auto* filter=obs_source_create_private("hyperdsp_transient_filter","Transient",s);obs_source_filter_add(source,filter);
    obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    auto a=stream();bool ok=a.frames==96000&&a.linked&&a.onset>.7&&a.tail>.08&&std::abs(a.steady-.2)<.001;
    obs_data_set_int(s,"mode",0);obs_source_update(filter,s);auto bypass=stream();
    ok=ok&&bypass.onset==.2f&&bypass.tail==.04f;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_data_release(s);
    auto* graph=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);obs_source_filter_add(source,graph);obs_source_filter_set_order(source,graph,OBS_ORDER_MOVE_TOP);
    calldata_t d;calldata_init(&d);
    calldata_set_string(&d,"graph",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_transient_filter:1","params":{"sustain_db":12},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"punch","name":"Punch","default":1,"bindings":[{"node":0,"parameter":"attack_db","low":-12,"high":12}]}]})");
    calldata_set_bool(&d,"validate_only",false);
    bool applied=proc_handler_call(obs_source_get_proc_handler(graph),"hyperdsp_graph_apply_v1",&d)&&calldata_bool(&d,"ok");calldata_free(&d);
    auto b=stream();ok=ok&&applied&&b.linked&&std::abs(a.onset-b.onset)<1e-6&&std::abs(a.tail-b.tail)<1e-6;
    s=obs_source_get_settings(graph);obs_data_set_double(s,"graph_control_punch",0);obs_source_update(graph,s);obs_data_release(s);
    auto c=stream();ok=ok&&c.onset<.06&&c.tail>.08&&c.linked;
    std::printf("TRANSIENT_OBS_PASS=%d onset=%g tail=%g steady=%g graph=%g softened=%g frames=%u\n",ok,a.onset,a.tail,a.steady,b.onset,c.onset,a.frames);
    obs_source_filter_remove(source,graph);obs_source_filter_remove(source,watcher);obs_source_release(graph);obs_source_release(watcher);obs_source_release(source);obs_shutdown();return ok?0:1;
}
