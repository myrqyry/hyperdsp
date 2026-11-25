#include "framework/dsp/dynamics.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
static float steady(DynamicsSettings s,float x,float right=0,unsigned rate=48000){
    Dynamics d;d.reset(rate,s);
    std::array<float,256> l{},r{};float* p[]={l.data(),r.data()};
    for(unsigned k=0;k<rate/64;++k){l.fill(x);r.fill(right);d.process(p,2,l.size());}
    return l.back();
}
int main(){
    auto near=[](float a,float b){return std::abs(a-b)<.0002f;};
    DynamicsSettings s;s.threshold=-20;s.ratio=4;s.knee=0;s.attack=1;s.release=20;
    const float comp=steady(s,1), quiet=steady(s,.05);
    bool ok=near(comp,std::pow(10.f,-15.f/20))&&near(quiet,.05);
    s.mode=DynamicsMode::Expander;s.ratio=2;
    const float exp=steady(s,.01);ok=ok&&near(exp,.001)&&near(steady(s,.5),.5);
    s.mode=DynamicsMode::Gate;s.range=60;
    const float gate=steady(s,.01);ok=ok&&near(gate,.00001)&&near(steady(s,.5),.5);
    s.mode=DynamicsMode::Limiter;s.threshold=-6;
    Dynamics d;d.reset(48000,s);
    float a=1,b=.25;float* p[]={&a,&b};d.process(p,2,1);
    ok=ok&&near(a,std::pow(10.f,-6.f/20))&&near(b/a,.25);
    s.mode=DynamicsMode::Compressor;s.threshold=-20;s.ratio=4;s.wet=.5;
    ok=ok&&near(steady(s,1),(.5f+.5f*comp));
    s.wet=1;s.mode=DynamicsMode::Bypass;s.input_db=12;
    ok=ok&&near(steady(s,.5),.5);
    ok=ok&&near(Dynamics::curve(-20,-20,4,6,DynamicsMode::Compressor,60),-.5625f);
    s=DynamicsSettings{};s.threshold=-20;s.knee=0;s.attack=1;s.release=20;
    s.routes[0]={6,0,0,1,20,10,100,0};
    d.reset(48000,s);d.external_values({1,0,0,0});
    for(int i=0;i<48000;++i){a=.5;b=.2;d.process(p,2,1);}
    const float external=a;
    ok=ok&&near(a,.5)&&near(b,.2);
    // Gate remains open in the hysteresis band, then holds before closing.
    s=DynamicsSettings{};s.mode=DynamicsMode::Gate;s.threshold=-20;s.range=60;
    s.hold=50;s.hysteresis=6;s.attack=.1f;s.release=5;
    d.reset(48000,s);
    auto run=[&](float x,unsigned n){for(unsigned i=0;i<n;++i){a=x;b=x*.4f;d.process(p,2,1);}return a;};
    const float opened=run(.2f,4800),hysteresis=run(.07f,4800);
    const float held=run(.01f,1000),closed=run(.01f,12000);
    ok=ok&&near(opened,.2)&&near(hysteresis,.07)&&near(held,.01)&&near(closed,.00001);
    // With unity output trim and fully wet, even isolated full-scale
    // transients and makeup gain must respect the sample ceiling.
    for(unsigned rate:{44100u,48000u,96000u}){
        s=DynamicsSettings{};s.mode=DynamicsMode::Limiter;s.threshold=-3;s.makeup=12;
        d.reset(rate,s);
        const float ceiling=std::pow(10.f,-3.f/20);
        for(unsigned i=0;i<rate/4;++i){
            a=i%97==0?2.f:.25f*std::sin(i*.37f);b=a*-.4f;
            d.process(p,2,1);
            ok=ok&&std::isfinite(a)&&std::isfinite(b)&&std::abs(a)<=ceiling+1e-6f&&std::abs(b)<=ceiling+1e-6f;
        }
        a=std::numeric_limits<float>::quiet_NaN();b=.25f;d.process(p,2,1);
        ok=ok&&a==0&&std::isfinite(b);
    }
    std::printf("DYNAMICS_GATE open=%g hysteresis=%g hold=%g closed=%g\n",opened,hysteresis,held,closed);
    std::printf("DYNAMICS_CORE comp=%g quiet=%g exp=%g gate=%g external=%g pass=%d\n",comp,quiet,exp,gate,external,ok);
    return ok?0:1;
}
