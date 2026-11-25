#include "framework/dsp/formant.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
static double response(FormantSettings s,double hz,unsigned rate=48000){
    Formant e;e.reset(rate,s);
    double re=0,im=0,error=0,energy=0;
    for(unsigned i=0;i<rate/2;++i){
        double a=2*std::numbers::pi*hz*i/rate;
        float x=.3f*std::sin(a),y=-.4f*x;float* p[]={&x,&y};e.process(p,2,1);
        if(i>=rate/4){re+=x*std::cos(a);im+=x*std::sin(a);energy+=x*x;error+=(y+.4*x)*(y+.4*x);}
    }
    if(error>energy*1e-10)return -1;
    return 8*std::hypot(re,im)/rate/.3;
}
int main(){
    FormantSettings s;s.mode=FormantMode::Custom;s.frequency={500,1500,2500};
    s.bandwidth={100,100,100};s.gain={0,-36,-36};
    bool ok=true;
    for(unsigned rate:{44100u,48000u,96000u}){
        auto peak=response(s,500,rate),outside=response(s,100,rate);
        ok=ok&&peak>.32&&peak<.35&&outside>0&&outside<.02;
        s.shift=12;auto raised=response(s,1000,rate),old=response(s,500,rate);s.shift=0;
        ok=ok&&raised>.32&&old<.05;
        std::printf("FORMANT_CUSTOM rate=%u peak=%g outside=%g raised=%g old=%g\n",rate,peak,outside,raised,old);
    }
    // Width alters off-center response, while gain alters the peak.
    const auto narrow=response(s,600);s.width=4;const auto broad=response(s,600);s.width=1;
    s.gain[0]=-6;const auto quieter=response(s,500);s.gain[0]=0;
    ok=ok&&broad>narrow*1.8&&quieter>.16&&quieter<.18;
    std::printf("FORMANT_WIDTH narrow=%g broad=%g quieter=%g\n",narrow,broad,quieter);
    s=FormantSettings{};s.mode=FormantMode::Vowel;s.from=0;s.to=2;s.morph=0;
    auto a=response(s,800),a2=response(s,270);
    s.morph=1;auto i=response(s,270),i2=response(s,800);
    s.morph=.5;auto middle=response(s,std::sqrt(800.*270));
    ok=ok&&a>.30&&a>a2*5&&i>.30&&i>i2*5&&middle>.30;
    std::printf("FORMANT_VOWEL a=%g a_at_i=%g i=%g i_at_a=%g middle=%g\n",a,a2,i,i2,middle);
    s.morph=0;s.routes[0]={6,0,0,1,1,10,100,0};
    Formant e,block;e.reset(48000,s);block.reset(48000,s);e.external_values({1,0,0,0});block.external_values({1,0,0,0});
    double active=0,fallback=0;
    for(unsigned k=0;k<480;++k){
        if(k==240){e.external_values({0,0,0,0});block.external_values({0,0,0,0});}
        std::array<float,127> l{},r{};
        for(unsigned j=0;j<l.size();++j)l[j]=.3f*std::sin(2*std::numbers::pi*270*(k*l.size()+j)/48000);
        auto input=l;float* p[]={l.data(),r.data()};block.process(p,2,l.size());
        for(unsigned j=0;j<l.size();++j){
            float x=input[j],y=0;float* q[]={&x,&y};e.process(q,2,1);
            ok=ok&&std::isfinite(x)&&std::abs(x-l[j])<1e-6&&y==0&&r[j]==0;
            if(k>=120&&k<240)active+=x*x;
            if(k>=360)fallback+=x*x;
        }
    }
    ok=ok&&active>fallback*25;
    // Exercise the custom offset and gain routes at maximum sample-rate bounds.
    s=FormantSettings{};s.mode=FormantMode::Custom;s.frequency={500,1500,2500};
    s.bandwidth={100,100,100};s.gain={0,-36,-36};
    s.routes[0]={6,5,0,1,500,10,100,0};s.routes[1]={6,8,0,1,-6,10,100,0};
    e.reset(48000,s);e.external_values({1,1,0,0});
    double re=0,im=0;
    for(unsigned j=0;j<48000;++j){
        double a=2*std::numbers::pi*1000*j/48000;
        float x=.3f*std::sin(a);float* p[]={&x};e.process(p,1,1);
        if(j>=24000){re+=x*std::cos(a);im+=x*std::sin(a);}
    }
    double routed=2*std::hypot(re,im)/24000/.3;ok=ok&&routed>.16&&routed<.18;
    std::printf("FORMANT_ROUTES offset_gain=%g\n",routed);
    s.routes={};s.frequency={16000,16000,16000};s.bandwidth={20,20,20};s.gain={6,6,6};
    s.shift=24;s.width=.25f;
    e.reset(8000,s);
    for(unsigned j=0;j<16000;++j){float x=j==0?1:0;float* p[]={&x};e.process(p,1,1);ok=ok&&std::isfinite(x)&&std::abs(x)<6;}
    float invalid=std::numeric_limits<float>::quiet_NaN();float* q[]={&invalid};e.process(q,1,1);ok=ok&&std::isfinite(invalid);
    s.mode=FormantMode::Bypass;e.set(s);float x=.3f;float* p[]={&x};e.process(p,1,1);ok=ok&&x==.3f;
    std::printf("FORMANT_MOD active=%g fallback=%g pass=%d\n",active,fallback,ok);
    return ok?0:1;
}
