#pragma once
#include "modulation_matrix.hpp"
#include "fractional_delay.hpp"
#include <array>
#include <cmath>
#include <numbers>

namespace hyperdsp {
enum class SpaceMode { Bypass, Phaser, Reverb };
struct SpaceSettings {
    SpaceMode mode=SpaceMode::Phaser;
    float center=800,depth=2,rate_hz=.4f,feedback=.3f,width=1;
    unsigned stages=6;
    float predelay=15,decay=1.5f,damping=.35f,size=1;
    float wet=.5f,input_db=0,output_db=0;
    std::array<RouteConfig,modulation_slots> routes{};
};
// Four-line orthogonal feedback network per channel pair, with independent
// all-pass diffusion on each stereo input. Every buffer is prepared off the callback.
class Space {
    static constexpr unsigned lines=4;
    struct Pair {
        FractionalDelay network,pre,diff;
        std::array<float,lines> low{};
        std::array<float,lines> lengths{},gains{};
        float last_decay=-1,last_size=-1;
        void prepare(unsigned rate){
            network.prepare_samples(unsigned(std::ceil(rate*.086)),lines);
            pre.prepare_samples(unsigned(std::ceil(rate*.2)),2);
            diff.prepare_samples(unsigned(std::ceil(rate*.013)),2);
            clear();
        }
        void clear(){network.clear();pre.clear();diff.clear();low={};last_decay=last_size=-1;}
        std::array<float,2> next(float left,float right,unsigned rate,float predelay,float decay,float damping,float size,float width){
            if(size!=last_size||decay!=last_decay){
                constexpr float ms[]={29.7f,37.1f,41.1f,43.7f};
                for(unsigned j=0;j<lines;++j){
                    lengths[j]=std::max(1.f,std::round(ms[j]*size*rate*.001f));
                    gains[j]=std::exp(float(-3*std::numbers::ln10)*lengths[j]/(rate*decay));
                }
                last_size=size;last_decay=decay;
            }
            std::array<float,2> in{left,right};
            for(unsigned c=0;c<2;++c){
                const float x=in[c];
                in[c]=predelay*rate*.001f>=1?pre.read(c,predelay*rate*.001f):x;
                pre.write(c,x);
                const float z=diff.read(c,rate*(c?.0117f:.0079f));
                // Schroeder all-pass diffuser, |coefficient| < 1.
                const float v=in[c]+.6f*z;
                diff.write(c,v);in[c]=z-.6f*v;
            }
            pre.advance();diff.advance();
            std::array<float,lines> y{},v{};
            for(unsigned j=0;j<lines;++j){
                y[j]=network.read(j,lengths[j]);
                low[j]=(1-damping)*y[j]+damping*low[j];
                if(std::abs(low[j])<1e-20f)low[j]=0;
                v[j]=low[j]*gains[j];
            }
            // Normalized Hadamard transform: energy-preserving before damping
            // and per-line attenuation. Orthogonal stereo injection/projection.
            const float mid=.5f*(in[0]+in[1]),side=.5f*(in[0]-in[1]);
            const std::array<float,4> h{
                .5f*(v[0]+v[1]+v[2]+v[3]),
                .5f*(v[0]-v[1]+v[2]-v[3]),
                .5f*(v[0]+v[1]-v[2]-v[3]),
                .5f*(v[0]-v[1]-v[2]+v[3])};
            for(unsigned j=0;j<lines;++j){
                float x=h[j]+.5f*(mid+(j<2?side:-side));
                network.write(j,std::isfinite(x)?std::clamp(x,-128.f,128.f):0.f);
            }
            network.advance();
            const float m=.5f*(y[0]+y[1]+y[2]+y[3]);
            const float s=.5f*(y[0]+y[1]-y[2]-y[3])*width;
            return {m+s,m-s};
        }
    };
    struct PhaserChannel {std::array<float,12> z{};float previous=0;};
    std::array<PhaserChannel,8> phaser_{};
    std::array<Pair,4> pairs_{};
    SpaceSettings settings_{};
    MixModulator mixer_;
    ModulationMatrix matrix_;
    LinearRamp center_,depth_,speed_,feedback_,width_,predelay_,decay_,damping_,size_;
    unsigned rate_=48000,channels_=0,ramp_=480;
    bool any_route_=false;
    double phase_=0;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    void configure(SpaceSettings s,bool reset){
        if(unsigned(s.mode)>2)s.mode=SpaceMode::Bypass;
        s.center=bound(s.center,40,8000,800);s.depth=bound(s.depth,0,4,2);
        s.rate_hz=bound(s.rate_hz,.05,10,.4);s.feedback=bound(s.feedback,-.85,.85,.3);
        s.width=bound(s.width,0,1,1);s.stages=std::clamp(s.stages,2u,12u)&~1u;
        s.predelay=bound(s.predelay,0,200,15);s.decay=bound(s.decay,.1,10,1.5);
        s.damping=bound(s.damping,0,.99,.35);s.size=bound(s.size,.5,1.9,1);
        const bool changed=reset||s.mode!=settings_.mode||s.stages!=settings_.stages;
        if(changed){
            phaser_={};phase_=0;
            for(unsigned i=0;i<(channels_+1)/2;++i)pairs_[i].clear();
        }
        settings_=s;
        auto param=[&](LinearRamp& r,float v){if(changed)r.reset(v);else r.set(v,ramp_);};
        param(center_,s.center);param(depth_,s.depth);param(speed_,s.rate_hz);param(feedback_,s.feedback);
        param(width_,s.width);param(predelay_,s.predelay);param(decay_,s.decay);param(damping_,s.damping);param(size_,s.size);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        any_route_=false;
        constexpr float limits[]={8000,4,10,.85f,1,1,10,.99f,200,1.4f,1};
        for(unsigned i=0;i<modulation_slots;++i){
            auto c=s.routes[i];
            if(c.destination<0||c.destination>=11)c.source=0;
            else c.amount=bound(c.amount,-limits[c.destination],limits[c.destination],0);
            matrix_.configure(i,rate_,c,reset);any_route_=any_route_||matrix_.active(i);
        }
    }
public:
    void reset(unsigned rate,unsigned channels,SpaceSettings s){
        rate_=std::clamp(rate,8000u,192000u);channels_=std::clamp(channels,1u,8u);ramp_=std::max(1u,rate_/100);
        for(unsigned i=0;i<(channels_+1)/2;++i)pairs_[i].prepare(rate_);
        configure(s,true);
    }
    void set(SpaceSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    void process(float* const* p,unsigned count,unsigned frames){
        if(!p||count!=channels_||!count)return;
        for(unsigned i=0;i<frames;++i){
            auto mix=mixer_.next();
            float center=center_.next(),depth=depth_.next(),hz=speed_.next(),fb=feedback_.next(),width=width_.next();
            float predelay=predelay_.next(),decay=decay_.next(),damping=damping_.next(),size=size_.next();
            std::array<float,8> dry{},in{},wet{};
            for(unsigned c=0;c<count;++c)if(p[c]&&std::isfinite(p[c][i]))dry[c]=p[c][i];
            if(any_route_){
                const auto f=matrix_.next(dry.data(),count);
                center=bound(center+f.sum(0),40,8000,800);depth=bound(depth+f.sum(1),0,4,2);
                hz=bound(hz+f.sum(2),.05,10,.4);fb=bound(fb+f.sum(3),-.85,.85,.3);
                mix.wet=std::clamp(mix.wet+f.sum(4),0.f,1.f);
                mix.output_gain*=std::max(0.f,1+f.sum(5));
                decay=bound(decay+f.sum(6),.1,10,1.5);damping=bound(damping+f.sum(7),0,.99,.35);
                predelay=bound(predelay+f.sum(8),0,200,15);size=bound(size+f.sum(9),.5,1.9,1);
                width=bound(width+f.sum(10),0,1,1);
            }
            if(settings_.mode==SpaceMode::Bypass)continue;
            for(unsigned c=0;c<count;++c){in[c]=mix.input(dry[c]);if(!std::isfinite(in[c]))in[c]=0;in[c]=std::clamp(in[c],-128.f,128.f);}
            if(settings_.mode==SpaceMode::Phaser){
                auto coefficient=[&](double offset){
                    const float f=std::clamp(center*std::exp2(depth*float(std::sin(2*std::numbers::pi*(phase_+offset)))),20.f,.45f*rate_);
                    const float g=std::tan(float(std::numbers::pi)*f/rate_);return (g-1)/(g+1);
                };
                const float a=coefficient(0),b=width==0?a:coefficient(.5*width);
                for(unsigned c=0;c<count;++c){
                    auto& state=phaser_[c];const float k=c%2?b:a;
                    float x=std::clamp(in[c]+fb*state.previous,-128.f,128.f);
                    for(unsigned j=0;j<settings_.stages;++j){
                        const float y=k*x+state.z[j];
                        state.z[j]=x-k*y;
                        if(std::abs(state.z[j])<1e-20f)state.z[j]=0;
                        x=y;
                    }
                    state.previous=std::isfinite(x)?x:0;
                    wet[c]=state.previous;
                }
                phase_+=hz/double(rate_);phase_-=std::floor(phase_);
            }else{
                for(unsigned c=0;c<count;c+=2){
                    auto out=pairs_[c/2].next(in[c],c+1<count?in[c+1]:in[c],rate_,predelay,decay,damping,size,c+1<count?width:0);
                    wet[c]=out[0];if(c+1<count)wet[c+1]=out[1];
                }
            }
            for(unsigned c=0;c<count;++c)if(p[c])p[c][i]=mix.output(dry[c],in[c],wet[c]);
        }
    }
};
}
