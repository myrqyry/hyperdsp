#include "framework/dsp/native_filter.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
constexpr unsigned sr=48000;
constexpr double pi=3.141592653589793;
static double response(FilterSettings s,double hz){
    NativeFilter engine;engine.reset(sr,s);
    double sum=0,ref=0;
    for(unsigned i=0;i<sr;i++){
        float x=float(.2*std::sin(2*pi*hz*i/sr));
        float y=x;
        float* channels[]={&y};
        engine.process(channels,1,1);
        if(i>=sr/2){sum+=double(y)*y;ref+=double(x)*x;}
    }
    return std::sqrt(sum/ref);
}
int main(){
    FilterSettings s;
    s.frequency=1000;
    s.mode=FilterMode::LowPass;
    auto low=response(s,100), high=response(s,8000);
    s.stages=4;auto steep=response(s,8000);
    s.stages=1;s.mode=FilterMode::HighPass;
    auto hp_low=response(s,100),hp_high=response(s,8000);
    s.mode=FilterMode::BandPass;auto bp_center=response(s,1000),bp_low=response(s,100);
    s.mode=FilterMode::Notch;auto notch=response(s,1000);
    s.mode=FilterMode::Peak;s.gain_db=6;auto peak=response(s,1000);
    s.mode=FilterMode::LowShelf;auto ls_low=response(s,100),ls_high=response(s,8000);
    s.mode=FilterMode::HighShelf;auto hs_low=response(s,100),hs_high=response(s,8000);
    s.mode=FilterMode::Comb;s.comb_depth=1;auto comb_notch=response(s,1000),comb_pass=response(s,500);
    s.mode=FilterMode::AllPass;auto allpass=response(s,1000);
    bool ok=low>.98&&high<.025&&steep<high*.01&&hp_low<.02&&hp_high>.98&&
        bp_center>.98&&bp_center<1.02&&bp_low<.16&&notch<.002&&
        peak>1.97&&peak<2.02&&ls_low>1.95&&ls_high>.98&&ls_high<1.03&&
        hs_high>1.95&&hs_low>.98&&hs_low<1.03&&comb_notch<.002&&comb_pass>.98&&allpass>.98&&allpass<1.02;
    // Channel independence, finite bounded settings, live modulation.
    NativeFilter f;s=FilterSettings{};s.mode=FilterMode::LowPass;s.frequency=1000;
    f.reset(sr,s);
    std::array<float,480> l{},r{};l[0]=1;
    float* ch[]={l.data(),r.data()};f.process(ch,2,l.size());
    for(float v:r)ok=ok&&v==0;
    for(float v:l)ok=ok&&std::isfinite(v);
    s.frequency=std::numeric_limits<float>::quiet_NaN();s.q=100000;s.gain_db=1000;
    s.source=ModSource::Lfo;s.target=ModTarget::Cutoff;s.depth=1;s.hz=20;
    f.set(s);
    for(unsigned block=0;block<100;++block){
        for(unsigned i=0;i<l.size();++i)l[i]=.2f*std::sin(float(i+block*l.size())*.21f);
        f.process(ch,2,l.size());
        for(float v:l)ok=ok&&std::isfinite(v)&&std::abs(v)<100.f;
    }
    s=FilterSettings{};s.mode=FilterMode::LowPass;s.frequency=200;s.source=ModSource::Envelope;s.target=ModTarget::Cutoff;s.depth=1;
    f.reset(sr,s);
    for(unsigned block=0;block<50;++block){l.fill(.8f);f.process(ch,2,l.size());}
    auto opened=f.current_frequency();
    for(unsigned block=0;block<100;++block){l.fill(0);f.process(ch,2,l.size());}
    auto closed=f.current_frequency();
    ok=ok&&opened>1000&&closed<250;
    std::printf("NATIVE_FILTER low=%g high=%g steep=%g hp=%g,%g bp=%g,%g notch=%g peak=%g shelves=%g,%g,%g,%g comb=%g,%g allpass=%g envelope=%g,%g pass=%d\n",low,high,steep,hp_low,hp_high,bp_center,bp_low,notch,peak,ls_low,ls_high,hs_low,hs_high,comb_notch,comb_pass,allpass,opened,closed,ok);
    return ok?0:1;
}
