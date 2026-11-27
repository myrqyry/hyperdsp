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

namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned frames=480,blocks=2000,rate=48000;
using AllocBegin=void(*)();
using AllocEnd=void(*)(uint64_t*,uint64_t*);
AllocBegin alloc_begin=nullptr;
AllocEnd alloc_end=nullptr;
struct Probe {
    const obs_source_info* original=nullptr;
    std::vector<std::array<float,2>> reference;
    std::vector<double> wall;
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
    double cpu_start=cpu_us();
    if(alloc_begin)alloc_begin();
    auto start=Clock::now();
    auto* out=p.original->filter_audio(ptr,data);
    auto stop=Clock::now();
    if(alloc_end){uint64_t a=0,r=0;alloc_end(&a,&r);p.allocations+=a;p.releases+=r;}
    double cpu_end=cpu_us();
    p.wall.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
    p.cpu_us+=cpu_end-cpu_start;
    if(!out||out->frames!=frames||!out->data[0]||!out->data[1]){++p.errors;return out;}
    auto* l=reinterpret_cast<float*>(out->data[0]);auto* r=reinterpret_cast<float*>(out->data[1]);
    for(unsigned j=0;j<out->frames;++j,++p.samples){
        if(!std::isfinite(l[j])||!std::isfinite(r[j])){++p.errors;continue;}
        if(!p.compare){p.reference.push_back({l[j],r[j]});p.energy+=double(l[j])*l[j];p.difference+=std::abs(l[j]-latency_contention::sample(p.samples+100*frames));}
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
    {"graph_filter","graph_chain","hyperdsp_bench_graph_chain",[](obs_data_t* s){obs_data_set_string(s,"graph_json",R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamics_filter:1","params":{"threshold":-20,"ratio":4},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_spectral_filter:1","params":{"threshold":-35},"inputs":[{"source":0,"weight":1}]},{"effect":"native:hyperdsp_space_filter:2","params":{"decay":1.2},"inputs":[{"source":1,"weight":1}]}],"output":2})");}}
};
bool run(void* module,obs_source_t* source,const Case& c,bool strict){
    auto* original=static_cast<const obs_source_info*>(dlsym(module,c.symbol));
    if(!original||!original->filter_audio){std::fprintf(stderr,"missing %s\n",c.symbol);return false;}
    Probe p;p.original=original;p.reference.reserve(blocks*frames);p.wall.reserve(blocks);
    probe=&p;
    obs_source_info info=*original;info.id=c.id;info.filter_audio=timed;obs_register_source(&info);
    auto* settings=obs_data_create();c.settings(settings);
    bool ok=true;
    for(bool concurrent:{false,true}){
        auto* filter=obs_source_create_private(c.id,c.label,settings);
        if(!filter){ok=false;break;}
        obs_source_filter_add(source,filter);
        p.compare=concurrent;p.samples=p.errors=p.allocations=p.releases=0;p.cpu_us=0;p.wall.clear();
        std::atomic<bool> ready{false},stop{false};unsigned updates=0;std::thread writer;
        auto stamp=os_gettime_ns();
        auto stream=[&](unsigned count,unsigned offset){
            std::array<float,frames> l{},r{};
            for(unsigned k=0;k<count;++k){
                for(unsigned j=0;j<frames;++j){l[j]=latency_contention::sample(uint64_t(k+offset)*frames+j);r[j]=-.4f*l[j];}
                obs_source_audio audio{};audio.data[0]=reinterpret_cast<uint8_t*>(l.data());audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
                audio.frames=frames;audio.speakers=SPEAKERS_STEREO;audio.format=AUDIO_FORMAT_FLOAT_PLANAR;audio.samples_per_sec=rate;
                audio.timestamp=stamp+uint64_t(k+offset)*10000000ULL;obs_source_output_audio(source,&audio);
            }
        };
        p.record=false;stream(100,0);
        if(concurrent){
            writer=std::thread([&]{
                while(!stop.load()){
                    obs_data_set_double(settings,"benchmark_unused",updates%2);
                    obs_source_update(filter,settings);++updates;ready=true;
                }
            });
            while(!ready.load())std::this_thread::yield();
        }
        p.record=true;stream(blocks,100);p.record=false;
        stop=true;if(writer.joinable())writer.join();
        obs_source_filter_remove(source,filter);obs_source_release(filter);
        if(p.wall.empty()){ok=false;break;}
        double sum=0;unsigned missed=0;for(double v:p.wall){sum+=v;if(v>10000)++missed;}
        std::sort(p.wall.begin(),p.wall.end());
        auto pct=[&](double q){return p.wall[std::min(p.wall.size()-1,size_t(q*(p.wall.size()-1)))];};
        const bool valid=p.wall.size()==blocks&&p.samples==uint64_t(blocks)*frames&&p.energy>1&&p.difference>1&&(!concurrent||updates>10);
        std::printf("BENCH effect=%s phase=%s blocks=%zu updates=%u samples=%llu errors=%llu mean_us=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f max_us=%.2f cpu_budget_pct=%.3f overruns=%u allocation_calls=%llu free_calls=%llu audit=%d valid=%d\n",
            c.label,concurrent?"edits":"idle",p.wall.size(),updates,(unsigned long long)p.samples,(unsigned long long)p.errors,
            sum/p.wall.size(),pct(.5),pct(.95),pct(.99),p.wall.back(),p.cpu_us/(p.wall.size()*10000)*100,missed,(unsigned long long)p.allocations,(unsigned long long)p.releases,bool(alloc_begin),valid);
        ok=ok&&valid&&((!strict&&concurrent)||!p.errors)&&(!strict||!alloc_begin||(!p.allocations&&!p.releases));
    }
    obs_data_release(settings);probe=nullptr;return ok;
}
}
int main(int argc,char** argv){
    if(argc<3||argc>4){std::fprintf(stderr,"usage: %s module.so data-dir [--allow-mismatch]\n",argv[0]);return 2;}
    bool strict=argc==3;
    if(!strict&&std::string_view(argv[3])!="--allow-mismatch")return 2;
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
    bool ok=true;for(const auto& c:cases)ok=run(module,source,c,strict)&&ok;
    obs_source_release(source);dlclose(module);obs_shutdown();
    std::printf("BENCH_PASS=%d strict=%d\n",ok,strict);return ok?0:1;
}
