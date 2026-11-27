// Linux-only opt-in timing probe. Wrap the actual exported OBS filter info,
// but run it through the real libobs filter chain; no production instrumentation.
#include <obs-module.h>
#include <util/platform.h>
#include <dlfcn.h>
#include <time.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "latency-contention-fixture.hpp"
#include "framework/dsp/graph_monitor.hpp"

namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned max_frames=480,blocks=2000,rate=48000,warmup=100;
unsigned frames=480;
bool telemetry=false;
const char* ladspa_path=nullptr;
double budget_us(){return double(frames)*1e6/rate;}
using AllocBegin=void(*)();
using AllocEnd=void(*)(uint64_t*,uint64_t*);
AllocBegin alloc_begin=nullptr;
AllocEnd alloc_end=nullptr;
struct Probe {
    const obs_source_info* original=nullptr;
    std::vector<std::array<float,2>> reference;
    std::vector<double> wall;
    std::vector<unsigned> packet_sizes;
    std::vector<int64_t> packet_offsets;
    uint64_t anchor=0;
    double max_overrun_cpu=0;
    unsigned overrun_cpu=0;
    bool clock=false;
    uint64_t samples=0,errors=0,allocations=0,releases=0;
    double cpu_us=0,energy=0,difference=0;
    bool compare=false,record=false;
};
Probe* probe=nullptr;
double cpu_us(){
    timespec t{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return 0;
    return double(t.tv_sec)*1e6+double(t.tv_nsec)/1e3;
}
obs_audio_data* timed(void* ptr,obs_audio_data* data){
    auto& p=*probe;
    if(!p.record)return p.original->filter_audio(ptr,data);
    const auto input_stamp=data->timestamp;
    double cpu_start=cpu_us();
    if(alloc_begin)alloc_begin();
    auto start=Clock::now();
    auto* out=p.original->filter_audio(ptr,data);
    auto stop=Clock::now();
    if(alloc_end){uint64_t a=0,r=0;alloc_end(&a,&r);p.allocations+=a;p.releases+=r;}
    double cpu_end=cpu_us();
    double elapsed=std::chrono::duration<double,std::micro>(stop-start).count(),cpu=cpu_end-cpu_start;
    p.wall.push_back(elapsed);p.cpu_us+=cpu;
    if(elapsed>budget_us()){p.max_overrun_cpu=std::max(p.max_overrun_cpu,cpu);if(cpu>budget_us())++p.overrun_cpu;}
    auto count=out?out->frames:0;
    if(p.clock){
        auto i=p.wall.size()-1;
        int64_t offset=out?int64_t(out->timestamp)-int64_t(input_stamp):0;
        if(!p.compare){p.packet_sizes.push_back(count);p.packet_offsets.push_back(offset);}
        else if(i>=p.packet_sizes.size()||p.packet_sizes[i]!=count||p.packet_offsets[i]!=offset)++p.errors;
        if(!count)return out;
        if(!p.samples)p.anchor=out->timestamp;
        if(out->timestamp!=p.anchor+p.samples*1000000000ULL/rate) {
            // The warm-up may leave a fractional nanosecond at the measurement boundary.
            auto expected=p.anchor+p.samples*1000000000ULL/rate;
            if(out->timestamp>expected+1||expected>out->timestamp+1)++p.errors;
        }
        if(count>frames+8){++p.errors;return out;}
    }else if(count!=frames){++p.errors;return out;}
    if(!out||!out->data[0]||!out->data[1]){++p.errors;return out;}
    auto* l=reinterpret_cast<float*>(out->data[0]);auto* r=reinterpret_cast<float*>(out->data[1]);
    for(unsigned j=0;j<out->frames;++j,++p.samples){
        if(!std::isfinite(l[j])||!std::isfinite(r[j])||(p.clock&&std::abs(r[j]+.4f*l[j])>1e-5f)){++p.errors;continue;}
        if(!p.compare){p.reference.push_back({l[j],r[j]});p.energy+=double(l[j])*l[j];p.difference+=std::abs(l[j]-latency_contention::sample(p.samples+warmup*frames));}
        else if(p.samples>=p.reference.size()||std::abs(l[j]-p.reference[p.samples][0])>1e-6f||
                std::abs(r[j]-p.reference[p.samples][1])>1e-6f)++p.errors;
    }
    return out;
}
const char* name(void*){return "HyperDSP timing source";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
struct Case {const char* symbol;const char* label;const char* id;void(*settings)(obs_data_t*);};
const Case cases[]={
    {"dynamics_filter","limiter","hyperdsp_bench_limiter",[](obs_data_t* s){obs_data_set_int(s,"mode",4);obs_data_set_double(s,"threshold",-20);}},
    {"color_filter","color_4x","hyperdsp_bench_color_4x",[](obs_data_t* s){obs_data_set_int(s,"mode",3);obs_data_set_int(s,"oversample",4);obs_data_set_double(s,"drive",12);}},
    {"spectral_filter","spectral_gate","hyperdsp_bench_spectral_gate",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"threshold",-35);}},
    {"time_filter","echo","hyperdsp_bench_echo",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"delay_ms",37);obs_data_set_double(s,"feedback",.65);}},
    {"space_filter","reverb","hyperdsp_bench_reverb",[](obs_data_t* s){obs_data_set_int(s,"mode",2);obs_data_set_double(s,"decay",1.2);}},
    {"dynamic_eq_filter","dynamic_eq","hyperdsp_bench_dynamic_eq",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"frequency",1500);obs_data_set_double(s,"threshold",-40);obs_data_set_double(s,"range_db",-12);}},
    {"multiband_filter","multiband","hyperdsp_bench_multiband",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"low_threshold",-40);obs_data_set_double(s,"low_ratio",8);}},
    {"transient_filter","transient","hyperdsp_bench_transient",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"attack_db",12);obs_data_set_double(s,"sustain_db",12);}},
    {"transient_filter","band_transient","hyperdsp_bench_band_transient",[](obs_data_t* s){obs_data_set_int(s,"mode",2);obs_data_set_double(s,"low_attack_db",12);obs_data_set_double(s,"high_sustain_db",-12);}},
    {"native_filter","native_lowpass","hyperdsp_bench_native_lowpass",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"frequency",800);}},
    {"formant_filter","formant","hyperdsp_bench_formant",[](obs_data_t* s){obs_data_set_int(s,"mode",1);obs_data_set_double(s,"morph",.4);}},
    {"stereo_filter","stereo","hyperdsp_bench_stereo",[](obs_data_t* s){obs_data_set_double(s,"width",2);}},
    {"mic_filter","microphone","hyperdsp_bench_microphone",[](obs_data_t* s){obs_data_set_int(s,"mic_type",3);}},
    {"ladspa_filter","ladspa_amp","hyperdsp_bench_ladspa_amp",[](obs_data_t* s){obs_data_set_string(s,"library",ladspa_path);obs_data_set_string(s,"label","amp_stereo");obs_data_set_double(s,"port_0",.25);}},
    {"clock_sync_filter","clock_sync","hyperdsp_bench_clock_sync",[](obs_data_t* s){obs_data_set_int(s,"clock",0);obs_data_set_int(s,"quality",1);}},
    {"graph_filter","graph_chain","hyperdsp_bench_graph_chain",[](obs_data_t* s){obs_data_set_string(s,"graph_json",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamics_filter:1","params":{"threshold":-20,"ratio":4},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_spectral_filter:1","params":{"threshold":-35},"inputs":[{"source":0,"weight":1}]},{"effect":"native:hyperdsp_space_filter:2","params":{"decay":1.2},"inputs":[{"source":1,"weight":1}]}],"output":2})");}}
};
bool poll(obs_source_t* filter,const Case& c){
    hyperdsp::GraphMonitorFrameV3 graph{};
    hyperdsp::FilterTelemetryFrame node{};
    std::string_view symbol=c.symbol;
    const char* proc=nullptr;void* frame=nullptr;
    if(symbol=="graph_filter"){proc="hyperdsp_graph_monitor_v3";frame=&graph;}
    else if(symbol=="dynamics_filter"||symbol=="dynamic_eq_filter"||symbol=="multiband_filter"||symbol=="transient_filter"){proc="hyperdsp_filter_telemetry_v1";frame=&node;}
    else if(symbol=="clock_sync_filter")proc="hyperdsp_clock_sync_status_v1";
    else if(symbol=="stereo_filter")proc="hyperdsp_stereo_status_v1";
    if(!proc)return false;
    calldata_t data;calldata_init(&data);if(frame)calldata_set_ptr(&data,"frame",frame);
    bool ready=proc_handler_call(obs_source_get_proc_handler(filter),proc,&data)&&calldata_bool(&data,"available");
    calldata_free(&data);return ready;
}
bool has_telemetry(const Case& c){
    std::string_view s=c.symbol;
    return s=="graph_filter"||s=="dynamics_filter"||s=="dynamic_eq_filter"||s=="multiband_filter"||s=="transient_filter"||s=="clock_sync_filter"||s=="stereo_filter";
}
bool run(void* module,obs_source_t* source,const Case& c,bool strict){
    if(std::string_view(c.symbol)=="ladspa_filter"&&!ladspa_path){std::printf("BENCH_SKIP effect=%s reason=no_ladspa_fixture\n",c.label);return true;}
    auto* original=static_cast<const obs_source_info*>(dlsym(module,c.symbol));
    if(!original||!original->filter_audio){std::fprintf(stderr,"missing %s\n",c.symbol);return false;}
    Probe p;p.original=original;p.clock=std::string_view(c.symbol)=="clock_sync_filter";p.reference.reserve(blocks*(frames+8));p.wall.reserve(blocks);p.packet_sizes.reserve(blocks);p.packet_offsets.reserve(blocks);
    probe=&p;
    obs_source_info info=*original;info.id=c.id;info.filter_audio=timed;obs_register_source(&info);
    auto* settings=obs_data_create();c.settings(settings);
    bool ok=true;
    for(bool concurrent:{false,true}){
        auto* filter=obs_source_create_private(c.id,c.label,settings);
        if(!filter){ok=false;break;}
        obs_source_filter_add(source,filter);
        p.compare=concurrent;p.samples=p.errors=p.allocations=p.releases=0;p.cpu_us=0;p.wall.clear();p.overrun_cpu=0;p.max_overrun_cpu=0;p.anchor=0;
        std::atomic<bool> ready{false},stop{false};unsigned updates=0;std::thread writer,reader;
        std::atomic<unsigned> reads{0};
        const bool monitor=telemetry&&has_telemetry(c);
        auto stamp=os_gettime_ns();
        auto stream=[&](unsigned count,unsigned offset){
            std::array<float,max_frames> l{},r{};
            for(unsigned k=0;k<count;++k){
                for(unsigned j=0;j<frames;++j){l[j]=latency_contention::sample(uint64_t(k+offset)*frames+j);r[j]=-.4f*l[j];}
                obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(l.data());audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
                audio.frames=frames;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=rate;
                audio.timestamp=stamp+uint64_t(k+offset)*frames*(p.clock?1000400000ULL:1000000000ULL)/rate;obs_source_output_audio(source,&audio);
                if(monitor&&k%8==0&&poll(filter,c))++reads;
            }
        };
        p.record=false;if(monitor)poll(filter,c);stream(warmup,0);
        if(concurrent){
            writer=std::thread([&]{
                while(!stop.load()){
                    obs_data_set_double(settings,"benchmark_unused",updates%2);
                    obs_source_update(filter,settings);++updates;ready=true;
                }
            });
            while(!ready.load())std::this_thread::yield();
        }
        if(monitor)reader=std::thread([&]{while(!stop.load()){if(poll(filter,c))++reads;std::this_thread::yield();}});
        p.record=true;stream(blocks,warmup);p.record=false;
        stop=true;if(writer.joinable())writer.join();if(reader.joinable())reader.join();
        obs_source_filter_remove(source,filter);obs_source_release(filter);
        if(p.wall.empty()){ok=false;break;}
        double sum=0;unsigned missed=0;for(double v:p.wall){sum+=v;if(v>budget_us())++missed;}
        std::sort(p.wall.begin(),p.wall.end());
        auto pct=[&](double q){return p.wall[std::min(p.wall.size()-1,size_t(q*(p.wall.size()-1)))];};
        const bool valid=p.wall.size()==blocks&&(p.clock?std::abs(double(p.samples)-blocks*frames)<frames:p.samples==uint64_t(blocks)*frames)&&p.energy>1e-6&&(p.clock?std::any_of(p.packet_sizes.begin(),p.packet_sizes.end(),[](unsigned n){return n!=frames;}):p.difference>1)&&(!monitor||concurrent||reads>0)&&(!concurrent||updates>10);
        std::printf("BENCH effect=%s phase=%s frames=%u telemetry_reads=%u blocks=%zu updates=%u samples=%llu errors=%llu mean_us=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f cpu_budget_pct=%.3f overruns=%u cpu_overruns=%u max_overrun_cpu_us=%.2f energy=%.6g allocation_calls=%llu free_calls=%llu audit=%d valid=%d\n",
            c.label,concurrent?"edits":"idle",frames,reads.load(),p.wall.size(),updates,(unsigned long long)p.samples,(unsigned long long)p.errors,
            sum/p.wall.size(),pct(.5),pct(.95),pct(.99),p.wall.back(),p.cpu_us/(p.wall.size()*budget_us())*100,missed,p.overrun_cpu,p.max_overrun_cpu,p.energy,(unsigned long long)p.allocations,(unsigned long long)p.releases,bool(alloc_begin),valid);
        ok=ok&&valid&&((!strict&&concurrent)||!p.errors)&&(!strict||!alloc_begin||(!p.allocations&&!p.releases));
    }
    obs_data_release(settings);probe=nullptr;return ok;
}
}
int main(int argc,char** argv){
    if(argc<3){std::fprintf(stderr,"usage: %s module.so data-dir [--allow-mismatch] [--frames 120|480] [--telemetry] [--ladspa /path/amp.so] [--case name]\n",argv[0]);return 2;}
    bool strict=true;std::string_view selected;
    for(int i=3;i<argc;++i){
        std::string_view option=argv[i];
        if(option=="--allow-mismatch")strict=false;
        else if(option=="--telemetry")telemetry=true;
        else if(option=="--frames"&&i+1<argc){std::string_view n=argv[++i];if(n=="120")frames=120;else if(n=="480")frames=480;else return 2;}
        else if(option=="--ladspa"&&i+1<argc)ladspa_path=argv[++i];
        else if(option=="--case"&&i+1<argc)selected=argv[++i];
        else return 2;
    }
    alloc_begin=reinterpret_cast<AllocBegin>(dlsym(RTLD_DEFAULT,"hyperdsp_bench_alloc_begin"));
    alloc_end=reinterpret_cast<AllocEnd>(dlsym(RTLD_DEFAULT,"hyperdsp_bench_alloc_end"));
    if(bool(alloc_begin)!=bool(alloc_end))return 2;
    if(alloc_begin){
        auto allocate=reinterpret_cast<void*(*)(size_t)>(dlsym(RTLD_DEFAULT,"malloc"));
        auto release=reinterpret_cast<void(*)(void*)>(dlsym(RTLD_DEFAULT,"free"));
        if(!allocate||!release)return 2;
        uint64_t a=0,r=0;alloc_begin();auto* p=allocate(128);release(p);alloc_end(&a,&r);
        if(a!=1||r!=1){std::fprintf(stderr,"allocation audit self-check failed\n");return 2;}
        std::printf("ALLOC_AUDIT_SELF_CHECK=1\n");
    }
    if(!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=rate;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* obs_module=nullptr;
    if(obs_open_module(&obs_module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(obs_module))return 4;
    auto* module=dlopen(argv[1],RTLD_NOW|RTLD_NOLOAD);
    if(!module){std::fprintf(stderr,"%s\n",dlerror());return 5;}
    obs_source_info source_info{.id="hyperdsp_benchmark_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&source_info);
    auto* source=obs_source_create(source_info.id,"Benchmark",nullptr,nullptr);
    if(!source)return 6;
    bool ok=true;unsigned ran=0;for(const auto& c:cases)if(selected.empty()||selected==c.label){ok=run(module,source,c,strict)&&ok;++ran;}
    ok=ok&&ran>0&&!(selected=="ladspa_amp"&&!ladspa_path);
    obs_source_release(source);dlclose(module);obs_shutdown();
    std::printf("BENCH_PASS=%d strict=%d\n",ok,strict);return ok?0:1;
}
