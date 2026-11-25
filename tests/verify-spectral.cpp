#include "framework/dsp/spectral.hpp"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace hyperdsp;
int main(){
    Spectral e;SpectralSettings s;s.mode=SpectralMode::Gate;s.range=0;s.wet=1;
    e.reset(48000,2,s);
    bool ok=e.latency_samples()==1024;
    double err=0,power=0;
    auto input=[](int i){return i<0?0.f:.3f*std::sin(i*.071f)+.07f*std::cos(i*.193f);};
    for(int i=0;i<24000;++i){
        float x=input(i),y=0;float* p[]={&x,&y};e.process(p,2,1);
        float expected=input(i-1024);err+=(x-expected)*(x-expected);power+=expected*expected;
        ok=ok&&std::abs(y)<1e-8&&std::isfinite(x);
    }
    double reconstruction=std::sqrt(err/power);ok=ok&&reconstruction<.00001;
    s.range=80;s.threshold=-25;s.attack=1;s.release=1;
    e.reset(48000,1,s);
    double loud=0,quiet=0;
    for(int i=0;i<48000;++i){
        float x=.4f*std::sin(2*std::numbers::pi*1500*i/48000)+.005f*std::sin(2*std::numbers::pi*6000*i/48000);
        float* p[]={&x};e.process(p,1,1);
        if(i>=24000){loud+=x*std::sin(2*std::numbers::pi*1500*i/48000);quiet+=x*std::sin(2*std::numbers::pi*6000*i/48000);}
    }
    loud=std::abs(loud)*2/24000;quiet=std::abs(quiet)*2/24000;
    ok=ok&&loud>.35&&quiet<.0001;
    s=SpectralSettings{};s.mode=SpectralMode::Freeze;s.wet=1;s.hold=0;
    e.reset(48000,1,s);
    double frozen=0,released=0;
    for(int i=0;i<48000;++i){
        if(i==12000){s.hold=1;e.set(s);}
        if(i==36000){s.hold=0;e.set(s);}
        float x=i<16000?.3f*std::sin(2*std::numbers::pi*1500*i/48000):0;
        float* p[]={&x};e.process(p,1,1);
        if(i>=24000&&i<34000)frozen+=x*x;
        if(i>=42000)released+=x*x;
    }
    frozen=std::sqrt(frozen/10000);released=std::sqrt(released/6000);
    ok=ok&&frozen>.18&&frozen<.25&&released<.00001;
    s=SpectralSettings{};s.mode=SpectralMode::Smear;s.smear_ms=200;s.wet=1;
    e.reset(48000,1,s);double tail=0,late=0;
    for(int i=0;i<96000;++i){
        float x=i<24000?.3f*std::sin(2*std::numbers::pi*1500*i/48000):0;
        float* p[]={&x};e.process(p,1,1);
        if(i>=30000&&i<36000)tail+=x*x;
        if(i>=90000)late+=x*x;
    }
    ok=ok&&tail>1&&late<.01;
    // Off-bin phase advance must keep the original frequency, rather
    // than snapping a frozen tone to the nearest FFT bin.
    s=SpectralSettings{};s.mode=SpectralMode::Freeze;s.wet=1;
    e.reset(48000,1,s);
    double re=0,im=0,energy=0;
    for(int i=0;i<144000;++i){
        if(i==24000){s.hold=1;e.set(s);}
        const double phase=2*std::numbers::pi*1172*i/48000;
        float x=i<28000?.3f*std::sin(phase):0;
        float* ptr[]={&x};e.process(ptr,1,1);
        if(i>=96000){re+=x*std::cos(phase);im+=x*std::sin(phase);energy+=x*x;}
    }
    const double retained=2*std::hypot(re,im)/48000;
    ok=ok&&retained>.28&&retained<.32&&energy>1900&&energy<2400;
    std::printf("SPECTRAL_OFFBIN retained=%g energy=%g\n",retained,energy);
    for(unsigned rate:{44100u,96000u})for(auto mode:{SpectralMode::Gate,SpectralMode::Freeze,SpectralMode::Smear}){
        s=SpectralSettings{};s.mode=mode;s.wet=.65f;s.threshold=-30;
        Spectral block;block.reset(rate,2,s);e.reset(rate,2,s);
        for(unsigned k=0;k<80;++k){
            std::array<float,127> l{},r{};
            for(unsigned i=0;i<l.size();++i){l[i]=.3f*std::sin(.071f*(k*l.size()+i));r[i]=.2f*std::sin(.13f*(k*l.size()+i));}
            if(k==32){s.hold=1;e.set(s);block.set(s);}
            auto left=l,right=r;float* p[]={l.data(),r.data()};block.process(p,2,l.size());
            for(unsigned i=0;i<l.size();++i){
                float a=left[i],b=right[i];float* q[]={&a,&b};e.process(q,2,1);
                ok=ok&&std::isfinite(a)&&std::isfinite(b)&&std::abs(a-l[i])<1e-6&&std::abs(b-r[i])<1e-6;
            }
        }
    }
    s=SpectralSettings{};s.mode=SpectralMode::Smear;s.smear_ms=200;
    // Wet=0 still aligns to spectral latency. True bypass does not delay.
    s.wet=0;e.reset(48000,1,s);
    for(int i=0;i<2048;++i){float x=i==0?.7f:0;float* p[]={&x};e.process(p,1,1);ok=ok&&std::abs(x-(i==1024?.7f:0))<1e-6;}
    s.mode=SpectralMode::Bypass;e.set(s);float x=.3;float* p[]={&x};e.process(p,1,1);ok=ok&&x==.3f;
    std::printf("SPECTRAL_CORE reconstruction=%g loud=%g quiet=%g frozen=%g released=%g tail=%g late=%g pass=%d\n",reconstruction,loud,quiet,frozen,released,tail,late,ok);
    return ok?0:1;
}
