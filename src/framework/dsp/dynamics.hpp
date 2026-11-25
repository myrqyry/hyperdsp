#pragma once
#include "modulation_matrix.hpp"
#include <array>
#include <cmath>

namespace hyperdsp {
enum class DynamicsMode { Bypass, Compressor, Expander, Gate, Limiter };
struct DynamicsSettings {
    DynamicsMode mode=DynamicsMode::Compressor;
    float threshold=-18,ratio=4,knee=6,attack=10,release=100,range=60;
    float hold=20,hysteresis=3,makeup=0,wet=1,input_db=0,output_db=0;
    std::array<RouteConfig,modulation_slots> routes{};
};
// Linked peak dynamics. No allocations, lookahead or intersample peak claims.
class Dynamics {
    DynamicsSettings settings_{};
    ModulationMatrix matrix_;
    MixModulator mixer_;
    LinearRamp threshold_,ratio_,knee_,range_,makeup_;
    unsigned rate_=48000,ramp_=480,hold_left_=0,hold_samples_=960;
    float peak_=0,gain_db_=0,attack_c_=0,release_c_=0;
    bool gate_open_=false,any_route_=false;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    static float db(float v){return 20.f*std::log10(std::max(v,1e-8f));}
    static float amp(float v){return std::pow(10.f,v/20.f);}
    void configure(DynamicsSettings s,bool reset){
        if(unsigned(s.mode)>4)s.mode=DynamicsMode::Bypass;
        s.threshold=bound(s.threshold,-80,0,-18);s.ratio=bound(s.ratio,1,20,4);
        s.knee=bound(s.knee,0,24,6);s.range=bound(s.range,0,100,60);
        s.makeup=bound(s.makeup,-24,24,0);
        s.attack=bound(s.attack,.1,200,10);s.release=bound(s.release,5,2000,100);
        s.hold=bound(s.hold,0,1000,20);s.hysteresis=bound(s.hysteresis,0,24,3);
        if(reset||s.mode!=settings_.mode){peak_=0;gain_db_=s.mode==DynamicsMode::Gate?-s.range:0;gate_open_=false;hold_left_=0;}
        settings_=s;
        auto param=[&](LinearRamp& r,float v){if(reset)r.reset(v);else r.set(v,ramp_);};
        param(threshold_,s.threshold);param(ratio_,s.ratio);param(knee_,s.knee);param(range_,s.range);param(makeup_,s.makeup);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        attack_c_=float(std::exp(-1000./(rate_*double(s.attack))));
        release_c_=float(std::exp(-1000./(rate_*double(s.release))));
        hold_samples_=unsigned(s.hold*.001*rate_);hold_left_=std::min(hold_left_,hold_samples_);
        any_route_=false;
        for(unsigned i=0;i<modulation_slots;++i){
            auto c=s.routes[i];
            if(c.destination<0||c.destination>5)c.source=0;
            c.amount=bound(c.amount,c.destination==0?-80:c.destination==1?-19:c.destination==2?-24:c.destination==3?-100:-1,
                c.destination==0?80:c.destination==1?19:c.destination==2?24:c.destination==3?100:1,0);
            matrix_.configure(i,rate_,c,reset);any_route_=any_route_||matrix_.active(i);
        }
    }
public:
    // Static gain change in dB, before temporal smoothing.
    static float curve(float level,float threshold,float ratio,float knee,DynamicsMode mode,float range){
        const float delta=level-threshold;
        if(mode==DynamicsMode::Compressor){
            const float slope=1/ratio-1;
            if(knee>0&&std::abs(delta)<knee*.5f){const float x=delta+knee*.5f;return slope*x*x/(2*knee);}
            return slope*std::max(0.f,delta);
        }
        if(mode==DynamicsMode::Expander){
            const float slope=ratio-1;
            float value;
            if(knee>0&&std::abs(delta)<knee*.5f){const float x=delta-knee*.5f;value=-slope*x*x/(2*knee);}
            else value=slope*std::min(0.f,delta);
            return std::max(-range,value);
        }
        if(mode==DynamicsMode::Limiter)return -std::max(0.f,delta);
        return 0;
    }
    void reset(unsigned rate,DynamicsSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(DynamicsSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    float gain_reduction_db()const{return std::max(0.f,-gain_db_);}
    void process(float* const* p,unsigned channels,unsigned frames){
        if(!p||channels>8)return;
        for(unsigned i=0;i<frames;++i){
            auto mix=mixer_.next();
            float threshold=threshold_.next(),ratio=ratio_.next(),knee=knee_.next(),range=range_.next(),makeup=makeup_.next();
            std::array<float,8> samples{};
            if(any_route_){
                for(unsigned c=0;c<channels;++c)if(p[c])samples[c]=p[c][i];
                const auto f=matrix_.next(samples.data(),channels);
                threshold=bound(threshold+f.sum(0),-80,0,-18);ratio=bound(ratio+f.sum(1),1,20,4);
                makeup=bound(makeup+f.sum(2),-24,24,0);range=bound(range+f.sum(3),0,100,60);
                mix.wet=std::clamp(mix.wet+f.sum(4),0.f,1.f);
                mix.output_gain*=std::max(0.f,1+f.sum(5));
            }
            if(settings_.mode==DynamicsMode::Bypass)continue;
            float instant=0;
            for(unsigned c=0;c<channels;++c)if(p[c]){
                const float x=p[c][i];
                if(std::isfinite(x))instant=std::max(instant,std::abs(mix.input(x)));
            }
            // Peak detector has instant rise and exponential release.
            peak_=std::max(instant,peak_*release_c_);
            if(peak_<1e-20f)peak_=0;
            const float level=db(peak_);
            float target=0;
            if(settings_.mode==DynamicsMode::Gate){
                if(level>=threshold){gate_open_=true;hold_left_=hold_samples_;}
                else if(gate_open_){
                    if(level>=threshold-settings_.hysteresis)hold_left_=hold_samples_;
                    else if(hold_left_)--hold_left_;
                    else gate_open_=false;
                }
                target=gate_open_?0:-range;
            }else target=curve(level,threshold,ratio,knee,settings_.mode,range);
            const bool closing=target<gain_db_;
            if(settings_.mode==DynamicsMode::Limiter&&closing)gain_db_=target;
            else{
                const bool upward=settings_.mode==DynamicsMode::Gate||settings_.mode==DynamicsMode::Expander;
                const float coeff=(upward?!closing:closing)?attack_c_:release_c_;
                gain_db_=target+coeff*(gain_db_-target);
            }
            if(std::abs(gain_db_-target)<1e-6f)gain_db_=target;
            float g=amp(gain_db_+makeup);
            // The limiter ceiling is for the wet branch, after makeup.
            // Dry mixing and output trim are intentionally outside the ceiling.
            if(settings_.mode==DynamicsMode::Limiter&&instant>0)g=std::min(g,amp(threshold)/instant);
            for(unsigned c=0;c<channels;++c)if(p[c]){
                const float dry=p[c][i];
                if(!std::isfinite(dry)){p[c][i]=0;continue;}
                const float in=mix.input(dry);
                p[c][i]=mix.output(dry,in,in*g);
            }
        }
    }
};
}
