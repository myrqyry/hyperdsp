#pragma once
#include "band_energy.hpp"
#include "modulation.hpp"

namespace hyperdsp {
// Source IDs intentionally match the native filter's persisted source IDs.
class ReactiveSource {
    BandEnergy band_;
    int source_=0,shape_=0;
    unsigned rate_=48000;
    double phase_=0,hz_=1,attack_=0,release_=0;
    float envelope_=0;
public:
    void configure(unsigned rate,int source,int shape,double hz,double attack,double release,double sensitivity){
        rate=std::clamp(rate,8000u,192000u);
        source=std::clamp(source,0,5);shape=std::clamp(shape,0,3);
        if(rate!=rate_||source!=source_){band_.reset();phase_=0;envelope_=0;}
        rate_=rate;source_=source;shape_=shape;
        hz_=MixSettings::bounded(hz,.05,20,1);
        attack_=std::exp(-1000./(rate_*MixSettings::bounded(attack,1,200,10)));
        release_=std::exp(-1000./(rate_*MixSettings::bounded(release,10,2000,100)));
        if(source_>=3)band_.configure(rate_,Band(source_-3),attack,release,sensitivity);
    }
    float next(const float* samples,unsigned channels){
        if(source_>=3)return band_.next(samples,channels);
        if(source_==2){
            float peak=0;
            for(unsigned i=0;i<channels;++i)if(std::isfinite(samples[i]))peak=std::max(peak,std::abs(samples[i]));
            const double a=peak>envelope_?attack_:release_;
            envelope_=float(a*envelope_+(1-a)*peak);
            if(envelope_<1e-20f)envelope_=0;
            return std::clamp(envelope_,0.f,1.f);
        }
        if(source_!=1)return 0;
        float wave=0;
        switch(shape_){
        case 0:wave=float(std::sin(2*std::numbers::pi*phase_));break;
        case 1:wave=float(1-4*std::abs(phase_-.5));break;
        case 2:wave=phase_<.5?1.f:-1.f;break;
        case 3:wave=float(2*phase_-1);break;
        }
        phase_+=hz_/rate_;phase_-=std::floor(phase_);
        return wave;
    }
};
}
