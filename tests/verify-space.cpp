#include "framework/dsp/space.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
int main(){
    Space e;SpaceSettings s;s.mode=SpaceMode::Phaser;s.center=1000;s.depth=0;s.feedback=0;s.wet=.5;s.stages=2;
    e.reset(48000,2,s);
    double power=0;
    for(unsigned i=0;i<48000;++i){
        float x=.4f*std::sin(2*std::numbers::pi*1000*i/48000),y=x*.4f;float* p[]={&x,&y};
        e.process(p,2,1);if(i>24000)power+=x*x;
    }
    const double notch=std::sqrt(power/23999);
    bool ok=notch<.0001;
    s.wet=1;e.reset(48000,2,s);power=0;
    for(unsigned i=0;i<48000;++i){
        float x=.4f*std::sin(2*std::numbers::pi*1000*i/48000),y=0;float* p[]={&x,&y};
        e.process(p,2,1);if(i>24000)power+=x*x;ok=ok&&y==0;
    }
    ok=ok&&std::abs(std::sqrt(power/23999)-.282843)<.001;
    s.mode=SpaceMode::Reverb;s.wet=1;s.predelay=20;s.decay=1;s.damping=0;s.width=1;
    for(unsigned rate:{44100u,48000u,96000u}){
        e.reset(rate,2,s);
        double early=0,late=0,cross=0,tail=0;unsigned first=0;
        for(unsigned i=0;i<rate*3;++i){
            float x=i==0?1:0,y=0;float* p[]={&x,&y};e.process(p,2,1);
            if(!first&&std::abs(x)+std::abs(y)>1e-7)first=i;
            if(i<rate/2)early+=x*x+y*y;
            if(i>rate&&i<rate*3/2)late+=x*x+y*y;
            if(i>rate*2)tail+=x*x+y*y;
            cross+=y*y;
            ok=ok&&std::isfinite(x)&&std::isfinite(y);
        }
        ok=ok&&first>=unsigned(rate*.02)&&first<rate/10&&early>.01&&cross>.001&&late<early*.002&&tail<early*.000002;
        std::printf("SPACE_REVERB rate=%u first=%u early=%g late=%g tail=%g cross=%g\n",rate,first,early,late,tail,cross);
    }
    // Decay and damping are independent from the dry mix. Compare the
    // late energy from the same impulse at different nominal decay times.
    auto late_energy=[&](float decay,float damping,float width){
        SpaceSettings t;t.mode=SpaceMode::Reverb;t.wet=1;t.predelay=0;
        t.decay=decay;t.damping=damping;t.width=width;
        e.reset(48000,2,t);
        double energy=0,side=0;
        for(unsigned i=0;i<96000;++i){
            float x=i==0?1:0,y=0;float* p[]={&x,&y};e.process(p,2,1);
            if(i>48000)energy+=x*x+y*y;
            side+=std::abs(x-y);
            ok=ok&&std::isfinite(x)&&std::isfinite(y);
        }
        if(width==0)ok=ok&&side<1e-6;
        return energy;
    };
    auto short_tail=late_energy(.4,0,1),long_tail=late_energy(3,0,1);
    auto damped_tail=late_energy(3,.8,1);
    late_energy(1,0,0);
    ok=ok&&long_tail>short_tail*100&&long_tail>.001&&damped_tail<long_tail;
    std::printf("SPACE_TAIL short=%g long=%g damped=%g\n",short_tail,long_tail,damped_tail);
    // Processing must be invariant to block partitioning, including active
    // phase sweep and a live, nonzero reverb feedback network.
    for(auto mode:{SpaceMode::Phaser,SpaceMode::Reverb}){
        SpaceSettings t;t.mode=mode;t.wet=.65f;t.decay=2;t.predelay=3;
        Space block;block.reset(48000,2,t);e.reset(48000,2,t);
        for(unsigned k=0;k<160;++k){
            std::array<float,127> l{},r{};
            for(unsigned i=0;i<l.size();++i){l[i]=.3f*std::sin(.071f*(k*l.size()+i));r[i]=.2f*std::sin(.13f*(k*l.size()+i));}
            auto left=l,right=r;float* p[]={l.data(),r.data()};block.process(p,2,l.size());
            for(unsigned i=0;i<l.size();++i){
                float x=left[i],y=right[i];float* q[]={&x,&y};e.process(q,2,1);
                ok=ok&&std::isfinite(x)&&std::isfinite(y)&&std::abs(x-l[i])<1e-6&&std::abs(y-r[i])<1e-6;
            }
        }
    }
    s.mode=SpaceMode::Phaser;s.wet=.5;s.routes[0]={6,4,0,1,-.5,10,100,0};
    e.reset(48000,1,s);e.external_values({1,0,0,0});
    float x=0;float* p[]={&x};
    for(unsigned i=0;i<48000;++i){x=.3;e.process(p,1,1);}
    ok=ok&&std::abs(x-.3)<.0001;
    x=std::numeric_limits<float>::quiet_NaN();e.process(p,1,1);ok=ok&&std::isfinite(x);
    std::printf("SPACE_CORE notch=%g pass=%d\n",notch,ok);return ok?0:1;
}
