#pragma once
#include "modulation.hpp"

namespace hyperdsp {
struct TransientSettings {
    bool enabled=true;
    float attack_db=0,sustain_db=0,attack_ms=25,sustain_ms=150,wet=1,input_db=0,output_db=0;
};
// Linked peak-envelope contrast. The fast envelope follows onsets immediately;
// the slower envelope tracks the body and decay. Only their difference drives
// gain, so a stationary constant input converges back to unity gain.
class Transient {
    unsigned rate_=48000,ramp_=480;
    bool enabled_=true;
    float fast_=0,slow_=0,fast_release_=0,slow_attack_=0,slow_release_=0;
    LinearRamp attack_,sustain_;
    MixModulator mixer_;
    static float coefficient(unsigned rate,float ms){return float(std::exp(-1000./(rate*double(ms))));}
    void configure(TransientSettings s,bool reset){
        reset=reset||s.enabled!=enabled_;
        enabled_=s.enabled;
        auto change=[&](LinearRamp& r,float v){if(reset)r.reset(v);else r.set(v,ramp_);};
        change(attack_,MixSettings::bounded(s.attack_db,-12,12,0));
        change(sustain_,MixSettings::bounded(s.sustain_db,-12,12,0));
        float attack=MixSettings::bounded(s.attack_ms,5,100,25);
        float sustain=MixSettings::bounded(s.sustain_ms,50,500,150);
        fast_release_=coefficient(rate_,10);
        slow_attack_=coefficient(rate_,attack);
        slow_release_=coefficient(rate_,sustain);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset){fast_=slow_=0;mixer_.reset(rate_,mix);}
        else mixer_.set(mix,{});
    }
public:
    void reset(unsigned rate,TransientSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(TransientSettings s){configure(s,false);}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!enabled_||!p||!channels||channels>8)return;
        for(unsigned j=0;j<frames;++j){
            auto mix=mixer_.next();
            float attack=attack_.next(),sustain=sustain_.next(),peak=0;
            for(unsigned c=0;c<channels;++c)if(p[c]&&std::isfinite(p[c][j]))
                peak=std::max(peak,std::abs(mix.input(p[c][j])));
            fast_=std::max(peak,fast_*fast_release_);
            float coefficient=fast_>slow_?slow_attack_:slow_release_;
            slow_=fast_+coefficient*(slow_-fast_);
            if(fast_<1e-20f)fast_=0;
            if(slow_<1e-20f)slow_=0;
            // Absolute floor avoids applying full gain to near-silence.
            float contrast=(fast_-slow_)/std::max({fast_,slow_,.001f});
            float db=attack*std::max(contrast,0.f)+sustain*std::max(-contrast,0.f);
            float gain=std::pow(10.f,db/20);
            for(unsigned c=0;c<channels;++c)if(p[c]){
                float dry=p[c][j];
                if(!std::isfinite(dry)){p[c][j]=0;continue;}
                float trimmed=mix.input(dry);
                p[c][j]=mix.output(dry,trimmed,trimmed*gain);
            }
        }
    }
};
}
