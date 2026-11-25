#pragma once
#include "effect_mix.hpp"
#include <cmath>
#include <numbers>

namespace hyperdsp {
struct LfoSettings {
    int target=0; // 0 off, 1 wet/dry, 2 output amplitude
    double hz=1.,depth=0.;
};
class LinearRamp {
    float current_=0,target_=0,step_=0;
    unsigned left_=0;
public:
    void reset(float v){current_=target_=v;step_=0;left_=0;}
    void set(float v,unsigned samples){
        if(v==target_)return;
        if(!samples){reset(v);return;}
        target_=v;left_=samples;step_=(target_-current_)/float(samples);
    }
    float next(){
        if(left_){current_+=step_;if(!--left_)current_=target_;}
        return current_;
    }
};
class MixModulator {
    LinearRamp wet_,in_,out_,wet_depth_,out_depth_;
    unsigned ramp_=480,rate_=48000;
    double phase_=0,hz_=1;
    static float depth(LfoSettings s,int target){
        return s.target==target?MixSettings::bounded(s.depth,0.,1.,0.):0.f;
    }
public:
    void reset(unsigned rate,MixSettings m,LfoSettings l={}){
        rate_=std::max(1u,rate);ramp_=std::max(1u,rate_/100);
        phase_=0;hz_=MixSettings::bounded(l.hz,.05,20.,1.);
        wet_.reset(m.wet);in_.reset(m.input_gain);out_.reset(m.output_gain);
        wet_depth_.reset(depth(l,1));out_depth_.reset(depth(l,2));
    }
    void set(MixSettings m,LfoSettings l){
        wet_.set(m.wet,ramp_);in_.set(m.input_gain,ramp_);out_.set(m.output_gain,ramp_);
        wet_depth_.set(depth(l,1),ramp_);out_depth_.set(depth(l,2),ramp_);
        hz_=MixSettings::bounded(l.hz,.05,20.,1.);
    }
    MixSettings next(){
        MixSettings m{wet_.next(),in_.next(),out_.next()};
        const float wd=wet_depth_.next(),od=out_depth_.next();
        if(wd!=0.f||od!=0.f){
            const float wave=float(std::sin(2.*std::numbers::pi*phase_));
            m.wet=std::clamp(m.wet+wd*wave,0.f,1.f);
            m.output_gain*=1.f+od*wave;
            phase_+=hz_/double(rate_);
            phase_-=std::floor(phase_);
        }
        return m;
    }
};
}
