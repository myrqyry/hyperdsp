#include "framework/dsp/graph_monitor.hpp"
#include "framework/dsp/monitor_spectrum.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <cstdio>
#include <cmath>
#include <numbers>
#include <algorithm>
using namespace hyperdsp;
static const char* name(void*){return "Monitor test";}
static void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void destroy(void*){}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_source_info fixture{.id="monitor_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&fixture);
    auto* source=obs_source_create("monitor_test_source","Monitor test",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"level","name":"Level","default":0.5,"bindings":[{"node":0,"parameter":"gain_db","low":-12,"high":0}]}]})");
    auto* filter=obs_source_create_private("hyperdsp_graph_filter","Graph",settings);
    obs_source_filter_add(source,filter);
    GraphMonitorFrame frame{};
    auto read=[&](){
        calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&frame);
        bool called=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_graph_monitor_v1",&d);
        bool ready=called&&calldata_bool(&d,"available");calldata_free(&d);return ready;
    };
    std::array<float,480> l{},r{};
    uint64_t start=os_gettime_ns();unsigned samples=0;
    auto stream=[&](unsigned blocks){
        for(unsigned k=0;k<blocks;++k){
            for(unsigned i=0;i<480;++i){l[i]=.4f*std::sin(2*std::numbers::pi*(samples+i)*1500/48000);r[i]=-l[i];}
            obs_source_audio audio{};audio.data[0]=(uint8_t*)l.data();audio.data[1]=(uint8_t*)r.data();
            audio.frames=480;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=48000;
            audio.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&audio);samples+=480;
        }
    };
    stream(5);bool ok=!read();stream(5);ok=ok&&read();
    MonitorSpectrum analyzer;auto in=analyzer.analyze(frame.input),out=analyzer.analyze(frame.output);
    unsigned peak=std::max_element(in.begin(),in.end())-in.begin();
    ok=ok&&frame.rate==48000&&peak==32&&std::abs(in[peak]-20*std::log10(.4))<.1&&std::abs(out[peak]-in[peak]+6)<.1;
    ok=ok&&frame.control_count==1&&std::string(frame.controls[0].id.data())=="level"&&std::abs(frame.controls[0].effective-.5)<.001;
    std::printf("MONITOR peak=%u input=%g output=%g control=%g\n",peak,in[peak],out[peak],frame.controls[0].effective);
    // The production graph routes a square LFO to the macro; the
    // snapshot must report effective and manual values independently.
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"level","name":"Level","default":0.5,"bindings":[{"node":0,"parameter":"gain_db","low":-12,"high":0}]}],"routes":[{"control":"level","source":1,"shape":2,"rate":0.05,"amount":0.25}]})");
    obs_source_update(filter,settings);
    ok=ok&&!read();stream(8);ok=ok&&read();
    ok=ok&&frame.control_count==1&&std::abs(frame.controls[0].base-.5)<.001&&std::abs(frame.controls[0].effective-.75)<.001;
    std::printf("MONITOR_REACTIVE base=%g effective=%g\n",frame.controls[0].base,frame.controls[0].effective);
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);
    ok=ok&&!read();stream(5);ok=ok&&read()&&frame.control_count==0;
    for(unsigned i=0;i<frame.input.size();++i)ok=ok&&frame.input[i]==frame.output[i];
    os_sleep_ms(650);ok=ok&&!read();
    // Deterministic lease and window coverage, independent of wall-clock sleeps.
    GraphMonitor core;GraphMonitorFrame snap{};
    std::array<GraphMonitorControl,8> controls{};
    std::array<float,monitor_samples> input{},output{};input.fill(.25f);output.fill(.125f);
    const uint64_t t=1000000000ULL;
    ok=ok&&!core.begin(input.data(),monitor_samples,48000,t)&&!core.read(snap,t);
    ok=ok&&core.begin(input.data(),monitor_samples,48000,t+1);
    core.end(output.data(),monitor_samples,t+2,controls,0);
    ok=ok&&core.read(snap,t+3)&&snap.input[0]==.25f&&snap.output.back()==.125f;
    core.invalidate();ok=ok&&!core.read(snap,t+4);
    ok=ok&&!core.begin(input.data(),monitor_samples,48000,t+1000000005ULL);
    ok=ok&&!core.read(snap,t+1000000006ULL);
    ok=ok&&core.begin(input.data(),512,48000,t+1000000007ULL);
    core.end(output.data(),512,t+1000000008ULL,controls,0);
    ok=ok&&!core.read(snap,t+1000000009ULL);
    ok=ok&&core.begin(input.data(),512,48000,t+1000000010ULL);
    core.end(output.data(),512,t+1000000011ULL,controls,0);
    ok=ok&&core.read(snap,t+1000000012ULL)&&snap.input.back()==.25f;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();
    std::printf("GRAPH_MONITOR_PASS=%d\n",ok);return ok?0:1;
}
