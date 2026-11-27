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
constexpr unsigned max_frames=480,blocks=2000,segment_blocks=250;
unsigned rate=48000,channels=2;
speaker_layout layout=SPEAKERS_STEREO;
constexpr std::array<float,8> channel_scale{1,-.4f,.7f,-.2f,.5f,-.8f,.3f,-.6f};
unsigned warmup=100;
bool transitions=false;
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
    std::vector<std::array<float,8>> reference;
    std::vector<double> wall;
    std::vector<unsigned> packet_sizes;
    std::vector<int64_t> packet_offsets;
    uint64_t anchor=0,segment_samples=0,segment_input_stamp=0;
    double first_us=0,transition_max_us=0;
    unsigned transition_allocations=0,transition_releases=0;
    std::array<double,blocks/segment_blocks> segment_difference{};
    double max_overrun_cpu=0;
    unsigned overrun_cpu=0;
    bool clock=false,ladspa=false;
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
    if(alloc_end){uint64_t a=0,r=0;alloc_end(&a,&r);p.allocations+=a;p.releases+=r;
        if(p.wall.empty()||(transitions&&p.wall.size()%segment_blocks==0)){p.transition_allocations+=a;p.transition_releases+=r;}
    }
    double cpu_end=cpu_us();
    double elapsed=std::chrono::duration<double,std::micro>(stop-start).count(),cpu=cpu_end-cpu_start;
    auto block=p.wall.size();
    if(!block)p.first_us=elapsed;
    if(transitions&&block%segment_blocks==0){
        p.transition_max_us=std::max(p.transition_max_us,elapsed);
    }
    p.wall.push_back(elapsed);p.cpu_us+=cpu;
    if(elapsed>budget_us()){p.max_overrun_cpu=std::max(p.max_overrun_cpu,cpu);if(cpu>budget_us())++p.overrun_cpu;}
    auto count=out?out->frames:0;
    if(p.clock){
        auto i=p.wall.size()-1;
        int64_t offset=out?int64_t(out->timestamp)-int64_t(input_stamp):0;
        if(!p.compare){p.packet_sizes.push_back(count);p.packet_offsets.push_back(offset);}
        else if(i>=p.packet_sizes.size()||p.packet_sizes[i]!=count||p.packet_offsets[i]!=offset)++p.errors;
        if((transitions&&block%segment_blocks==0)||(!warmup&&!block)){
            p.anchor=0;p.segment_samples=0;p.segment_input_stamp=input_stamp;
            // Resampler adoption must really re-prime, not retain the previous
            // engine. The first packet may legitimately produce no samples.
            if(count>=frames)++p.errors;
        }
        if(!count)return out;
        if(!p.segment_samples){
            p.anchor=out->timestamp;
            if(p.segment_input_stamp&&p.anchor!=p.segment_input_stamp)++p.errors;
        }
        if(out->timestamp!=p.anchor+p.segment_samples*1000000000ULL/rate) {
            // The warm-up may leave a fractional nanosecond at the measurement boundary.
            auto expected=p.anchor+p.segment_samples*1000000000ULL/rate;
            if(out->timestamp>expected+1||expected>out->timestamp+1)++p.errors;
        }
        if(count>frames+8){++p.errors;return out;}
        p.segment_samples+=count;
    }else if(count!=frames){++p.errors;return out;}
    if(!out){++p.errors;return out;}
    std::array<const float*,8> output{};
    for(unsigned c=0;c<channels;++c){
        output[c]=reinterpret_cast<const float*>(out->data[c]);
        if(!output[c]){++p.errors;return out;}
    }
    for(unsigned j=0;j<out->frames;++j,++p.samples){
        std::array<float,8> sample{};
        bool valid=true;
        for(unsigned c=0;c<channels;++c){
            sample[c]=output[c][j];
            if(!std::isfinite(sample[c])||(p.clock&&std::abs(sample[c]-channel_scale[c]*sample[0])>1e-5f))valid=false;
            if(p.ladspa){
                const float gain=transitions&&(block/segment_blocks)%2?.5f:.25f;
                const float expected=channel_scale[c]*latency_contention::sample(p.samples+warmup*frames)*gain;
                if(std::abs(sample[c]-expected)>1e-6f)valid=false;
            }
        }
        if(!valid){++p.errors;continue;}
        if(!p.compare){
            p.reference.push_back(sample);
            auto input=latency_contention::sample(p.samples+warmup*frames);
            for(unsigned c=0;c<channels;++c){
                p.energy+=double(sample[c])*sample[c];
                auto delta=std::abs(sample[c]-channel_scale[c]*input);
                p.difference+=delta;p.segment_difference[block/segment_blocks]+=delta;
            }
        }else{
            if(p.samples>=p.reference.size()){++p.errors;continue;}
            for(unsigned c=0;c<channels;++c)
                if(std::abs(sample[c]-p.reference[p.samples][c])>1e-6f){++p.errors;break;}
        }
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
    {"ladspa_filter","ladspa_amp","hyperdsp_bench_ladspa_amp",[](obs_data_t* s){obs_data_set_string(s,"library",ladspa_path);obs_data_set_string(s,"label",channels==2?"amp_stereo":"amp_mono");obs_data_set_double(s,"port_0",.25);}},
    {"clock_sync_filter","clock_sync","hyperdsp_bench_clock_sync",[](obs_data_t* s){obs_data_set_int(s,"clock",0);obs_data_set_int(s,"quality",1);}},
    {"graph_filter","graph_chain","hyperdsp_bench_graph_chain",[](obs_data_t* s){obs_data_set_string(s,"graph_json",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamics_filter:1","params":{"threshold":-20,"ratio":4},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_spectral_filter:1","params":{"threshold":-35},"inputs":[{"source":0,"weight":1}]},{"effect":"native:hyperdsp_space_filter:2","params":{"decay":1.2},"inputs":[{"source":1,"weight":1}]}],"output":2})");}}
};
void structural_settings(const Case& c,obs_data_t* s,bool alternate){
    c.settings(s);
    if(!alternate)return;
    std::string_view id=c.symbol;
    if(id=="color_filter")obs_data_set_int(s,"oversample",1);
    else if(id=="spectral_filter")obs_data_set_int(s,"mode",0);
    else if(id=="time_filter"){obs_data_set_int(s,"mode",2);}
    else if(id=="space_filter"){obs_data_set_int(s,"mode",1);obs_data_set_int(s,"stages",4);}
    else if(id=="native_filter"){obs_data_set_int(s,"mode",2);obs_data_set_int(s,"stages",2);}
    else if(id=="formant_filter")obs_data_set_int(s,"mode",0);
    else if(id=="mic_filter")obs_data_set_int(s,"mic_type",0);
    else if(id=="transient_filter")obs_data_set_int(s,"mode",std::string_view(c.label)=="transient"?2:1);
    else if(id=="clock_sync_filter")obs_data_set_int(s,"quality",2);
    else if(id=="ladspa_filter"){obs_data_set_string(s,"label","amp_mono");obs_data_set_double(s,"port_0",.5);}
    else if(id=="graph_filter")obs_data_set_string(s,"graph_json",R"({"schema":1,"nodes":[],"output":-1})");
    else obs_data_set_int(s,"mode",0);
}
bool is_dry_transition(const Case& c){
    std::string_view id=c.symbol;
    return id=="spectral_filter"||id=="formant_filter"||id=="mic_filter"||id=="graph_filter"||id=="dynamics_filter"||id=="dynamic_eq_filter"||id=="multiband_filter"||id=="stereo_filter";
}
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
    Probe p;p.original=original;p.clock=std::string_view(c.symbol)=="clock_sync_filter";p.ladspa=std::string_view(c.symbol)=="ladspa_filter";p.reference.reserve(blocks*(frames+8));p.wall.reserve(blocks);p.packet_sizes.reserve(blocks);p.packet_offsets.reserve(blocks);
    probe=&p;
    obs_source_info info=*original;info.id=c.id;info.filter_audio=timed;obs_register_source(&info);
    bool ok=true;
    for(bool concurrent:{false,true}){
        auto* settings=obs_data_create();c.settings(settings);
        auto* filter=obs_source_create_private(c.id,c.label,settings);
        if(!filter){obs_data_release(settings);ok=false;break;}
        obs_source_filter_add(source,filter);
        p.compare=concurrent;p.samples=p.errors=p.allocations=p.releases=0;p.cpu_us=0;p.wall.clear();p.overrun_cpu=0;p.max_overrun_cpu=0;p.anchor=0;p.segment_samples=0;p.segment_input_stamp=0;p.first_us=p.transition_max_us=0;p.transition_allocations=p.transition_releases=0;
        std::atomic<bool> ready{false},stop{false};std::atomic<unsigned> requested{0},applied{0};unsigned updates=0;std::thread writer,reader;
        std::atomic<unsigned> reads{0};
        const bool expected_dry=channels!=2&&std::string_view(c.symbol)=="stereo_filter";
        const bool monitor=telemetry&&has_telemetry(c)&&!expected_dry;
        auto stamp=os_gettime_ns();
        auto change=[&](unsigned segment){
            // Every structural key is restored explicitly on return to the original mode.
            auto* next=obs_data_create();structural_settings(c,next,segment%2);
            if(std::string_view(c.symbol)=="native_filter"&&!(segment%2))obs_data_set_int(next,"stages",1);
            if(std::string_view(c.symbol)=="space_filter"&&!(segment%2))obs_data_set_int(next,"stages",6);
            if(std::string_view(c.symbol)=="stereo_filter"&&!(segment%2))obs_data_set_int(next,"mode",1);
            obs_source_update(filter,next);obs_data_release(next);++updates;
        };
        auto stream=[&](unsigned count,unsigned offset){
            std::array<std::array<float,max_frames>,8> input{};
            for(unsigned k=0;k<count;++k){
                if(transitions&&p.record&&k&&k%segment_blocks==0){
                    auto segment=k/segment_blocks;
                    if(concurrent){requested=segment;while(applied.load()!=segment)std::this_thread::yield();}
                    else change(segment);
                }
                for(unsigned j=0;j<frames;++j){
                    auto sample=latency_contention::sample(uint64_t(k+offset)*frames+j);
                    for(unsigned c=0;c<channels;++c)input[c][j]=channel_scale[c]*sample;
                }
                obs_source_audio audio{};
                for(unsigned c=0;c<channels;++c)audio.data[c]=reinterpret_cast<uint8_t*>(input[c].data());
                audio.frames=frames;audio.speakers=layout;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=rate;
                audio.timestamp=stamp+uint64_t(k+offset)*frames*(p.clock?1000400000ULL:1000000000ULL)/rate;obs_source_output_audio(source,&audio);
                if(monitor&&k%8==0&&poll(filter,c))++reads;
            }
        };
        p.record=false;if(monitor)poll(filter,c);stream(warmup,0);
        if(concurrent){
            writer=std::thread([&]{
                if(transitions){
                    ready=true;
                    while(!stop.load()){
                        auto segment=requested.load();
                        if(segment!=applied.load()){change(segment);applied=segment;}
                        else std::this_thread::yield();
                    }
                    return;
                }
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
        obs_source_filter_remove(source,filter);obs_source_release(filter);obs_data_release(settings);
        if(p.wall.empty()){ok=false;break;}
        double sum=0;unsigned missed=0;for(double v:p.wall){sum+=v;if(v>budget_us())++missed;}
        std::sort(p.wall.begin(),p.wall.end());
        auto pct=[&](double q){return p.wall[std::min(p.wall.size()-1,size_t(q*(p.wall.size()-1)))];};
        bool transitions_valid=true;
        if(transitions){
            if(updates!=blocks/segment_blocks-1)transitions_valid=false;
            if(!p.clock&&is_dry_transition(c)&&!expected_dry){
                for(unsigned i=0;i<p.segment_difference.size();++i){
                    if((i%2&&p.segment_difference[i]>1e-3)||(!(i%2)&&p.segment_difference[i]<1))transitions_valid=false;
                }
            }
        }
        const bool valid=p.wall.size()==blocks&&(p.clock?std::abs(double(p.samples)-blocks*frames)<(transitions?2000:frames):p.samples==uint64_t(blocks)*frames)&&p.energy>1e-6&&(p.clock?std::any_of(p.packet_sizes.begin(),p.packet_sizes.end(),[](unsigned n){return n!=frames;}):(expected_dry?p.difference<1e-3:p.difference>1))&&(!monitor||concurrent||reads>0)&&(transitions?transitions_valid:(!concurrent||updates>10));
        std::printf("BENCH effect=%s phase=%s cold=%d structural=%d frames=%u rate=%u channels=%u expected_dry=%d telemetry_reads=%u blocks=%zu updates=%u samples=%llu errors=%llu mean_us=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f cpu_budget_pct=%.3f overruns=%u cpu_overruns=%u max_overrun_cpu_us=%.2f energy=%.6g first_us=%.2f transition_max_us=%.2f transition_allocations=%u transition_frees=%u allocation_calls=%llu free_calls=%llu audit=%d valid=%d\n",
            c.label,transitions?(concurrent?"handoff":"inline"):(concurrent?"edits":"idle"),warmup==0,transitions,frames,rate,channels,expected_dry,reads.load(),p.wall.size(),updates,(unsigned long long)p.samples,(unsigned long long)p.errors,
            sum/p.wall.size(),pct(.5),pct(.95),pct(.99),p.wall.back(),p.cpu_us/(p.wall.size()*budget_us())*100,missed,p.overrun_cpu,p.max_overrun_cpu,p.energy,p.first_us,p.transition_max_us,p.transition_allocations,p.transition_releases,(unsigned long long)p.allocations,(unsigned long long)p.releases,bool(alloc_begin),valid);
        ok=ok&&valid&&((!strict&&concurrent)||!p.errors)&&(!strict||!alloc_begin||(!p.allocations&&!p.releases));
    }
    probe=nullptr;return ok;
}
}
int main(int argc,char** argv){
    if(argc<3){std::fprintf(stderr,"usage: %s module.so data-dir [--allow-mismatch] [--frames 120|480] [--rate 48000|96000] [--channels 1|2|6|8] [--telemetry] [--cold] [--transitions] [--ladspa /path/amp.so] [--case name]\n",argv[0]);return 2;}
    bool strict=true;std::string_view selected;
    for(int i=3;i<argc;++i){
        std::string_view option=argv[i];
        if(option=="--allow-mismatch")strict=false;
        else if(option=="--telemetry")telemetry=true;
        else if(option=="--cold")warmup=0;
        else if(option=="--transitions"){transitions=true;warmup=0;}
        else if(option=="--frames"&&i+1<argc){std::string_view n=argv[++i];if(n=="120")frames=120;else if(n=="480")frames=480;else return 2;}
        else if(option=="--rate"&&i+1<argc){std::string_view n=argv[++i];if(n=="48000")rate=48000;else if(n=="96000")rate=96000;else return 2;}
        else if(option=="--channels"&&i+1<argc){
            std::string_view n=argv[++i];
            if(n=="1"){channels=1;layout=SPEAKERS_MONO;}
            else if(n=="2"){channels=2;layout=SPEAKERS_STEREO;}
            else if(n=="6"){channels=6;layout=SPEAKERS_5POINT1;}
            else if(n=="8"){channels=8;layout=SPEAKERS_7POINT1;}
            else return 2;
        }
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
    obs_audio_info ai{};ai.samples_per_sec=rate;ai.speakers=layout;
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
