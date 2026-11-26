#pragma once
#include "three_band.hpp"
#include "transient.hpp"
namespace hyperdsp {
struct BandTransientSettings {
    bool enabled=true;
    float low_hz=250,high_hz=2500;
    std::array<float,3> attack_db{},sustain_db{};
    float attack_ms=25,sustain_ms=150,wet=1,input_db=0,output_db=0;
};
class BandTransient {
    unsigned rate_=48000;
    float low_=0,high_=0;
    bool enabled_=true;
    ThreeBandBank bank_;
    std::array<Transient,3> bands_;
    void configure(BandTransientSettings s,bool reset){
        float low=MixSettings::bounded(s.low_hz,60,500,250);
        float high=MixSettings::bounded(s.high_hz,1000,std::min(8000.,rate_*.45),2500);
        reset=reset||low!=low_||high!=high_||s.enabled!=enabled_;
        low_=low;high_=high;enabled_=s.enabled;
        for(unsigned i=0;i<3;++i){
            TransientSettings t;t.attack_db=s.attack_db[i];t.sustain_db=s.sustain_db[i];
            t.attack_ms=s.attack_ms;t.sustain_ms=s.sustain_ms;
            if(reset)bands_[i].reset(rate_,t);else bands_[i].set(t);
        }
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)bank_.reset(rate_,low_,high_,mix);else bank_.set_mix(mix);
    }
public:
    void reset(unsigned rate,BandTransientSettings s){rate_=std::clamp(rate,8000u,192000u);configure(s,true);}
    void set(BandTransientSettings s){configure(s,false);}
    float contour(unsigned band)const{return bands_[band].contour();}
    float contour_gain_db(unsigned band)const{return bands_[band].contour_gain_db();}
    float low_frequency()const{return low_;}
    float high_frequency()const{return high_;}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(enabled_)bank_.process(p,channels,frames,bands_);
    }
};
}
