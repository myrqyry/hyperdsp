#pragma once
#include "graph_json.hpp"
#include "graph_monitor.hpp"
#include "fft.hpp"
#include <numbers>
namespace hyperdsp {
struct PreviewPoint {double hz,db;};
struct GraphPreview {
    bool ok=false,linear=false,tail_warning=false,external_routes=false;
    std::string error;
    unsigned rate=0,latency=0;
    double frequency=0,level_db=0,input_rms_db=0,output_rms_db=0,output_peak_db=0;
    GraphMonitorFrame frame{};
    std::vector<PreviewPoint> response;
};
inline double preview_db(double value){return 20*std::log10(std::max(value,1e-12));}
inline GraphPreview preview_graph(const std::string& raw,unsigned rate,unsigned channels,double hz,double level_db){
    GraphPreview result;
    try{
        if(rate<8000||rate>192000||channels<1||channels>8||!std::isfinite(hz)||hz<20||hz>=rate*.5||
           !std::isfinite(level_db)||level_db<-60||level_db>0){result.error="Invalid preview signal or format";return result;}
        GraphSpec spec;
        if(!graph_json::parse(raw,spec,result.error))return result;
        for(auto& n:spec.nodes)if(n.effect.starts_with("ladspa:")){
            result.error="LADSPA nodes are not supported by isolated native preview";return result;
        }
        // A deliberately conservative allowlist: only known stationary linear
        // filters and gain. Other families still receive a real tone render.
        result.linear=spec.routes.empty()&&std::all_of(spec.nodes.begin(),spec.nodes.end(),[](auto& n){
            return n.effect=="gain"||n.effect.starts_with("native:hyperdsp_native_filter:");
        });
        result.external_routes=std::any_of(spec.routes.begin(),spec.routes.end(),[](auto& r){return r.config.source>=6;});
        auto graph=PreparedGraph::prepare(spec,rate,channels,result.error);
        if(!graph)return result;
        result.rate=rate;result.frequency=hz;result.level_db=level_db;result.latency=graph->latency();
        constexpr unsigned block=256;
        std::array<std::array<float,block>,8> buffer{};
        std::array<float*,8> pointers{};
        for(unsigned c=0;c<channels;++c)pointers[c]=buffer[c].data();
        const unsigned length=rate/2,window=std::min(length,monitor_samples);
        double in_energy=0,out_energy=0,peak=0;
        const double amplitude=std::pow(10.,level_db/20);
        result.frame.rate=rate;
        for(unsigned offset=0;offset<length;offset+=block){
            unsigned count=std::min(block,length-offset);
            for(unsigned j=0;j<count;++j){
                float x=float(amplitude*std::sin(2*std::numbers::pi*hz*(offset+j)/rate));
                for(unsigned c=0;c<channels;++c)buffer[c][j]=x;
                if(offset+j>=length-window){
                    unsigned i=offset+j-(length-window);result.frame.input[i]=x;in_energy+=double(x)*x;
                }
            }
            graph->process(pointers.data(),channels,count);
            for(unsigned j=0;j<count;++j)if(offset+j>=length-window){
                unsigned i=offset+j-(length-window);float x=buffer[0][j];result.frame.output[i]=x;
                out_energy+=double(x)*x;peak=std::max(peak,std::abs(double(x)));
            }
        }
        result.input_rms_db=preview_db(std::sqrt(in_energy/window));result.output_rms_db=preview_db(std::sqrt(out_energy/window));
        result.output_peak_db=preview_db(peak);
        if(result.linear){
            // A fresh instance, not the tone-rendered one. Impulse FFT has
            // unit amplitude and no analysis window: this is a finite IR.
            graph.reset();graph=PreparedGraph::prepare(spec,rate,channels,result.error);
            if(!graph)return result;
            constexpr unsigned n=32768;
            std::array<std::complex<float>,n> impulse{};
            double energy=0,tail=0;
            for(unsigned offset=0;offset<n;offset+=block){
                for(unsigned c=0;c<channels;++c){buffer[c].fill(0);if(offset==0)buffer[c][0]=1;}
                graph->process(pointers.data(),channels,block);
                for(unsigned j=0;j<block;++j){
                    float x=buffer[0][j];impulse[offset+j]=x;energy+=double(x)*x;
                    if(offset+j>=n-1024)tail+=double(x)*x;
                }
            }
            result.tail_warning=energy>0&&tail/energy>1e-8;
            FFT<n> fft;fft.transform(impulse);
            const double high=std::min(20000.,rate*.49);
            for(unsigned i=0;i<=256;++i){
                double f=20*std::pow(high/20,i/256.);
                double k=f*n/rate;unsigned lo=unsigned(k);double t=k-lo;
                double mag=std::lerp(double(std::abs(impulse[lo])),double(std::abs(impulse[lo+1])),t);
                result.response.push_back({f,preview_db(mag)});
            }
        }
        result.ok=true;
    }catch(const std::exception& ex){result.error=ex.what();}
    return result;
}
}
