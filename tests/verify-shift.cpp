#include "framework/dsp/spectral.hpp"
#include <cstdio>
#include <cmath>
using namespace hyperdsp;
struct Result{double target,original,rms,other;};
Result measure(SpectralMode mode,float semitones,float hz,float input,float target,unsigned rate=48000){
    Spectral e;SpectralSettings s;s.mode=mode;s.pitch=semitones;s.shift_hz=hz;
    e.reset(rate,2,s);
    double re=0,im=0,ore=0,oim=0,power=0,other=0;
    for(unsigned i=0;i<rate*2;++i){
        float x=.3f*std::sin(2*std::numbers::pi*input*i/rate),y=0;
        float* p[]={&x,&y};e.process(p,2,1);
        if(i>=rate){
            const double phase=2*std::numbers::pi*target*i/rate, orig=2*std::numbers::pi*input*i/rate;
            re+=x*std::cos(phase);im+=x*std::sin(phase);
            ore+=x*std::cos(orig);oim+=x*std::sin(orig);
            power+=x*x;other+=y*y;
        }
    }
    return {2*std::hypot(re,im)/rate,2*std::hypot(ore,oim)/rate,std::sqrt(power/rate),std::sqrt(other/rate)};
}
int main(){
    bool ok=true;
    for(auto rate:{44100u,48000u,96000u}){
        for(auto a:{measure(SpectralMode::Pitch,12,0,1172,2344,rate),
                    measure(SpectralMode::Pitch,-12,0,1172,586,rate),
                    measure(SpectralMode::FrequencyShift,0,300,1172,1472,rate),
                    measure(SpectralMode::FrequencyShift,0,-300,1172,872,rate)}){
            std::printf("SHIFT rate=%u target=%g original=%g rms=%g silent=%g\n",rate,a.target,a.original,a.rms,a.other);
            ok=ok&&a.target>.20&&a.target<.37&&a.original<.01&&a.rms<.30&&a.other<1e-8;
        }
    }
    // Three partials: pitch preserves ratios, frequency shift preserves spacing.
    for(auto mode:{SpectralMode::Pitch,SpectralMode::FrequencyShift}){
        Spectral e;SpectralSettings s;s.mode=mode;s.pitch=12;s.shift_hz=300;
        e.reset(48000,2,s);
        constexpr double frequencies[]={600,1200,1800};
        std::array<double,3> re{},im{};
        double error=0,energy=0;
        for(unsigned i=0;i<96000;++i){
            float x=0;for(auto f:frequencies)x+=.12f*std::sin(2*std::numbers::pi*f*i/48000);
            float y=-x*.4f;float* p[]={&x,&y};e.process(p,2,1);
            if(i>=48000){
                for(unsigned j=0;j<3;++j){
                    const double f=mode==SpectralMode::Pitch?2*frequencies[j]:frequencies[j]+300;
                    const double a=2*std::numbers::pi*f*i/48000;
                    re[j]+=x*std::cos(a);im[j]+=x*std::sin(a);
                }
                energy+=x*x;error+=(y+.4*x)*(y+.4*x);
            }
        }
        std::printf("SHIFT_POLY mode=%d amps",int(mode));
        for(unsigned j=0;j<3;++j){double amp=2*std::hypot(re[j],im[j])/48000;ok=ok&&amp>.075&&amp<.16;std::printf(" %g",amp);}
        const double stereo=std::sqrt(error/energy);ok=ok&&stereo<.002;
        std::printf(" stereo_error=%g\n",stereo);
    }
    // Test active modulation and block partition invariance.
    for(auto mode:{SpectralMode::Pitch,SpectralMode::FrequencyShift}){
        Spectral e,block;SpectralSettings s;s.mode=mode;
        s.routes[0]={6,mode==SpectralMode::Pitch?6:7,0,1,mode==SpectralMode::Pitch?12.:300.,10,100,0};
        e.reset(48000,1,s);block.reset(48000,1,s);e.external_values({1,0,0,0});block.external_values({1,0,0,0});
        double re=0,im=0;
        for(unsigned k=0;k<600;++k){
            std::array<float,127> l{};
            for(unsigned i=0;i<l.size();++i)l[i]=.3f*std::sin(2*std::numbers::pi*1172*(k*l.size()+i)/48000);
            auto input=l;float* p[]={l.data()};block.process(p,1,l.size());
            for(unsigned i=0;i<l.size();++i){
                float x=input[i];float* q[]={&x};e.process(q,1,1);
                ok=ok&&std::isfinite(x)&&std::abs(x-l[i])<1e-6;
                if(k>=300){const auto a=2*std::numbers::pi*(mode==SpectralMode::Pitch?2344:1472)*(k*l.size()+i)/48000;re+=x*std::cos(a);im+=x*std::sin(a);}
            }
        }
        const double amp=2*std::hypot(re,im)/(300*127);ok=ok&&amp>.2;
        std::printf("SHIFT_MOD mode=%d amplitude=%g\n",int(mode),amp);
    }
    auto unity=measure(SpectralMode::Pitch,0,0,1172,1172);
    auto discarded=measure(SpectralMode::Pitch,12,0,15000,18000);
    auto below=measure(SpectralMode::FrequencyShift,0,-1500,1172,328);
    ok=ok&&std::abs(unity.target-.3)<1e-5&&discarded.rms<.005&&below.rms<.005;
    std::printf("SHIFT_BOUNDARY unity=%g upper=%g lower=%g pass=%d\n",unity.target,discarded.rms,below.rms,ok);
    return ok?0:1;
}
