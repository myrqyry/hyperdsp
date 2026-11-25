#include "framework/dsp/color.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
int main(){
    auto near=[](float a,float b){return std::abs(a-b)<.0001f;};
    bool ok=near(Color::shape(ColorMode::Saturation,1),std::tanh(1.f))&&
        near(Color::shape(ColorMode::SoftClip,.5f),.6875f)&&
        near(Color::shape(ColorMode::HardClip,2),1)&&
        near(Color::shape(ColorMode::Fold,1.5f),.5f)&&
        near(Color::shape(ColorMode::Fold,-1.5f),-.5f);
    Color e;ColorSettings s;s.mode=ColorMode::Crush;s.bits=3;s.reduce=4;
    e.reset(48000,s);
    float l[]={.3f,.8f,.8f,.8f,-.3f,-.8f,-.8f,-.8f},r[8]{};
    float* p[]={l,r};e.process(p,2,8);
    for(unsigned i=0;i<8;++i)ok=ok&&near(l[i],i<4?.25f:-.25f)&&r[i]==0;
    auto alias=[&](unsigned os){
        s=ColorSettings{};s.mode=ColorMode::HardClip;s.drive=18;s.oversample=os;
        e.reset(48000,s);
        double re=0,im=0;
        for(unsigned i=0;i<48000;++i){
            float x=.6f*std::sin(2*std::numbers::pi*10000*i/48000);
            float* ch[]={&x};e.process(ch,1,1);
            if(i>=24000){double a=2*std::numbers::pi*18000*i/48000;re+=x*std::cos(a);im+=x*std::sin(a);}
        }
        return 2*std::hypot(re,im)/24000;
    };
    auto a1=alias(1),a2=alias(2),a4=alias(4);
    ok=ok&&a1>.15&&a2<a1*.1&&a4<a1*.1;
    // Oversampled parallel dry path must have matching integer latency.
    s=ColorSettings{};s.mode=ColorMode::Saturation;s.oversample=4;s.wet=0;
    e.reset(48000,s);
    for(unsigned i=0;i<100;++i){
        float x=i==0?.5f:0.f;float* ch[]={&x};e.process(ch,1,1);
        ok=ok&&near(x,i==24?.5f:0.f);
    }
    // With a tiny signal the nonlinear curve is effectively linear;
    // verify actual wet-path phase, gain, channel isolation and block continuity.
    for(unsigned rate:{44100u,48000u,96000u})for(unsigned factor:{2u,4u}){
        s=ColorSettings{};s.oversample=factor;
        e.reset(rate,s);
        Color chunked;chunked.reset(rate,s);
        std::array<float,257> left{},right{};
        double error=0,energy=0;
        for(unsigned k=0;k<40;++k){
            for(unsigned i=0;i<left.size();++i){
                const unsigned n=k*left.size()+i;
                left[i]=.01f*std::sin(2*std::numbers::pi*1000*n/rate);
                right[i]=0;
            }
            auto one=left;
            float* block[]={left.data(),right.data()};
            chunked.process(block,2,left.size());
            for(unsigned i=0;i<left.size();++i){
                const unsigned n=k*left.size()+i;
                float x=one[i];float* ptr[]={&x};e.process(ptr,1,1);
                ok=ok&&near(x,left[i])&&right[i]==0;
                if(n>100){
                    const double expected=.01*std::sin(2*std::numbers::pi*1000*(n-24)/rate);
                    error+=(x-expected)*(x-expected);energy+=expected*expected;
                }
            }
        }
        ok=ok&&std::sqrt(error/energy)<.003&&e.latency_samples()==24;
    }
    // Exact signed quantizer has 2^bits levels, including negative full-scale.
    s=ColorSettings{};s.mode=ColorMode::Crush;s.bits=2;s.reduce=1;e.reset(48000,s);
    for(float input:{-1.f,-.5f,0.f,.5f,1.f}){
        float x=input;float* ptr[]={&x};e.process(ptr,1,1);
        ok=ok&&near(x,std::min(input,.5f));
    }
    s.mode=ColorMode::Bypass;e.set(s);float x=.4f;float* ch[]={&x};e.process(ch,1,1);ok=ok&&near(x,.4);
    s.mode=ColorMode::HardClip;s.oversample=1;s.wet=1;s.drive=0;
    s.routes[0]={6,0,0,1,20,10,100,0};
    e.reset(48000,s);e.external_values({1,0,0,0});
    for(unsigned i=0;i<48000;++i){x=.2f;e.process(ch,1,1);}
    ok=ok&&near(x,1);
    x=std::numeric_limits<float>::quiet_NaN();e.process(ch,1,1);ok=ok&&std::isfinite(x);
    std::printf("COLOR_CORE alias1=%g alias2=%g alias4=%g pass=%d\n",a1,a2,a4,ok);
    return ok?0:1;
}
