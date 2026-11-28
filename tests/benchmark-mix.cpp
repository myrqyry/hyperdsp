// Isolated OBS mixer qualification; no device, encoder, or running desktop OBS.
#include <obs-module.h>
#include <util/platform.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include "framework/dsp/graph_monitor.hpp"
#include <numbers>
#include <thread>
#include <vector>

namespace {
constexpr unsigned rate=48000,frames=480,output_frames=1024,count=4,packets=500,tracks=3;
constexpr float amplitude=.08f;
constexpr double gain=.5011872336272722;
constexpr double tau=2*std::numbers::pi;
using Clock=std::chrono::steady_clock;
constexpr unsigned bins[count]={8,16,24,32};
constexpr const char* graph=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_spectral_filter:1","params":{"range":0},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":0,"weight":1}]}],"output":1,"controls":[{"id":"threshold","name":"Threshold","default":0,"bindings":[{"node":0,"parameter":"threshold","low":-60,"high":-20}]}]})";
struct Packet {
    uint64_t stamp=0;
    std::array<float,output_frames> l{},r{};
};
struct Capture {
    std::array<std::vector<Packet>,tracks> data;
    unsigned invalid=0;
    Capture(){for(auto& v:data)v.reserve(512);}
};
void capture(void* ptr,size_t track,audio_data* data){
    auto& c=*static_cast<Capture*>(ptr);
    if(track>=tracks||!data||data->frames!=output_frames||!data->data[0]||!data->data[1]){++c.invalid;return;}
    auto& v=c.data[track];
    if(v.size()==v.capacity()){++c.invalid;return;}
    v.emplace_back();auto& p=v.back();p.stamp=data->timestamp;
    std::copy_n(reinterpret_cast<const float*>(data->data[0]),output_frames,p.l.begin());
    std::copy_n(reinterpret_cast<const float*>(data->data[1]),output_frames,p.r.begin());
}
const char* name(void*){return "HyperDSP mix source";}
void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
void destroy(void*){}
struct Source {
    obs_source_t* source=nullptr,*filter=nullptr;
    obs_data_t* settings=nullptr;
    unsigned updates=0,seen_controls=0,channel=0;
    ~Source(){if(channel)obs_set_output_source(channel,nullptr);if(filter){if(source)obs_source_filter_remove(source,filter);obs_source_release(filter);}if(settings)obs_data_release(settings);if(source)obs_source_release(source);}
};
bool run(bool negative){
    obs_source_info info{.id="hyperdsp_mix_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&info);
    std::array<Source,count> sources;
    for(unsigned n=0;n<count;++n){
        auto& s=sources[n];
        s.source=obs_source_create(info.id,("Mix "+std::to_string(n)).c_str(),nullptr,nullptr);
        s.settings=obs_data_create();obs_data_set_string(s.settings,"graph_json",graph);
        s.filter=obs_source_create_private("hyperdsp_graph_filter","Spectral and gain",s.settings);
        if(!s.source||!s.filter)return false;
        obs_source_filter_add(s.source,s.filter);
        obs_source_set_audio_mixers(s.source,1u|(1u<<(1+(negative&&n==0?1:n%2))));
        s.channel=n+1;obs_set_output_source(s.channel,s.source);
        obs_source_set_audio_active(s.source,true);
    }
    Capture c;
    auto* audio=obs_get_audio();
    unsigned connected=0;
    for(;connected<tracks;++connected)if(!audio_output_connect(audio,connected,nullptr,capture,&c))break;
    if(connected!=tracks){
        for(unsigned i=0;i<connected;++i)audio_output_disconnect(audio,i,capture,&c);
        return false; // Source destructors detach already-routed sources.
    }
    // A bounded 60 ms lead represents capture buffering, not a real device.
    auto start=Clock::now()+std::chrono::milliseconds(150);
    auto stamp=os_gettime_ns()+150000000ULL;
    std::atomic<bool> stop=false;
    std::thread writer([&]{
        unsigned i=0;
        while(!stop.load()){
            for(auto& s:sources){
                hyperdsp::GraphMonitorFrameV3 monitor{};
                calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&monitor);
                if(proc_handler_call(obs_source_get_proc_handler(s.filter),"hyperdsp_graph_monitor_v3",&d)&&calldata_bool(&d,"available")){
                    const auto& v=monitor.audio.signal;
                    if(v.control_count==1&&std::string_view(v.controls[0].id.data())=="threshold"){
                        if(v.controls[0].base==0)s.seen_controls|=1;
                        if(v.controls[0].base==1)s.seen_controls|=2;
                    }
                }
                calldata_free(&d);
                obs_data_set_double(s.settings,"graph_control_threshold",i%2);obs_source_update(s.filter,s.settings);++s.updates;
            }
            ++i;std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    std::array<std::thread,count> producers;
    for(unsigned n=0;n<count;++n)producers[n]=std::thread([&,n]{
        std::array<float,frames> l{},r{};
        for(unsigned k=0;k<packets;++k){
            auto offset=uint64_t(k)*frames*1000000000ULL/rate;
            for(unsigned j=0;j<frames;++j){
                const auto i=k*frames+j;
                l[j]=amplitude*float(std::sin(tau*bins[n]*i/output_frames));r[j]=-.4f*l[j];
            }
            obs_source_audio a{};a.data[0]=reinterpret_cast<uint8_t*>(l.data());a.data[1]=reinterpret_cast<uint8_t*>(r.data());
            a.frames=frames;a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=rate;a.timestamp=stamp+offset;
            std::this_thread::sleep_until(start+std::chrono::nanoseconds(offset)-std::chrono::milliseconds(60));
            obs_source_output_audio(sources[n].source,&a);
        }
    });
    for(auto& p:producers)p.join();
    stop=true;writer.join();
    std::this_thread::sleep_until(start+std::chrono::seconds(5)+std::chrono::milliseconds(400));
    for(unsigned t=0;t<tracks;++t)audio_output_disconnect(audio,t,capture,&c);
    for(auto& s:sources){obs_set_output_source(s.channel,nullptr);s.channel=0;}
    // Ignore the first/last 500 ms, where priming/draining is expected.
    unsigned checked=0,errors=0,missing=0;
    double max_amp_error=0,max_sum_error=0,max_stereo_error=0,max_wave_error=0,max_phase_error=0;
    std::array<std::array<double,count>,tracks> initial_re{},initial_im{};
    uint64_t prev=0;
    for(const auto& p:c.data[0]){
        if(p.stamp<stamp+500000000ULL||p.stamp>=stamp+4500000000ULL)continue;
        if(prev){auto d=p.stamp-prev;auto ideal=uint64_t(output_frames)*1000000000ULL/rate;if(d>ideal+2||d+2<ideal)++missing;}
        prev=p.stamp;++checked;
        std::array<const Packet*,tracks> block{&p,nullptr,nullptr};
        for(unsigned t=1;t<tracks;++t){
            auto it=std::find_if(c.data[t].begin(),c.data[t].end(),[&](const Packet& x){return x.stamp==p.stamp;});
            if(it!=c.data[t].end())block[t]=&*it;
        }
        if(!block[1]||!block[2]){++missing;continue;}
        for(unsigned j=0;j<output_frames;++j){
            const auto error=std::abs(double(p.l[j])-block[1]->l[j]-block[2]->l[j]);
            max_sum_error=std::max(max_sum_error,error);
            if(error>2e-6||!std::isfinite(error))++errors;
            for(auto* b:block){
                auto e=std::abs(double(b->r[j])+.4*b->l[j]);
                max_stereo_error=std::max(max_stereo_error,e);
                if(e>2e-6||!std::isfinite(e))++errors;
            }
        }
        for(unsigned t=0;t<tracks;++t){
            std::array<double,count> re{},im{};
            for(unsigned n=0;n<count;++n){
                for(unsigned j=0;j<output_frames;++j){
                    const double a=tau*bins[n]*j/output_frames;
                    re[n]+=block[t]->l[j]*std::cos(a);im[n]+=block[t]->l[j]*std::sin(a);
                }
                re[n]*=2./output_frames;im[n]*=2./output_frames;
                const double expected=t==0||t==1+n%2?amplitude*gain:0;
                auto e=std::abs(std::hypot(re[n],im[n])-expected);
                max_amp_error=std::max(max_amp_error,e);if(e>2e-4||!std::isfinite(e))++errors;
                // Integer-bin tones have the same phase in every contiguous 1024-frame block.
                if(checked==1){initial_re[t][n]=re[n];initial_im[t][n]=im[n];}
                else{
                    auto phase_error=std::hypot(re[n]-initial_re[t][n],im[n]-initial_im[t][n]);
                    max_phase_error=std::max(max_phase_error,phase_error);
                    if(phase_error>2e-4||!std::isfinite(phase_error))++errors;
                }
            }
            // Residual detects dropouts/spurious energy beyond the four tones.
            for(unsigned j=0;j<output_frames;++j){
                double expected=0;for(unsigned n=0;n<count;++n){double a=tau*bins[n]*j/output_frames;expected+=re[n]*std::cos(a)+im[n]*std::sin(a);}
                double e=std::abs(block[t]->l[j]-expected);
                max_wave_error=std::max(max_wave_error,e);if(e>3e-4||!std::isfinite(e))++errors;
            }
        }
    }
    bool ok=checked>=185&&checked<=190&&!errors&&!missing&&!c.invalid;
    unsigned controls=0;for(auto& s:sources){ok=ok&&s.updates>=40&&s.seen_controls==3;if(s.seen_controls==3)++controls;}
    std::printf("MIX_OUTPUT checked=%u samples_per_track=%u errors=%u missing=%u invalid=%u amp_error=%g sum_error=%g stereo_error=%g residual=%g phase_error=%g updates=%u controls_seen=%u pass=%d\n",checked,checked*output_frames,errors,missing,c.invalid,max_amp_error,max_sum_error,max_stereo_error,max_wave_error,max_phase_error,sources[0].updates,controls,ok);
    return ok;
}
}
int main(int argc,char** argv){
    if((argc!=4&&argc!=5)||(argc==5&&std::string_view(argv[4])!="--negative-routing")){
        std::fprintf(stderr,"usage: %s module.so data-dir libobs-opengl.so [--negative-routing]\n",argv[0]);return 2;
    }
    if(!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_video_info vi{};vi.graphics_module=argv[3];vi.fps_num=30;vi.fps_den=1;vi.base_width=vi.output_width=320;vi.base_height=vi.output_height=180;vi.output_format=VIDEO_FORMAT_RGBA;vi.colorspace=VIDEO_CS_SRGB;vi.range=VIDEO_RANGE_FULL;vi.scale_type=OBS_SCALE_BILINEAR;
    auto video=obs_reset_video(&vi);if(video!=OBS_VIDEO_SUCCESS){std::printf("MIX_VIDEO_INIT=%d\n",video);obs_shutdown();return 3;}
    obs_audio_info ai{};ai.samples_per_sec=rate;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai)){obs_shutdown();return 4;}
    obs_module_t* m=nullptr;
    if(obs_open_module(&m,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(m)){obs_shutdown();return 5;}
    bool negative=argc==5&&std::string_view(argv[4])=="--negative-routing";
    bool ok=run(negative);obs_shutdown();return ok?0:1;
}
