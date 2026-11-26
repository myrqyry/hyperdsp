#include "framework/dsp/graph_audition.hpp"
#include <cstdio>
using namespace hyperdsp;
int main(){
    const std::string gain=R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]}],"output":0})";
    bool ok=true;
    for(auto type:{AuditionSignal::Sine,AuditionSignal::Pink,AuditionSignal::Sweep}){
        auto a=audition_graph(gain,48000,2,type,1000,-12,{});
        ok=ok&&a.ok&&a.dry.size()==48000*2*3&&a.wet.size()==a.dry.size();
        double d=0,w=0;for(size_t i=0;i<a.dry.size();++i){d+=a.dry[i]*a.dry[i];w+=a.wet[i]*a.wet[i];}
        ok=ok&&d>0&&std::abs(10*std::log10(w/d)+6)<.001;
        auto dry=audition_pcm(a,false,-18),wet=audition_pcm(a,true,-18);
        ok=ok&&dry.size()==a.dry.size()*2&&wet.size()==dry.size();
        auto repeat=audition_graph(gain,48000,2,type,1000,-12,{});
        ok=ok&&repeat.dry==a.dry&&repeat.wet==a.wet;
    }
    auto loud=audition_graph(R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":24},"inputs":[{"source":-1,"weight":1}]}],"output":0})",48000,1,AuditionSignal::Sine,1000,0,{});
    auto pcm=audition_pcm(loud,true,0);
    int peak=0;for(size_t i=0;i+1<pcm.size();i+=2){int16_t v=int16_t(uint16_t(pcm[i])|(uint16_t(pcm[i+1])<<8));peak=std::max(peak,std::abs(int(v)));}
    ok=ok&&loud.ok&&peak>8000&&peak<=8232;
    std::vector<uint8_t> wav;
    auto four=[&](const char* s){for(int i=0;i<4;++i)wav.push_back(s[i]);};
    auto u16=[&](unsigned x){wav.push_back(x&255);wav.push_back((x>>8)&255);};
    auto u32=[&](unsigned x){u16(x&65535);u16(x>>16);};
    four("RIFF");u32(36+800*4);four("WAVE");four("fmt ");u32(16);u16(1);u16(2);u32(8000);u32(32000);u16(4);u16(16);four("data");u32(800*4);
    for(int i=0;i<800;++i){u16(8192);u16(57344);}
    auto file=audition_graph(gain,48000,1,AuditionSignal::File,1000,-12,wav);
    ok=ok&&file.ok&&file.rate==8000&&file.channels==2&&file.dry.size()==1600&&std::abs(file.dry[1]+.25)<1e-6;
    wav.pop_back();ok=ok&&!audition_graph(gain,48000,1,AuditionSignal::File,1000,-12,wav).ok;
    ok=ok&&!audition_graph("{}",48000,2,AuditionSignal::Sine,1000,-12,{}).ok;
    ok=ok&&!audition_graph(R"({"schema":1,"nodes":[{"effect":"ladspa:/no.so\tbad","inputs":[{"source":-1,"weight":1}]}],"output":0})",48000,2,AuditionSignal::Sine,1000,-12,{}).ok;
    std::printf("AUDITION_CORE_PASS=%d ceiling_peak=%d file=%u/%u\n",ok,peak,file.rate,file.channels);
    return ok?0:1;
}
