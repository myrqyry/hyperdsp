#include "framework/dsp/graph_monitor.hpp"
#include "framework/dsp/monitor_spectrum.hpp"
#include "framework/dsp/monitor_analysis.hpp"
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
    auto stream=[&](unsigned blocks,double amplitude=.4){
        for(unsigned k=0;k<blocks;++k){
            for(unsigned i=0;i<480;++i){l[i]=amplitude*std::sin(2*std::numbers::pi*(samples+i)*1500/48000);r[i]=-l[i];}
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
    GraphMonitorFrameV2 stereo{};
    auto readStereo=[&](){
        calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&stereo);
        bool ready=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_graph_monitor_v2",&d)&&calldata_bool(&d,"available");calldata_free(&d);return ready;
    };
    ok=ok&&readStereo()&&stereo.stereo&&stereo.signal.sequence==frame.sequence;
    auto metrics=window_stereo(stereo.signal.output,stereo.output_right);
    ok=ok&&metrics.valid&&metrics.correlation_valid&&std::abs(metrics.correlation+1)<1e-6&&metrics.mono_loss_db<-100;
    for(unsigned i=0;i<monitor_samples;++i)ok=ok&&stereo.signal.input[i]==-stereo.input_right[i]&&stereo.signal.output[i]==-stereo.output_right[i];
    std::array<float,monitor_samples> silence{},unity{};unity.fill(.4f);
    auto mono=window_stereo(unity,unity),single=window_stereo(unity,silence);
    ok=ok&&mono.valid&&mono.correlation_valid&&std::abs(mono.correlation-1)<1e-6&&std::abs(mono.mono_loss_db)<1e-6;
    ok=ok&&single.valid&&!single.correlation_valid&&std::abs(single.mono_loss_db+3.0103)<.001&&!window_stereo(silence,silence).valid;
    auto delta=spectrum_difference(in,out);
    ok=ok&&delta.valid[peak]&&std::abs(delta.db[peak]+6)<.1&&!delta.valid[0];
    stereo.size=0;ok=ok&&!readStereo();stereo.size=sizeof(stereo);
    std::printf("MONITOR_STEREO correlation=%g fold=%g spectrum_delta=%g\n",metrics.correlation,metrics.mono_loss_db,delta.db[peak]);
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
    GraphMonitorFrameV3 telemetry{};
    auto readNodes=[&](){
        calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&telemetry);
        bool ready=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_graph_monitor_v3",&d)&&calldata_bool(&d,"available");calldata_free(&d);return ready;
    };
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamic_eq_filter:1","params":{"frequency":1500,"threshold":-30,"range_db":-9},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_dynamics_filter:1","params":{"threshold":-30,"ratio":4},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_multiband_filter:1","params":{"mid_threshold":-30,"mid_ratio":4,"low_ratio":1,"high_ratio":1},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":0.3},{"source":1,"weight":0.3},{"source":2,"weight":0.3}]}],"output":3})");
    obs_source_update(filter,settings);ok=ok&&!readNodes();stream(100);ok=ok&&readNodes();
    const auto& n=telemetry.nodes;
    ok=ok&&telemetry.node_count==4&&n[0].kind==TelemetryKind::DynamicEq&&n[1].kind==TelemetryKind::Dynamics&&n[2].kind==TelemetryKind::Multiband&&n[3].kind==TelemetryKind::None;
    ok=ok&&std::abs(n[0].frequency-1500)<.1&&std::abs(n[0].gain_db[0]+9)<.02&&std::abs(n[1].gain_db[0]+16.5)<.2&&n[2].gain_db[1]<-10&&n[2].gain_db[0]==0&&n[2].gain_db[2]==0;
    ok=ok&&n[2].peak[1]>n[2].peak[0]&&n[2].peak[1]>n[2].peak[2];
    ok=ok&&std::abs(dynamic_eq_frozen_db(n[0],48000,1500)+9)<.02&&std::abs(dynamic_eq_frozen_db(n[0],48000,20))<.01;
    std::printf("NODE_TELEMETRY eq=%g compressor=%g mid=%g peaks=%g/%g/%g\n",n[0].gain_db[0],n[1].gain_db[0],n[2].gain_db[1],n[2].peak[0],n[2].peak[1],n[2].peak[2]);
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_transient_filter:1","params":{"attack_db":12,"sustain_db":-12},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_transient_filter:2","params":{"mid_attack_db":12,"mid_sustain_db":-12},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":0.5},{"source":1,"weight":0.5}]}],"output":2})");
    obs_source_update(filter,settings);ok=ok&&!readNodes();
    stream(3);ok=ok&&readNodes()&&telemetry.node_count==3&&n[0].kind==TelemetryKind::Transient&&n[1].kind==TelemetryKind::BandTransient;
    bool onset=n[0].peak[0]>.15&&n[0].gain_db[0]>1&&n[1].peak[1]>.15&&n[1].gain_db[1]>1;
    std::printf("GRAPH_ONSET values=%g/%g %g/%g kinds=%u/%u count=%u\n",n[0].peak[0],n[0].gain_db[0],n[1].peak[1],n[1].gain_db[1],unsigned(n[0].kind),unsigned(n[1].kind),telemetry.node_count);
    stream(50);stream(6,.04);ok=ok&&readNodes();
    bool decay=n[0].peak[0]<-.3&&n[0].gain_db[0]<-3&&n[1].peak[1]<-.3&&n[1].gain_db[1]<-3;
    ok=ok&&onset&&decay&&n[1].gain_db[0]==0&&n[1].gain_db[2]==0&&n[1].high_hz==2500;
    std::printf("GRAPH_CONTOUR onset=%d decay=%d broadband=%g/%g mid=%g/%g\n",onset,decay,n[0].peak[0],n[0].gain_db[0],n[1].peak[1],n[1].gain_db[1]);
    telemetry.size=0;ok=ok&&!readNodes();telemetry.size=sizeof(telemetry);
    obs_data_set_string(settings,"graph_json",R"({"schema":1,"nodes":[],"output":-1})");obs_source_update(filter,settings);
    ok=ok&&!readNodes();stream(5);ok=ok&&readNodes()&&telemetry.node_count==0;
    os_sleep_ms(650);ok=ok&&!read()&&!readStereo()&&!readNodes();
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
    GraphMonitorFrameV2 pair{};
    core.invalidate();core.read(pair,t+1000000013ULL);
    ok=ok&&core.begin(input.data(),512,48000,t+1000000014ULL,input.data());
    core.end(output.data(),512,t+1000000015ULL,controls,0,output.data());
    ok=ok&&core.begin(input.data(),512,48000,t+1000000016ULL);
    core.end(output.data(),512,t+1000000017ULL,controls,0);
    ok=ok&&!core.read(pair,t+1000000018ULL); // never combine mono and stereo halves
    ok=ok&&core.begin(input.data(),512,48000,t+1000000019ULL);
    core.end(output.data(),512,t+1000000020ULL,controls,0);
    ok=ok&&core.read(pair,t+1000000021ULL)&&!pair.stereo;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();
    std::printf("GRAPH_MONITOR_PASS=%d\n",ok);return ok?0:1;
}
