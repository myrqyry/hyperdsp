#pragma once
#include "native_filter.hpp"
#include "dynamics.hpp"
namespace hyperdsp {
struct DynamicEqSettings {
    bool enabled=true;
    float frequency=1000,q=2,threshold=-24,ratio=4,range_db=-6,knee=3;
    float attack_ms=2,release_ms=80,wet=1,input_db=0,output_db=0;
};
// Constant-Q, unity-center bandpass. The same band drives the linked detector
// and is added to the unfiltered signal with gain-dependent scaling. This
// preserves exact neutral identity and avoids gain-dependent pole changes.
class DynamicEq {
    unsigned rate_=48000,ramp_=480;
    bool enabled_=true;
    std::array<SvfState,8> state_{};
    SvfCoefficients coeff_{};
    float cached_hz_=0,cached_q_=0,peak_=0,gain_db_=0;
    float attack_c_=0,release_c_=0,peak_c_=0;
    LinearRamp frequency_,q_,threshold_,ratio_,range_,knee_;
    MixModulator mixer_;
    static float bound(double x,double lo,double hi,double fallback){return MixSettings::bounded(x,lo,hi,fallback);}
    void configure(DynamicEqSettings s,bool reset){
        reset=reset||enabled_!=s.enabled;enabled_=s.enabled;
        auto change=[&](LinearRamp& r,float v){if(reset)r.reset(v);else r.set(v,ramp_);};
        change(frequency_,std::log2(bound(s.frequency,20,std::min(20000.,rate_*.45),1000)));
        change(q_,std::log2(bound(s.q,.25,12,2)));
        change(threshold_,bound(s.threshold,-80,0,-24));change(ratio_,bound(s.ratio,1,20,4));
        change(range_,bound(s.range_db,-24,12,-6));change(knee_,bound(s.knee,0,24,3));
        auto time=[&](float ms){return float(std::exp(-1000./(rate_*double(ms))));};
        attack_c_=time(bound(s.attack_ms,.1,200,2));release_c_=time(bound(s.release_ms,5,2000,80));
        peak_c_=time(10);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset){state_={};cached_hz_=cached_q_=peak_=gain_db_=0;mixer_.reset(rate_,mix);}
        else mixer_.set(mix,{});
    }
public:
    void reset(unsigned rate,DynamicEqSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(DynamicEqSettings s){configure(s,false);}
    float detector_peak()const{return peak_;}
    float current_frequency()const{return cached_hz_;}
    float current_q()const{return cached_q_;}
    float dynamic_gain_db()const{return gain_db_;}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!enabled_||!p||!channels||channels>8)return;
        for(unsigned j=0;j<frames;++j){
            auto mix=mixer_.next();
            float hz=std::min(std::exp2(frequency_.next()),.45f*rate_),q=std::exp2(q_.next());
            float threshold=threshold_.next(),ratio=ratio_.next(),range=range_.next(),knee=knee_.next();
            if(hz!=cached_hz_||q!=cached_q_){
                coeff_=SvfCoefficients::design(FilterMode::BandPass,rate_,hz,q,0);
                cached_hz_=hz;cached_q_=q;
            }
            std::array<float,8> input{},band{};float instant=0;
            for(unsigned c=0;c<channels;++c)if(p[c]){
                input[c]=std::isfinite(p[c][j])?mix.input(p[c][j]):0;
                band[c]=state_[c].run(input[c],coeff_);
                instant=std::max(instant,std::abs(band[c]));
            }
            peak_=std::max(instant,peak_*peak_c_);if(peak_<1e-20f)peak_=0;
            float level=20*std::log10(std::max(peak_,1e-8f));
            float magnitude=std::min(std::abs(range),-Dynamics::curve(level,threshold,ratio,knee,DynamicsMode::Compressor,0));
            float target=std::copysign(magnitude,range);
            float c=(target*gain_db_<0||std::abs(target)>std::abs(gain_db_))?attack_c_:release_c_;
            gain_db_=target+c*(gain_db_-target);
            if(std::abs(gain_db_-target)<1e-6f)gain_db_=target;
            float scale=std::pow(10.f,gain_db_/20)-1;
            for(unsigned c=0;c<channels;++c)if(p[c])
                p[c][j]=mix.output(std::isfinite(p[c][j])?p[c][j]:0,input[c],input[c]+scale*band[c]);
        }
    }
};
}
