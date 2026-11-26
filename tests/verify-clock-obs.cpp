#include <obs-module.h>
#include <util/platform.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

struct Probe {
    uint64_t frames=0,packets=0,anchor=0;
    bool ok=true;
};
static Probe probe;
static const char* name(void*){return "Clock test";}
static void* create(obs_data_t*,obs_source_t*){return &probe;}
static void destroy(void*){}
static obs_audio_data* inspect(void*,obs_audio_data* a){
    if(!a||!a->frames)return a;
    if(!probe.packets)probe.anchor=a->timestamp;
    uint64_t expected=probe.anchor+probe.frames*1000000000ULL/48000;
    probe.ok=probe.ok&&a->timestamp==expected;
    auto* l=reinterpret_cast<float*>(a->data[0]);auto* r=reinterpret_cast<float*>(a->data[1]);
    if(!l||!r)probe.ok=false;
    else for(unsigned j=0;j<a->frames;++j)probe.ok=probe.ok&&std::isfinite(l[j])&&std::abs(r[j]+.4f*l[j])<1e-5;
    probe.frames+=a->frames;++probe.packets;return a;
}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_clock_sync_filter")){obs_shutdown();return 5;}
    obs_source_info fixture{.id="clock_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_source_info monitor{.id="clock_test_probe",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy,.filter_audio=inspect};
    obs_register_source(&fixture);obs_register_source(&monitor);
    auto* source=obs_source_create("clock_test_source","clock test",nullptr,nullptr);
    auto* clock=obs_source_create_private("hyperdsp_clock_sync_filter","clock sync",nullptr);
    auto* watcher=obs_source_create_private("clock_test_probe","downstream",nullptr);
    if(!source||!clock||!watcher)return 6;
    obs_source_filter_add(source,clock);obs_source_filter_add(source,watcher);
    obs_source_filter_set_order(source,clock,OBS_ORDER_MOVE_TOP);
    auto* props=obs_source_properties(clock);
    bool ok=props&&obs_properties_get(props,"quality")&&obs_properties_get(props,"limit_ppm")&&obs_properties_get(props,"clock");
    if(props)obs_properties_destroy(props);
    std::array<float,480> l{},r{};
    auto start=os_gettime_ns();
    auto stream=[&](unsigned count,uint64_t base,double ppm){
        for(unsigned k=0;k<count;++k){
            for(unsigned j=0;j<480;++j){l[j]=.4f*std::sin((k*480+j)*.071);r[j]=-.4f*l[j];}
            obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(l.data());audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
            audio.frames=480;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=48000;
            audio.timestamp=base+uint64_t(k*10000000.0*(1+ppm/1e6));
            obs_source_output_audio(source,&audio);
        }
    };
    stream(15000,start,400);
    calldata_t d;calldata_init(&d);
    bool called=proc_handler_call(obs_source_get_proc_handler(clock),"hyperdsp_clock_sync_status_v1",&d);
    double correction=calldata_float(&d,"correction_ppm");
    bool locked=calldata_bool(&d,"locked");
    ok=ok&&called&&calldata_bool(&d,"available")&&locked&&std::abs(correction-400)<35&&probe.ok&&probe.packets>14990&&
        std::abs(double(probe.frames)-7202880)<250;
    std::printf("CLOCK_OBS packets=%llu frames=%llu correction=%g locked=%d continuity=%d pass=%d\n",
        (unsigned long long)probe.packets,(unsigned long long)probe.frames,correction,locked,probe.ok,ok);
    calldata_free(&d);
    probe={};stream(1,start+200000000000ULL,0);
    calldata_init(&d);proc_handler_call(obs_source_get_proc_handler(clock),"hyperdsp_clock_sync_status_v1",&d);
    ok=ok&&!calldata_bool(&d,"locked")&&calldata_int(&d,"relocks")==1&&probe.ok&&probe.packets==1;
    calldata_free(&d);
    auto* settings=obs_source_get_settings(clock);
    obs_data_set_int(settings,"quality",0);obs_data_set_double(settings,"limit_ppm",50);
    obs_source_update(clock,settings);obs_data_release(settings);
    calldata_init(&d);proc_handler_call(obs_source_get_proc_handler(clock),"hyperdsp_clock_sync_status_v1",&d);
    ok=ok&&calldata_bool(&d,"available")&&!calldata_bool(&d,"locked")&&std::abs(calldata_float(&d,"ratio")-1)<1e-9;
    calldata_free(&d);
    obs_source_filter_remove(source,watcher);obs_source_filter_remove(source,clock);
    obs_source_release(watcher);obs_source_release(clock);obs_source_release(source);obs_shutdown();
    std::printf("CLOCK_OBS_PASS=%d\n",ok);return ok?0:1;
}
