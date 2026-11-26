#include "framework/dsp/band_transient.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <numbers>
using namespace hyperdsp;
static std::vector<float> render(unsigned rate,double hz,BandTransientSettings s){
    BandTransient e;e.reset(rate,s);
    std::vector<float> v(rate);
    for(unsigned i=0;i<rate;++i){
        double amp=i<rate/10?0:i<rate/2?.2:i<rate*8/10?.04:0;
        v[i]=float(amp*std::sin(2*std::numbers::pi*hz*i/rate));
    }
    float* p[]={v.data()};e.process(p,1,v.size());return v;
}
static double rms(const std::vector<float>& v,unsigned a,unsigned b){
    double sum=0;for(unsigned i=a;i<b;++i)sum+=double(v[i])*v[i];return std::sqrt(sum/(b-a));
}
int main(){
    bool ok=true;
    for(unsigned rate:{8000u,44100u,48000u,96000u,192000u}){
        for(double hz:{80.,250.,1000.,2500.,3500.}){
            BandTransientSettings s;
            auto neutral=render(rate,hz,s);
            s.wet=0;auto dry=render(rate,hz,s);
            ok=ok&&neutral==dry;
            double amplitude=rms(neutral,rate*3/10,rate*45/100);
            ok=ok&&std::abs(20*std::log10(amplitude/(.2/std::sqrt(2.))))<.15;
        }
    }
    BandTransientSettings s;s.attack_db[0]=12;s.sustain_db[0]=-12;
    auto low=render(48000,80,s),high=render(48000,6000,s);
    auto neutralLow=render(48000,80,{}),neutralHigh=render(48000,6000,{});
    double onset=rms(low,4800,5760)/rms(neutralLow,4800,5760);
    double tail=rms(low,26400,28800)/rms(neutralLow,26400,28800);
    double highRatio=rms(high,4800,5760)/rms(neutralHigh,4800,5760);
    ok=ok&&onset>1.5&&tail<.7&&std::abs(highRatio-1)<.01;
    for(unsigned band=0;band<3;++band){
        double hz=std::array<double,3>{80,1000,6000}[band];
        BandTransientSettings settings;settings.attack_db[band]=12;settings.sustain_db[band]=-12;
        auto ref=render(48000,hz,{}),shaped=render(48000,hz,settings);
        double attack=rms(shaped,4800,5760)/rms(ref,4800,5760);
        double sustain=rms(shaped,26400,28800)/rms(ref,26400,28800);
        settings.wet=0;
        ok=ok&&attack>1.5&&sustain<.7&&render(48000,hz,settings)==ref;
        std::printf("BAND_CONTOUR %u attack=%g sustain=%g\n",band,attack,sustain);
    }
    // A non-default crossover pair must still recombine without coloration.
    BandTransientSettings adjusted;adjusted.low_hz=500;adjusted.high_hz=4500;
    auto neutralAdjusted=render(48000,500,adjusted);
    ok=ok&&std::abs(20*std::log10(rms(neutralAdjusted,14400,21600)/(.2/std::sqrt(2.))))<.15;
    adjusted.enabled=false;
    auto bypass=render(48000,500,adjusted);
    for(unsigned i=0;i<bypass.size();++i){
        double amp=i<4800?0:i<24000?.2:i<38400?.04:0;
        ok=ok&&bypass[i]==float(amp*std::sin(2*std::numbers::pi*500*i/48000));
    }
    BandTransient a,b;a.reset(48000,{});b.reset(48000,{});
    s.attack_db={8,-6,4};a.set(s);b.set(s);
    std::array<float,777> l{},r{},x{},y{};
    for(unsigned i=0;i<l.size();++i){l[i]=x[i]=.3*std::sin(i*.17);r[i]=y[i]=-.5f*l[i];}
    float* p[]={l.data(),r.data()};a.process(p,2,l.size());
    for(unsigned i=0;i<x.size();++i){float* q[]={&x[i],&y[i]};b.process(q,2,1);}
    for(unsigned i=0;i<x.size();++i)ok=ok&&std::isfinite(l[i])&&std::abs(l[i]-x[i])<1e-6&&std::abs(r[i]-y[i])<1e-6&&std::abs(r[i]+.5f*l[i])<1e-6;
    GraphSpec spec{{{"native:hyperdsp_transient_filter:2",{},{{-1,1}}}},0};
    spec.controls.push_back({"bass","Bass attack",1,{{0,"low_attack_db",0,12}}});
    std::string error;auto graph=PreparedGraph::prepare(spec,48000,2,error);
    ok=ok&&graph&&graph->latency()==0;
    spec.controls[0].bindings[0]={0,"low_hz",100,300};
    ok=ok&&!PreparedGraph::prepare(spec,48000,2,error);
    std::printf("BAND_TRANSIENT_PASS=%d low_onset=%g low_tail=%g high_onset=%g\n",ok,onset,tail,highRatio);return ok?0:1;
}
