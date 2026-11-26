#include "framework/dsp/dynamic_eq.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <numbers>
using namespace hyperdsp;
static double measure(unsigned rate,double hz,double amplitude,DynamicEqSettings s){
    DynamicEq engine;engine.reset(rate,s);
    std::array<float,256> l{},r{};float* p[]={l.data(),r.data()};
    double power=0,reference=0;
    for(unsigned offset=0;offset+256<=rate;offset+=256){
        for(unsigned j=0;j<256;++j){l[j]=float(amplitude*std::sin(2*std::numbers::pi*hz*(offset+j)/rate));r[j]=-.5f*l[j];if(offset>rate/2)reference+=double(l[j])*l[j];}
        engine.process(p,2,256);
        for(unsigned j=0;j<256;++j){
            if(!std::isfinite(l[j])||std::abs(r[j]+.5f*l[j])>1e-6)return NAN;
            if(offset>rate/2)power+=double(l[j])*l[j];
        }
    }
    return 10*std::log10(power/reference);
}
int main(){
    bool ok=true;
    for(unsigned rate:{8000u,44100u,48000u,96000u,192000u}){
        DynamicEqSettings s;s.frequency=std::min(6500.,rate*.3);s.q=2;s.threshold=-30;s.range_db=-9;s.attack_ms=2;s.release_ms=80;
        double center=measure(rate,s.frequency,.5,s),quiet=measure(rate,s.frequency,.01,s),low=measure(rate,180,.5,s);
        ok=ok&&std::abs(center+9)<.15&&std::abs(quiet)<.001&&std::abs(low)<.1;
        s.range_db=6;double boost=measure(rate,s.frequency,.5,s);
        ok=ok&&std::abs(boost-6)<.15;
        s.wet=0;ok=ok&&std::abs(measure(rate,s.frequency,.5,s))<.00001;
        std::printf("DYNAMIC_EQ rate=%u center=%g quiet=%g low=%g boost=%g\n",rate,center,quiet,low,boost);
    }
    {
        // Mixed signal: measure both frequency components while the detector
        // is actively reducing the high-frequency component.
        DynamicEqSettings s;s.frequency=6500;s.q=2;s.threshold=-30;s.range_db=-9;
        DynamicEq engine;engine.reset(48000,s);
        std::vector<float> audio(48000);
        for(unsigned i=0;i<audio.size();++i)
            audio[i]=.2f*std::sin(2*std::numbers::pi*180*i/48000)+.5f*std::sin(2*std::numbers::pi*6500*i/48000);
        float* p[]={audio.data()};engine.process(p,1,audio.size());
        auto component=[&](double hz){
            double sine=0,cosine=0;unsigned n=9600;
            for(unsigned i=audio.size()-n;i<audio.size();++i){
                double phase=2*std::numbers::pi*hz*i/48000;
                sine+=audio[i]*std::sin(phase);cosine+=audio[i]*std::cos(phase);
            }
            return 2*std::hypot(sine,cosine)/n;
        };
        double low=20*std::log10(component(180)/.2),high=20*std::log10(component(6500)/.5);
        ok=ok&&std::abs(low)<.1&&std::abs(high+9)<.15;
        float active=engine.dynamic_gain_db();
        std::array<float,48000> silence{};float* quiet[]={silence.data()};
        engine.process(quiet,1,480);
        float early=engine.dynamic_gain_db();
        engine.process(quiet,1,47520);
        float settled=engine.dynamic_gain_db();
        ok=ok&&active<-8.9&&early<-7&&std::abs(settled)<.001;
        std::printf("DYNAMIC_EQ_MIX low_db=%g high_db=%g recovery=%g/%g/%g\n",low,high,active,early,settled);
    }
    DynamicEq a,b;DynamicEqSettings s;s.frequency=6500;s.range_db=-12;
    a.reset(48000,{});b.reset(48000,{});a.set(s);b.set(s);
    std::array<float,777> l{},r{};
    for(unsigned j=0;j<l.size();++j)l[j]=r[j]=.5f*std::sin(j*.85);
    float* p[]={l.data()};a.process(p,1,l.size());
    for(unsigned j=0;j<r.size();++j){float* q[]={&r[j]};b.process(q,1,1);}
    ok=ok&&l==r;
    GraphSpec spec{{{"native:hyperdsp_dynamic_eq_filter:1",{{"frequency",6500}},{{-1,1}}}},0};
    spec.controls.push_back({"depth","Depth",1,{{0,"range_db",0,-12}}});
    std::string error;auto graph=PreparedGraph::prepare(spec,48000,8,error);ok=ok&&graph&&graph->latency()==0;
    std::printf("DYNAMIC_EQ_PASS=%d\n",ok);return ok?0:1;
}
