#include "framework/dsp/transient.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <numbers>
using namespace hyperdsp;
int main(){
    bool ok=true;
    for(unsigned rate:{8000u,44100u,48000u,192000u}){
        auto render=[&](TransientSettings settings){
            Transient engine;engine.reset(rate,settings);
            std::vector<float> l(rate),r(rate);
            for(unsigned i=0;i<rate;++i){
                float x=i<rate/10?0:i<rate/2?.2f:i<rate*8/10?.04f:0;
                l[i]=x;r[i]=-x*.5f;
            }
            float* p[]={l.data(),r.data()};engine.process(p,2,rate);
            for(unsigned i=0;i<rate;++i)ok=ok&&std::isfinite(l[i])&&std::abs(r[i]+l[i]*.5f)<1e-6;
            return l;
        };
        TransientSettings s;
        auto neutral=render(s);
        s.attack_db=12;auto punch=render(s);
        s.attack_db=-12;auto soften=render(s);
        s.attack_db=0;s.sustain_db=12;auto tail=render(s);
        s.sustain_db=-12;auto shorten=render(s);
        auto onset=rate/10,steady=rate*4/10,decay=rate*55/100;
        ok=ok&&neutral[onset]==.2f&&neutral[decay]==.04f&&punch[onset]>.7&&soften[onset]<.06;
        ok=ok&&std::abs(punch[steady]-.2)<.001&&std::abs(tail[onset]-.2)<1e-6;
        ok=ok&&tail[decay]>.08&&shorten[decay]<.02;
        s.wet=0;ok=ok&&render(s)==neutral;
        std::printf("TRANSIENT rate=%u onset=%g/%g tail=%g/%g steady=%g\n",rate,punch[onset],soften[onset],tail[decay],shorten[decay],punch[steady]);
    }
    {
        Transient engine;TransientSettings s;s.attack_db=12;s.sustain_db=12;
        engine.reset(48000,s);
        std::array<float,512> impulse{};impulse[0]=.2f;float* p[]={impulse.data()};
        engine.process(p,1,impulse.size());
        ok=ok&&impulse[0]>.7&&std::all_of(impulse.begin()+1,impulse.end(),[](float x){return x==0;});
        engine.reset(48000,s);
        std::vector<float> tone(48000);
        double in=0,out=0;
        for(unsigned i=0;i<tone.size();++i){tone[i]=.2f*std::sin(2*std::numbers::pi*1000*i/48000);if(i>=36000)in+=tone[i]*tone[i];}
        float* q[]={tone.data()};engine.process(q,1,tone.size());
        for(unsigned i=36000;i<tone.size();++i)out+=tone[i]*tone[i];
        double db=10*std::log10(out/in);ok=ok&&std::abs(db)<.35;
        std::printf("TRANSIENT_TONE steady_db=%g impulse=%g\n",db,impulse[0]);
    }
    Transient a,b;a.reset(48000,{});b.reset(48000,{});
    TransientSettings s;s.attack_db=8;s.sustain_db=-7;a.set(s);b.set(s);
    std::array<float,777> x{},y{};
    for(unsigned i=0;i<x.size();++i)x[i]=y[i]=.3f*std::sin(i*.13);
    float* p[]={x.data()};a.process(p,1,x.size());
    for(unsigned i=0;i<y.size();++i){float* q[]={&y[i]};b.process(q,1,1);}
    ok=ok&&x==y;
    GraphSpec spec{{{"native:hyperdsp_transient_filter:1",{},{{-1,1}}}},0};
    spec.controls.push_back({"attack","Attack",1,{{0,"attack_db",0,12}}});
    std::string error;auto graph=PreparedGraph::prepare(spec,48000,8,error);ok=ok&&graph&&graph->latency()==0;
    if(graph){float x=.2;float* p[]={&x,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};graph->process(p,8,1);ok=ok&&x>.7;}
    std::printf("TRANSIENT_PASS=%d\n",ok);return ok?0:1;
}
