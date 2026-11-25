#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace hyperdsp {
enum class Band { Bass, Mid, Treble };
// Linked attack/release energy detector (RMS-style, not a fixed-window RMS meter); filtering is per channel, energy is combined without
// phase cancellation. Two Butterworth sections per edge improve isolation.
class BandEnergy {
    struct Coeff {
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        static Coeff edge(double rate,double hz,bool high){
            const double g=std::tan(std::numbers::pi*hz/rate),g2=g*g;
            const double a=1/(1+std::sqrt(2.)*g+g2);
            const double b=high?a:g2*a;
            return {b,high?-2*b:2*b,b,2*(g2-1)*a,(1-std::sqrt(2.)*g+g2)*a};
        }
    };
    struct State {
        double z1=0,z2=0;
        double run(double x,const Coeff& c){
            const double y=c.b0*x+z1;
            z1=c.b1*x-c.a1*y+z2;z2=c.b2*x-c.a2*y;
            if(!std::isfinite(y)||!std::isfinite(z1)||!std::isfinite(z2)){
                z1=z2=0;return 0;
            }
            if(std::abs(z1)<1e-25)z1=0;
            if(std::abs(z2)<1e-25)z2=0;
            return y;
        }
    };
    std::array<std::array<State,4>,8> states_{};
    std::array<Coeff,4> coeffs_{};
    unsigned count_=0,rate_=0;
    Band band_=Band::Bass;
    double attack_=0,release_=0,power_=0,gain_=1;
    static double bounded(double v,double lo,double hi,double fallback){
        return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
    }
public:
    void reset(){states_={};power_=0;}
    void configure(unsigned rate,Band band,double attack_ms,double release_ms,double db){
        rate=std::clamp(rate,8000u,192000u);
        if(unsigned(band)>2)band=Band::Bass;
        if(rate!=rate_||band!=band_){
            reset();rate_=rate;band_=band;count_=0;
            auto add=[&](double hz,bool high){
                const auto c=Coeff::edge(rate,hz,high);
                coeffs_[count_++]=c;coeffs_[count_++]=c;
            };
            if(band!=Band::Bass)add(band==Band::Mid?250:2500,true);
            if(band!=Band::Treble)add(band==Band::Bass?250:2500,false);
        }
        attack_=std::exp(-1000/(rate*bounded(attack_ms,1,200,10)));
        release_=std::exp(-1000/(rate*bounded(release_ms,10,2000,100)));
        gain_=std::pow(10.,bounded(db,-24,24,0)/20);
    }
    float next(const float* samples,unsigned channels){
        double peak_power=0;
        for(unsigned ch=0;ch<std::min(channels,8u);++ch){
            double x=samples&&std::isfinite(samples[ch])?samples[ch]:0;
            for(unsigned i=0;i<count_;++i)x=states_[ch][i].run(x,coeffs_[i]);
            peak_power=std::max(peak_power,x*x);
        }
        const double a=peak_power>power_?attack_:release_;
        power_=a*power_+(1-a)*peak_power;
        if(!std::isfinite(power_)||power_<1e-30)power_=0;
        return float(std::clamp(std::sqrt(power_)*gain_,0.,1.));
    }
};
}
