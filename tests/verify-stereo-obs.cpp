#include "history-contention-fixture.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <cstdio>
struct Probe{float l=0,r=0;unsigned packets=0;};
static Probe probe;
static const char* name(void*){return "Stereo test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(a&&a->frames&&a->data[0]&&a->data[1]){
        probe.l=reinterpret_cast<float*>(a->data[0])[a->frames-1];
        probe.r=reinterpret_cast<float*>(a->data[1])[a->frames-1];++probe.packets;
    }
    return a;
}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_stereo_filter")){obs_shutdown();return 5;}
    obs_source_info fixture{.id="stereo_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="stereo_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("stereo_test_source","Stereo test",nullptr,nullptr);
    auto* watcher=obs_source_create_private("stereo_test_probe","downstream",nullptr);obs_source_filter_add(source,watcher);
    auto* s=obs_data_create();obs_data_set_double(s,"width",2);
    auto* filter=obs_source_create_private("hyperdsp_stereo_filter","Stereo",s);obs_source_filter_add(source,filter);
    obs_source_filter_set_order(source,filter,OBS_ORDER_MOVE_TOP);
    unsigned samples=0;auto start=os_gettime_ns();
    auto stream=[&](float left,float right){
        std::array<float,480> l{},r{};l.fill(left);r.fill(right);
        for(unsigned k=0;k<300;++k){
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
            a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
            a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
        }
    };
    auto close=[](float a,float b){return std::abs(a-b)<1e-5;};
    auto status=[&](double correlation,double loss){
        calldata_t d;calldata_init(&d);
        bool ok=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_stereo_status_v1",&d)&&calldata_bool(&d,"available")&&calldata_bool(&d,"correlation_valid")&&
            std::abs(calldata_float(&d,"correlation")-correlation)<.001&&std::abs(calldata_float(&d,"mono_loss_db")-loss)<.05;
        std::printf("STEREO_STATUS valid=%d correlation=%g loss=%g expected=%g/%g pass=%d\n",calldata_bool(&d,"available"),calldata_float(&d,"correlation"),calldata_float(&d,"mono_loss_db"),correlation,loss,ok);
        calldata_free(&d);return ok;
    };
    stream(.4,-.2);std::printf("STEREO_WIDE %g/%g\n",probe.l,probe.r);bool ok=status(-1,10*std::log10(.01/.37))&&probe.packets&&close(probe.l,.7)&&close(probe.r,-.5);
    obs_data_set_double(s,"width",0);obs_source_update(filter,s);
    {calldata_t d;calldata_init(&d);
     ok=ok&&proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_stereo_status_v1",&d)&&!calldata_bool(&d,"available");
     calldata_free(&d);}
    stream(.4,-.2);
    ok=ok&&close(probe.l,.1)&&close(probe.r,.1);
    // Meter resets at each explicit edit, then follows the new signal.
    std::printf("STEREO_MONO %g/%g\n",probe.l,probe.r);ok=status(1,0)&&ok;
    obs_data_set_int(s,"mode",0);obs_source_update(filter,s);stream(.4,-.2);ok=ok&&close(probe.l,.4)&&close(probe.r,-.2);
    auto available=[&](){calldata_t d;calldata_init(&d);bool result=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_stereo_status_v1",&d)&&calldata_bool(&d,"available");calldata_free(&d);return result;};
    obs_source_set_enabled(filter,false);ok=ok&&!available();obs_source_set_enabled(filter,true);
    stream(.4,-.2);ok=ok&&available();os_sleep_ms(550);ok=ok&&!available();
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_data_release(s);
    auto* graph=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);obs_source_filter_add(source,graph);obs_source_filter_set_order(source,graph,OBS_ORDER_MOVE_TOP);
    calldata_t d;calldata_init(&d);
    calldata_set_string(&d,"graph",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_stereo_filter:1","inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"width","name":"Width","default":1,"bindings":[{"node":0,"parameter":"width","low":0,"high":2}]}]})");
    calldata_set_bool(&d,"validate_only",false);
    ok=(proc_handler_call(obs_source_get_proc_handler(graph),"hyperdsp_graph_apply_v1",&d)&&calldata_bool(&d,"ok"))&&ok;calldata_free(&d);
    stream(.4,-.2);ok=ok&&close(probe.l,.7)&&close(probe.r,-.5);
    s=obs_source_get_settings(graph);obs_data_set_double(s,"graph_control_width",0);obs_source_update(graph,s);obs_data_release(s);
    stream(.4,-.2);ok=ok&&close(probe.l,.1)&&close(probe.r,.1);
    std::printf("STEREO_OBS_PASS=%d mono=%g/%g packets=%u\n",ok,probe.l,probe.r,probe.packets);
    obs_source_filter_remove(source,graph);obs_source_filter_remove(source,watcher);obs_source_release(graph);obs_source_release(watcher);auto* history=obs_data_create();obs_data_set_double(history,"width",2);
    bool continuous=history_contention::verify(source,"hyperdsp_stereo_filter",history,"unused_test_control",0,1);
    obs_data_release(history);ok=ok&&continuous;
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
