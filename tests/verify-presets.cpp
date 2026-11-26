#include "framework/dsp/graph_monitor.hpp"
#include "framework/dsp/graph_json.hpp"
#include "framework/dsp/graph_audition.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <numbers>
using namespace hyperdsp;
static const char* name(void*){return "Preset verification";}
static void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void destroy(void*){}
int main(int argc,char** argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_source_info fixture{.id="preset_test_source",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&fixture);
    auto* source=obs_source_create("preset_test_source","Preset test",nullptr,nullptr);
    auto* filter=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);
    obs_source_filter_add(source,filter);
    std::set<std::string> expected={"parallel-color","vowel-space","voice-foundation","parallel-punch","radio-transmission","tape-echo","envelope-wah","rhythmic-pulse","spectral-dream","stereo-chorus"};
    bool ok=true;unsigned files=0;
    for(auto& entry:std::filesystem::directory_iterator(std::string(argv[2])+"/examples/graphs")){
        if(entry.path().extension()!=".json")continue;
        ++files;auto stem=entry.path().stem().string();expected.erase(stem);
        std::ifstream input(entry.path());std::string raw((std::istreambuf_iterator<char>(input)),{});
        GraphSpec spec;std::string error;bool pass=graph_json::parse(raw,spec,error);
        pass=pass&&!spec.controls.empty();
        auto audition=audition_graph(raw,48000,2,AuditionSignal::Pink,1000,-12,{});
        pass=pass&&audition.ok&&audition.peak<=2;
        for(unsigned rate:{44100u,48000u,96000u})for(unsigned channels:{1u,2u}){
            auto engine=PreparedGraph::prepare(spec,rate,channels,error);
            if(!engine){pass=false;continue;}
            std::array<std::array<float,256>,2> samples{};float* p[]={samples[0].data(),samples[1].data()};
            // Exercise default and all exposed controls at both endpoints.
            for(double value:{-1.,0.,1.}){
                if(value>=0){Parameters values;for(auto& c:spec.controls)values[c.id]=value;pass=engine->set_controls(values,error)&&pass;}
                double peak=0,energy=0;std::array<double,8> lo{},hi{};lo.fill(1);
                for(unsigned block=0;block<rate/256;++block){
                    for(unsigned j=0;j<256;++j)for(unsigned c=0;c<channels;++c){
                        auto n=block*256+j;double amp=(block%32<16?.4:.03)*(c?.7:1);
                        samples[c][j]=float(amp*(.6*std::sin(2*std::numbers::pi*180*n/rate)+.3*std::sin(2*std::numbers::pi*1100*n/rate)+.1*std::sin(2*std::numbers::pi*4700*n/rate)));
                    }
                    engine->process(p,channels,256);
                    for(unsigned c=0;c<channels;++c)for(float x:samples[c]){pass=pass&&std::isfinite(x);peak=std::max(peak,std::abs(double(x)));energy+=double(x)*x;}
                    for(unsigned i=0;i<spec.controls.size();++i){auto v=engine->effective_value(i);lo[i]=std::min(lo[i],v);hi[i]=std::max(hi[i],v);}
                }
                // All newly designed chains end in a full-wet sample-peak limiter.
                const double ceiling=(stem=="radio-transmission"||stem=="spectral-dream")?std::pow(10.,-3./20):std::pow(10.,-2./20);
                pass=pass&&peak<=(stem=="parallel-color"||stem=="vowel-space"?2.:ceiling+.0001)&&energy>1e-7;
                if(value<0&&!spec.routes.empty()){
                    bool moved=false;for(unsigned i=0;i<spec.controls.size();++i)moved=moved||hi[i]-lo[i]>.01;
                    pass=pass&&moved;
                }
            }
        }
        // Independent endpoint renders prove each macro is audible, not merely
        // accepted by the parameter schema. Fresh engines avoid tail/state bias.
        for(auto& control:spec.controls){
            auto render=[&](double value){
                auto engine=PreparedGraph::prepare(spec,48000,2,error,{},{{control.id,value}});
                std::vector<float> result;if(!engine)return result;
                std::array<std::array<float,256>,2> buf{};float* p[]={buf[0].data(),buf[1].data()};
                for(unsigned block=0;block<256;++block){
                    for(unsigned j=0;j<256;++j){
                        double n=block*256+j,amp=block<48?.5:.012;
                        float x=float(amp*(.45*std::sin(2*std::numbers::pi*180*n/48000)+.35*std::sin(2*std::numbers::pi*1100*n/48000)+.2*std::sin(2*std::numbers::pi*6500*n/48000)));
                        buf[0][j]=x;buf[1][j]=x;
                    }
                    engine->process(p,2,256);
                    result.insert(result.end(),buf[0].begin(),buf[0].end());
                    result.insert(result.end(),buf[1].begin(),buf[1].end());
                }
                return result;
            };
            auto low=render(0),high=render(1);
            double delta=0;
            if(low.size()==high.size()&&!low.empty()){
                for(size_t j=0;j<low.size();++j)delta+=std::pow(double(high[j])-low[j],2);
                delta=std::sqrt(delta/low.size());
            }
            if(delta<.0001)std::cerr<<stem<<" ineffective macro "<<control.id<<" delta="<<delta<<"\n";
            pass=pass&&delta>.0001;
        }
        // Production transaction and generated OBS controls, not just parser validation.
        calldata_t data;calldata_init(&data);calldata_set_string(&data,"graph",raw.c_str());calldata_set_bool(&data,"validate_only",false);
        bool applied=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_graph_apply_v1",&data)&&calldata_bool(&data,"ok");
        const char* status=nullptr;calldata_get_string(&data,"status",&status);
        if(!applied)std::cerr<<stem<<": "<<(status?status:"no status")<<"\n";
        calldata_free(&data);pass=pass&&applied;
        auto* props=obs_source_properties(filter);
        for(auto& c:spec.controls){auto key="graph_control_"+c.id;auto* prop=props?obs_properties_get(props,key.c_str()):nullptr;pass=pass&&prop&&obs_property_get_type(prop)==OBS_PROPERTY_FLOAT;}
        if(props)obs_properties_destroy(props);
        GraphMonitorFrame frame{};
        auto read=[&](){calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&frame);bool ready=proc_handler_call(obs_source_get_proc_handler(filter),"hyperdsp_graph_monitor_v1",&d)&&calldata_bool(&d,"available");calldata_free(&d);return ready;};
        read();
        std::array<float,480> l{},r{};auto start=os_gettime_ns();
        for(unsigned block=0;block<80;++block){
            for(unsigned j=0;j<480;++j){l[j]=.3f*std::sin(2*std::numbers::pi*440*(block*480+j)/48000);r[j]=.7f*l[j];}
            obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;a.timestamp=start+block*10000000ULL;
            obs_source_output_audio(source,&a);
        }
        bool received=read();double rms=0;
        for(float x:frame.output)rms+=double(x)*x;
        rms=std::sqrt(rms/frame.output.size());
        pass=pass&&received&&frame.control_count==spec.controls.size()&&std::isfinite(rms)&&rms>.00001&&rms<2;
        std::printf("PRESET %s controls=%zu routes=%zu audition_peak=%.4f obs_rms=%.4f pass=%d %s\n",stem.c_str(),spec.controls.size(),spec.routes.size(),audition.peak,rms,pass,error.c_str());
        ok=ok&&pass;
    }
    for(auto& missing:expected)std::cerr<<"Missing preset: "<<missing<<"\n";
    ok=ok&&expected.empty();
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_shutdown();
    std::printf("PRESETS_PASS=%d files=%u\n",ok,files);return ok?0:1;
}
