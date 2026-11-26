#pragma once
#include "three_band.hpp"
#include "dynamics.hpp"
namespace hyperdsp {
struct MultibandSettings {
    bool enabled=true;
    float low_hz=250,high_hz=2500;
    std::array<float,3> threshold{-18,-18,-18},ratio{2,2,2},makeup{0,0,0};
    float attack=10,release=120,knee=6,wet=1,input_db=0,output_db=0;
};
class Multiband {
    unsigned rate_=48000;
    float low_=0,high_=0;
    bool enabled_=true;
    ThreeBandBank bank_;
    std::array<Dynamics,3> dynamics_{};
    static float bound(double x,double a,double b,double fallback){return MixSettings::bounded(x,a,b,fallback);}
    void configure(MultibandSettings s,bool reset){
        float low=bound(s.low_hz,60,500,250),high=bound(s.high_hz,1000,std::min(8000.,rate_*.45),2500);
        // Topology changes are control-path edits, never graph macro targets.
        if(reset||low!=low_||high!=high_||enabled_!=s.enabled){
            reset=true;low_=low;high_=high;
        }
        enabled_=s.enabled;
        for(unsigned b=0;b<3;++b){
            DynamicsSettings d;d.threshold=s.threshold[b];d.ratio=s.ratio[b];d.makeup=s.makeup[b];
            d.attack=s.attack;d.release=s.release;d.knee=s.knee;
            if(reset)dynamics_[b].reset(rate_,d);else dynamics_[b].set(d);
        }
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)bank_.reset(rate_,low_,high_,mix);else bank_.set_mix(mix);
    }
public:
    void reset(unsigned rate,MultibandSettings s){rate_=std::clamp(rate,8000u,192000u);configure(s,true);}
    void set(MultibandSettings s){configure(s,false);}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(enabled_)bank_.process(p,channels,frames,dynamics_);
    }
};
}
