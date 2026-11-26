#pragma once
#include "modulation.hpp"
#include <array>
namespace hyperdsp {
struct StereoSettings {
    bool enabled=true;
    float width=1,mid_db=0,side_db=0,balance=0,wet=1,input_db=0,output_db=0;
};
class Stereo {
    bool enabled_=true;
    unsigned ramp_=480,rate_=48000;
    LinearRamp mid_,side_,width_,balance_;
    MixModulator mixer_;
    void configure(StereoSettings s,bool reset){
        auto change=[&](LinearRamp& r,float v){if(reset)r.reset(v);else r.set(v,ramp_);};
        enabled_=s.enabled;
        change(mid_,std::pow(10.f,MixSettings::bounded(s.mid_db,-24,24,0)/20));
        change(side_,std::pow(10.f,MixSettings::bounded(s.side_db,-24,24,0)/20));
        change(width_,MixSettings::bounded(s.width,0,2,1));
        change(balance_,MixSettings::bounded(s.balance,-1,1,0));
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
    }
public:
    void reset(unsigned rate,StereoSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(StereoSettings s){configure(s,enabled_!=s.enabled);}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!enabled_||channels!=2||!p||!p[0]||!p[1])return;
        for(unsigned j=0;j<frames;++j){
            auto mix=mixer_.next();
            float l=mix.input(p[0][j]),r=mix.input(p[1][j]);
            float m=(l+r)*.5f*mid_.next(),s=(l-r)*.5f*side_.next()*width_.next();
            float b=balance_.next();
            p[0][j]=mix.output(p[0][j],l,(m+s)*(1-std::max(b,0.f)));
            p[1][j]=mix.output(p[1][j],r,(m-s)*(1+std::min(b,0.f)));
        }
    }
};
struct StereoMetrics {
    bool valid=false,correlation_valid=false;
    double correlation=0,mono_loss_db=0,side_fraction=0;
};
// Exponentially weighted stereo power, 250 ms time constant. Fold-down
// compares power of (L+R)/2 with mean per-channel power, not loudness.
class StereoAnalysis {
    double a_=0,l_=0,r_=0,lr_=0;
public:
    void reset(unsigned rate){a_=std::exp(-1./(.25*std::max(1u,rate)));l_=r_=lr_=0;}
    void next(float l,float r){
        if(!std::isfinite(l)||!std::isfinite(r)){l=r=0;}
        l_=a_*l_+(1-a_)*double(l)*l;r_=a_*r_+(1-a_)*double(r)*r;lr_=a_*lr_+(1-a_)*double(l)*r;
    }
    StereoMetrics snapshot()const{
        StereoMetrics m;double total=l_+r_;
        if(total<1e-12)return m;
        m.valid=true;m.correlation_valid=l_>1e-12&&r_>1e-12;
        if(m.correlation_valid)m.correlation=std::clamp(lr_/std::sqrt(l_*r_),-1.,1.);
        double mid=std::clamp(.5+lr_/total,0.,1.);
        m.mono_loss_db=10*std::log10(std::max(mid,1e-12));m.side_fraction=1-mid;
        return m;
    }
};
}
