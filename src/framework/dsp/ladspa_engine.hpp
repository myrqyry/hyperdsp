#pragma once
#include "ladspa_controls.hpp"
#include "modulation_matrix.hpp"
#include "../../platform/native_library.hpp"
#include <memory>
#include <vector>
namespace hyperdsp {
inline constexpr unsigned max_ports=256,block_size=1024,route_count=modulation_slots;
struct LadspaEngine {
    void* library=nullptr;
    const LADSPA_Descriptor* descriptor=nullptr;
    struct Voice {
        LADSPA_Handle handle=nullptr;
        std::array<float,max_ports> values{};
        std::array<std::array<float,block_size>,2> input{},scratch{};
        bool activated=false;
    };
    std::vector<std::unique_ptr<Voice>> voices;
    std::vector<unsigned> in,out;
    std::vector<Control> controls;
    unsigned rate=0, channels=0;
    hyperdsp::MixModulator modulation;
    std::array<hyperdsp::MixSettings,block_size> mix_frames{};
    bool initialized=false;
    struct Route {
        int source=0,port=-1;
        bool mix=false,output=false;
        float min=0,max=0;
    };
    std::array<Route,route_count> routes;
    hyperdsp::ModulationMatrix matrix;
    bool any_route=false,any_port=false;
    std::array<float,max_ports> base_values{};
    std::array<float,8> detector_input{};
    std::string path,label;
    ~LadspaEngine() {
        if(descriptor)for(auto& v:voices)if(v->handle){
            if(v->activated&&descriptor->deactivate)descriptor->deactivate(v->handle);
            descriptor->cleanup(v->handle);
        }
        platform::close_library(library);
    }
    LadspaEngine()=default;
    LadspaEngine(const LadspaEngine&)=delete;
    LadspaEngine& operator=(const LadspaEngine&)=delete;
    void set_control(const Control& c,float value){
        base_values[c.port]=value;
        for(auto& v:voices)v->values[c.port]=value;
    }
    template<class Setup> bool open(const std::string& file,const std::string& effect,unsigned sr,unsigned ch,Setup setup) {
        if(library)return false;
        path=file;label=effect;
        rate=sr;channels=ch;
        if(!platform::absolute_library_path(path)||label.empty()||ch==0||ch>8)return false;
        library=platform::open_library(path);
        if(!library)return false;
        auto entry=platform::library_symbol<LADSPA_Descriptor_Function>(library,"ladspa_descriptor");
        if(!entry)return false;
        for(unsigned i=0;i<1024;++i){
            auto* d=entry(i);if(!d)break;
            if(d->Label&&label==d->Label){descriptor=d;break;}
        }
        auto* d=descriptor;
        if(!d||!LADSPA_IS_HARD_RT_CAPABLE(d->Properties)||!d->instantiate||!d->connect_port||
           !d->run||!d->cleanup||d->PortCount>max_ports||!d->PortDescriptors||!d->PortNames||!d->PortRangeHints)return false;
        for(unsigned p=0;p<d->PortCount;++p){
            auto type=d->PortDescriptors[p];
            if(LADSPA_IS_PORT_AUDIO(type)){
                if(LADSPA_IS_PORT_INPUT(type))in.push_back(p);
                else if(LADSPA_IS_PORT_OUTPUT(type))out.push_back(p);
                else return false;
            }else if(LADSPA_IS_PORT_CONTROL(type)){
                if(LADSPA_IS_PORT_INPUT(type))controls.push_back(control(d,p,sr));
            }else return false;
        }
        // One mono instance per OBS channel; stereo only for stereo layouts.
        if(in.size()!=out.size()||(in.size()!=1&&(in.size()!=2||ch!=2)))return false;
        unsigned count=in.size()==1?ch:1;
        for(unsigned i=0;i<count;++i){
            auto v=std::make_unique<Voice>();
            v->handle=d->instantiate(d,sr);
            if(!v->handle)return false;
            for(unsigned p=0;p<d->PortCount;++p)
                if(LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p]))d->connect_port(v->handle,p,&v->values[p]);
            // Provide valid audio buffers even during activate().
            for(unsigned c=0;c<in.size();++c){
                d->connect_port(v->handle,in[c],v->input[c].data());
                d->connect_port(v->handle,out[c],v->scratch[c].data());
            }
            voices.push_back(std::move(v));
        }
        if(!setup(*this))return false;
        for(auto& v:voices){if(d->activate)d->activate(v->handle);v->activated=true;}
        return true;
    }
    void process(float*const* audio,unsigned ch,unsigned frames) {
        if(!audio||ch!=channels)return;
        for(unsigned c=0;c<channels;++c)if(!audio[c])return;
        for(unsigned off=0;off<frames;){
            unsigned n=std::min(any_port?64u:block_size,frames-off);
            std::array<float,route_count> port_sums{};
            // All routes see the same pre-trim input and advance once per sample.
            // Sum contributions to each destination before applying its bounds.
            for(unsigned j=0;j<n;++j){
                auto mix=modulation.next();
                if(any_route){
                    for(unsigned c=0;c<channels;++c)detector_input[c]=audio[c][off+j];
                    const auto frame=matrix.next(detector_input.data(),channels);
                    for(unsigned slot=0;slot<route_count;++slot)
                        if(frame.entries[slot].destination>=2)port_sums[slot]+=frame.entries[slot].value;
                    mix.wet=std::clamp(mix.wet+frame.sum(0),0.f,1.f);
                    mix.output_gain*=std::max(0.f,1.f+frame.sum(1));
                }
                mix_frames[j]=mix;
            }
            if(any_port)for(unsigned slot=0;slot<route_count;++slot){
                const auto& r=routes[slot];
                if(!r.source||r.port<0)continue;
                bool seen=false;
                for(unsigned k=0;k<slot;++k)if(routes[k].source&&routes[k].port==r.port)seen=true;
                if(seen)continue;
                float sum=port_sums[slot];
                for(unsigned k=slot+1;k<route_count;++k)
                    if(routes[k].source&&routes[k].port==r.port)sum+=port_sums[k];
                const float value=std::clamp(base_values[r.port]+sum/float(n),r.min,r.max);
                for(auto& v:voices)v->values[r.port]=value;
            }
            for(unsigned i=0;i<voices.size();++i){
                auto& v=*voices[i];
                for(unsigned c=0;c<in.size();++c){
                    auto* samples=audio[i*in.size()+c]+off;
                    for(unsigned j=0;j<n;++j)v.input[c][j]=mix_frames[j].input(samples[j]);
                }
                descriptor->run(v.handle,n);
                for(unsigned c=0;c<out.size();++c){
                    auto* samples=audio[i*in.size()+c]+off;
                    for(unsigned j=0;j<n;++j)samples[j]=mix_frames[j].output(samples[j],mix_frames[j].input(samples[j]),v.scratch[c][j]);
                }
            }
            off+=n;
        }
    }
};
}
