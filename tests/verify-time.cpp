#include "framework/dsp/time_effect.hpp"
#include <array>
#include <cstdio>
#include <cmath>
#include <limits>
using namespace hyperdsp;
int main(){
    auto near=[](float a,float b){return std::abs(a-b)<.0002f;};
    // A logical reset must hide all old samples, including the second
    // interpolation tap, until new history replaces them after a ring wrap.
    FractionalDelay ring;ring.prepare_samples(4,2);
    auto write=[&](float x){ring.write(0,x);ring.write(1,-x);ring.advance();};
    for(unsigned i=0;i<12;++i)write(9);
    ring.clear();
    bool reset_ok=ring.read(0,1)==0&&ring.read(1,4)==0;
    write(2);reset_ok=reset_ok&&near(ring.read(0,1.5f),1)&&near(ring.read(1,1.5f),-1)&&ring.read(0,2)==0;
    for(unsigned i=3;i<=10;++i)write(float(i));
    reset_ok=reset_ok&&near(ring.read(0,4),7)&&near(ring.read(1,3.5f),-7.5f);
    ring.clear();write(4);reset_ok=reset_ok&&near(ring.read(0,1),4)&&ring.read(0,4)==0;
    std::printf("DELAY_RESET_PASS=%d\n",reset_ok);
    TimeEffect e;TimeSettings s;s.mode=TimeMode::Echo;s.delay_ms=1;s.feedback=.5;s.damping=0;s.wet=1;
    e.reset(48000,2,s);
    bool ok=reset_ok;
    for(unsigned i=0;i<200;++i){
        float l=i==0?1:0,r=0;float* p[]={&l,&r};e.process(p,2,1);
        ok=ok&&near(l,i==48?1:i==96?.5:i==144?.25:i==192?.125:0)&&r==0;
    }
    s.mode=TimeMode::PingPong;e.reset(48000,2,s);
    for(unsigned i=0;i<200;++i){
        float l=i==0?1:0,r=0;float* p[]={&l,&r};e.process(p,2,1);
        ok=ok&&near(l,i==48?1:i==144?.25:0)&&near(r,i==96?.5:i==192?.125:0);
    }
    s.mode=TimeMode::Echo;s.delay_ms=1.5f/48;s.feedback=0;e.reset(48000,1,s);
    for(unsigned i=0;i<8;++i){float x=i==0?1:0;float* p[]={&x};e.process(p,1,1);ok=ok&&near(x,i==1||i==2?.5:0);}
    for(auto mode:{TimeMode::Chorus,TimeMode::Flanger,TimeMode::Vibrato}){
        s=TimeSettings{};s.mode=mode;s.delay_ms=5;s.depth_ms=2;s.rate_hz=.7;s.feedback=.3;s.wet=1;
        e.reset(48000,2,s);TimeEffect block;block.reset(48000,2,s);
        double difference=0,energy=0,stereo=0;
        for(unsigned k=0;k<200;++k){
            std::array<float,113> l{},r{};
            for(unsigned i=0;i<l.size();++i)l[i]=r[i]=.3f*std::sin(2*std::numbers::pi*700*(k*l.size()+i)/48000.);
            auto input=l;float* p[]={l.data(),r.data()};block.process(p,2,l.size());
            for(unsigned i=0;i<l.size();++i){
                float a=input[i],b=a;float* q[]={&a,&b};e.process(q,2,1);
                ok=ok&&near(a,l[i])&&near(b,r[i])&&std::isfinite(a)&&std::isfinite(b);
                if(k>10){difference+=std::abs(a-input[i]);energy+=a*a;stereo+=std::abs(a-b);}
            }
        }
        ok=ok&&difference>100&&energy>10&&stereo>10;
        std::printf("TIME_MOD mode=%d delta=%g energy=%g stereo=%g\n",int(mode),difference,energy,stereo);
    }
    // Negative feedback alternates polarity, and damping only colors repeats.
    s=TimeSettings{};s.delay_ms=1;s.feedback=-.5;s.wet=1;e.reset(48000,1,s);
    for(unsigned i=0;i<160;++i){
        float x=i==0?1:0;float* p[]={&x};e.process(p,1,1);
        ok=ok&&near(x,i==48?1:i==96?-.5:i==144?.25:0);
    }
    s.feedback=.5;s.damping=.5;e.reset(48000,1,s);
    for(unsigned i=0;i<100;++i){
        float x=i==0?1:0;float* p[]={&x};e.process(p,1,1);
        if(i==48||i==96||i==97)ok=ok&&near(x,i==48?1:i==96?.25:.125);
    }
    // Full maximum delay, including ring wrap, at several sample rates.
    for(unsigned rate:{44100u,96000u,192000u}){
        s=TimeSettings{};s.delay_ms=2000;s.feedback=0;s.wet=1;e.reset(rate,1,s);
        for(unsigned i=0;i<2*rate+8;++i){
            float x=i==0?1:0;float* p[]={&x};e.process(p,1,1);
            ok=ok&&near(x,i==2*rate?1:0);
        }
    }
    s=TimeSettings{};s.delay_ms=1;s.feedback=.95;s.wet=1;s.routes[0]={6,4,0,1,-1,10,100,0};
    e.reset(48000,1,s);e.external_values({1,0,0,0});
    float x=0;float* p[]={&x};
    for(int i=0;i<48000;++i){x=.2f;e.process(p,1,1);}
    ok=ok&&near(x,.2);
    x=std::numeric_limits<float>::quiet_NaN();e.process(p,1,1);ok=ok&&std::isfinite(x);
    s.mode=TimeMode::Bypass;e.set(s);x=.3;e.process(p,1,1);ok=ok&&near(x,.3);
    std::printf("TIME_CORE_PASS=%d\n",ok);return ok?0:1;
}
