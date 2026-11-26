#pragma once
#include "graph_json.hpp"
#include <cmath>
#include <numbers>
#include <cstdint>
#include <cstring>
namespace hyperdsp {
enum class AuditionSignal { Sine, Pink, Sweep, File };
struct GraphAudition {
    bool ok=false,external_routes=false,truncated=false;
    unsigned rate=0,channels=0,latency=0;
    std::string error;
    std::vector<float> dry,wet;
    double peak=0;
};
inline constexpr size_t audition_file_limit=32*1024*1024;
inline bool decode_audition_wav(const std::vector<uint8_t>& bytes,GraphAudition& a){
    auto fail=[&](const char* s){a.error=s;return false;};
    auto u16=[&](size_t i){return unsigned(bytes[i])|(unsigned(bytes[i+1])<<8);};
    auto u32=[&](size_t i){return uint32_t(u16(i))|(uint32_t(u16(i+2))<<16);};
    auto tag=[&](size_t i,const char* s){return i<=bytes.size()&&bytes.size()-i>=4&&!std::memcmp(bytes.data()+i,s,4);};
    if(bytes.size()<12||bytes.size()>audition_file_limit||!tag(0,"RIFF")||!tag(8,"WAVE"))return fail("Expected local PCM16 WAV (up to 32 MiB)");
    uint64_t end=uint64_t(u32(4))+8;
    if(end>bytes.size()||end<12)return fail("Truncated WAV");
    size_t fmt=0,data=0,fmt_size=0,data_size=0;
    for(size_t at=12;at+8<=end;){
        size_t len=u32(at+4),start=at+8;
        if(len>end-start||len+(len&1)>end-start)return fail("Truncated WAV chunk");
        if(tag(at,"fmt ")&&!fmt){fmt=start;fmt_size=len;}
        if(tag(at,"data")&&!data){data=start;data_size=len;}
        at=start+len+(len&1);
    }
    if(!fmt||fmt_size<16||!data||u16(fmt)!=1||u16(fmt+14)!=16)return fail("Only uncompressed PCM16 WAV is supported");
    a.channels=u16(fmt+2);a.rate=u32(fmt+4);
    if(a.channels<1||a.channels>2||a.rate<8000||a.rate>192000||u16(fmt+12)!=a.channels*2||
       u32(fmt+8)!=a.rate*a.channels*2||data_size%(a.channels*2))return fail("WAV must be mono/stereo, 8–192 kHz, complete PCM16 frames");
    size_t frames=data_size/(a.channels*2);
    if(!frames)return fail("Empty WAV");
    size_t count=std::min<size_t>(frames,size_t(a.rate)*8);
    a.truncated=count<frames;a.dry.resize(count*a.channels);
    for(size_t i=0;i<a.dry.size();++i){
        unsigned raw=u16(data+2*i);int signed_value=raw>=32768?int(raw)-65536:int(raw);
        a.dry[i]=float(signed_value)/32768.f;
    }
    return true;
}
inline GraphAudition audition_graph(const std::string& raw,unsigned rate,unsigned channels,AuditionSignal signal,double hz,double level_db,const std::vector<uint8_t>& wav){
    GraphAudition a;
    try{
        GraphSpec spec;
        if(!graph_json::parse(raw,spec,a.error))return a;
        for(auto& node:spec.nodes)if(node.effect.starts_with("ladspa:")){a.error="LADSPA nodes are excluded from native audition";return a;}
        a.external_routes=std::any_of(spec.routes.begin(),spec.routes.end(),[](auto& route){return route.config.source>=6;});
        if(signal==AuditionSignal::File){
            if(!decode_audition_wav(wav,a))return a;
        }else{
            if(rate<8000||rate>192000||channels<1||channels>2||!std::isfinite(hz)||hz<20||hz>=rate*.5||
               !std::isfinite(level_db)||level_db<-60||level_db>0){a.error="Audition needs mono/stereo, valid frequency and level";return a;}
            if(signal!=AuditionSignal::Sine&&signal!=AuditionSignal::Pink&&signal!=AuditionSignal::Sweep){a.error="Unknown test signal";return a;}
            a.rate=rate;a.channels=channels;const unsigned frames=rate*3;
            a.dry.resize(size_t(frames)*channels);
            std::array<double,7> pink{};uint32_t rng=0x12345678;
            double phase=0,peak=0,high=std::min(20000.,rate*.45);
            for(unsigned j=0;j<frames;++j){
                double value=0;
                if(signal==AuditionSignal::Pink){
                    rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
                    double white=double(rng)/2147483648.-1;
                    pink[0]=.99886*pink[0]+white*.0555179;pink[1]=.99332*pink[1]+white*.0750759;
                    pink[2]=.96900*pink[2]+white*.153852;pink[3]=.86650*pink[3]+white*.3104856;
                    pink[4]=.55000*pink[4]+white*.5329522;pink[5]=-.7616*pink[5]-white*.016898;
                    value=pink[0]+pink[1]+pink[2]+pink[3]+pink[4]+pink[5]+pink[6]+white*.5362;pink[6]=white*.115926;
                }else{
                    value=std::sin(phase);
                    double freq=signal==AuditionSignal::Sweep?20*std::pow(high/20,double(j)/(frames-1)):hz;
                    phase=std::remainder(phase+2*std::numbers::pi*freq/rate,2*std::numbers::pi);
                }
                peak=std::max(peak,std::abs(value));
                for(unsigned c=0;c<channels;++c)a.dry[size_t(j)*channels+c]=float(value);
            }
            double scale=std::pow(10.,level_db/20)/std::max(peak,1e-12);
            for(auto& x:a.dry)x=float(x*scale);
        }
        auto graph=PreparedGraph::prepare(spec,a.rate,a.channels,a.error);
        if(!graph)return a;
        a.latency=graph->latency();
        unsigned frames=unsigned(a.dry.size()/a.channels);
        a.wet.resize(a.dry.size());
        constexpr unsigned block=256;
        std::array<std::array<float,block>,2> buffers{};
        std::array<float*,2> pointers{buffers[0].data(),buffers[1].data()};
        // Remove known graph latency from the comparison; pad input with silence.
        for(unsigned offset=0;offset<frames+a.latency;offset+=block){
            unsigned count=std::min(block,frames+a.latency-offset);
            for(unsigned j=0;j<count;++j)for(unsigned c=0;c<a.channels;++c)
                buffers[c][j]=offset+j<frames?a.dry[size_t(offset+j)*a.channels+c]:0;
            graph->process(pointers.data(),a.channels,count);
            for(unsigned j=0;j<count;++j)if(offset+j>=a.latency)for(unsigned c=0;c<a.channels;++c){
                auto v=buffers[c][j];
                if(!std::isfinite(v)){a.error="Nonfinite audition output";return a;}
                a.wet[size_t(offset+j-a.latency)*a.channels+c]=v;
            }
        }
        for(auto v:a.dry)a.peak=std::max(a.peak,std::abs(double(v)));
        for(auto v:a.wet)a.peak=std::max(a.peak,std::abs(double(v)));
        a.ok=true;
    }catch(const std::exception& e){a.error=e.what();}
    return a;
}
// Shared attenuation preserves the relative level of A and B. No boost or
// independent normalization. Peak ceiling is a digital safety margin only.
inline std::vector<uint8_t> audition_pcm(const GraphAudition& a,bool wet,double listening_db){
    if(!a.ok||!std::isfinite(listening_db)||!a.channels)return {};
    auto& src=wet?a.wet:a.dry;size_t frames=src.size()/a.channels;
    double scale=std::min(std::pow(10.,std::clamp(listening_db,-60.,0.)/20),
                          std::pow(10.,-12./20)/std::max(a.peak,1e-12));
    size_t fade=std::min<size_t>(frames/2,std::max(1u,a.rate/100));
    std::vector<uint8_t> bytes(src.size()*2);
    for(size_t i=0;i<src.size();++i){
        size_t j=i/a.channels;
        double envelope=fade?std::min({1.,double(j)/fade,double(frames-1-j)/fade}):0;
        auto value=int16_t(std::lround(std::clamp(double(src[i])*scale*envelope,-1.,1.)*32767));
        auto bits=uint16_t(value);bytes[2*i]=bits&255;bytes[2*i+1]=bits>>8;
    }
    return bytes;
}
}
