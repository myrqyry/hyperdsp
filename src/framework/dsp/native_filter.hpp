#pragma once
#include "modulation.hpp"
#include <array>
#include <cstddef>
#include <numbers>

namespace hyperdsp {
enum class FilterMode { Bypass, LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf, Comb, AllPass };
enum class FilterAlignment { Resonant, Butterworth };
enum class ModSource { Off, Lfo, Envelope };
enum class ModTarget { Cutoff, Resonance, Gain, Mix, Output };
enum class WaveShape { Sine, Triangle, Square, Saw };
struct FilterSettings {
    FilterMode mode=FilterMode::LowPass;
    float frequency=1000,q=.70710678f,gain_db=0,comb_depth=1;
    unsigned stages=1;
    FilterAlignment alignment=FilterAlignment::Resonant;
    float wet=1,input_db=0,output_db=0;
    ModSource source=ModSource::Off;
    ModTarget target=ModTarget::Cutoff;
    WaveShape shape=WaveShape::Sine;
    float hz=1,depth=0,attack_ms=10,release_ms=100;
};

// Topology-preserving state-variable filter. Coefficients are shared across
// channels, while integrator state is independent. Continuous parameter sweeps
// do not interpolate direct-form pole coefficients.
struct SvfCoefficients {
    double a1=1,a2=0,a3=0,m0=1,m1=0,m2=0;
    static SvfCoefficients design(FilterMode mode,double rate,double hz,double q,double gain_db){
        double g=std::tan(std::numbers::pi*hz/rate),k=1./q;
        double m0=0,m1=0,m2=0;
        const double A=std::pow(10.,gain_db/40.);
        switch(mode){
        case FilterMode::LowPass: m2=1;break;
        case FilterMode::HighPass:m0=1;m1=-k;m2=-1;break;
        case FilterMode::BandPass:m1=k;break;
        case FilterMode::Notch:m0=1;m1=-k;break;
        case FilterMode::AllPass:m0=1;m1=-2*k;break;
        case FilterMode::Peak:k=1./(q*A);m0=1;m1=k*(A*A-1);break;
        case FilterMode::LowShelf:
            g/=std::sqrt(A);k=std::sqrt(2.);m0=1;m1=k*(A-1);m2=A*A-1;break;
        case FilterMode::HighShelf:
            g*=std::sqrt(A);k=std::sqrt(2.);m0=A*A;m1=k*(1-A)*A;m2=1-A*A;break;
        default:m0=1;break;
        }
        const double a1=1./(1.+g*(g+k)),a2=g*a1;
        return {a1,a2,g*a2,m0,m1,m2};
    }
};
struct SvfState {
    double ic1=0,ic2=0;
    float run(float x,const SvfCoefficients& c){
        const double v3=x-ic2;
        const double v1=c.a1*ic1+c.a2*v3;
        const double v2=ic2+c.a2*ic1+c.a3*v3;
        ic1=2*v1-ic1;ic2=2*v2-ic2;
        const double y=c.m0*x+c.m1*v1+c.m2*v2;
        if(!std::isfinite(y)||!std::isfinite(ic1)||!std::isfinite(ic2)){ic1=ic2=0;return x;}
        if(std::abs(ic1)<1e-25)ic1=0;
        if(std::abs(ic2)<1e-25)ic2=0;
        return float(y);
    }
};
class NativeFilter {
    static constexpr unsigned channels_=8,stages_=4,delay_size_=16384;
    struct Channel {
        std::array<SvfState,stages_> state{};
        std::array<float,delay_size_> delay{};
        unsigned pos=0;
        float comb(float x,float samples,float amount){
            unsigned n=unsigned(samples);
            float t=samples-float(n);
            float delayed=delay[(pos+delay_size_-n)%delay_size_]*(1-t)+delay[(pos+delay_size_-n-1)%delay_size_]*t;
            delay[pos]=x;pos=(pos+1)%delay_size_;
            return (x-amount*delayed)/(1+amount);
        }
    };
    std::array<Channel,channels_> voices_{};
    FilterSettings settings_{};
    LinearRamp frequency_,q_,gain_,depth_,comb_;
    MixModulator mixer_;
    unsigned rate_=48000,ramp_=480;
    double phase_=0;
    std::array<SvfCoefficients,stages_> cached_{};
    std::array<double,stages_> stage_q_{};
    FilterMode cached_mode_=FilterMode::Bypass;
    float cached_hz_=0,cached_q_=0,cached_gain_=0;
    float envelope_=0,attack_=0,release_=0,current_hz_=1000;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    float max_hz()const{return std::min(20000.f,.45f*rate_);}
    FilterSettings sanitize(FilterSettings s)const{
        if(unsigned(s.mode)>unsigned(FilterMode::AllPass))s.mode=FilterMode::Bypass;
        if(unsigned(s.source)>unsigned(ModSource::Envelope))s.source=ModSource::Off;
        if(unsigned(s.target)>unsigned(ModTarget::Output))s.target=ModTarget::Cutoff;
        if(unsigned(s.shape)>unsigned(WaveShape::Saw))s.shape=WaveShape::Sine;
        s.frequency=bound(s.frequency,20,max_hz(),std::min(1000.f,max_hz()));
        s.q=bound(s.q,.25,12,.70710678);
        s.gain_db=bound(s.gain_db,-24,24,0);
        s.comb_depth=bound(s.comb_depth,0,1,1);
        s.stages=std::clamp(s.stages,1u,4u);
        if(s.mode!=FilterMode::LowPass&&s.mode!=FilterMode::HighPass){
            s.stages=1;s.alignment=FilterAlignment::Resonant;
        }
        if(s.alignment!=FilterAlignment::Butterworth)s.alignment=FilterAlignment::Resonant;
        s.hz=bound(s.hz,.05,20,1);
        s.depth=bound(s.depth,0,1,0);
        s.attack_ms=bound(s.attack_ms,1,200,10);
        s.release_ms=bound(s.release_ms,10,2000,100);
        return s;
    }
    void set_envelope_coefficients(){
        attack_=float(std::exp(-1000./(rate_*double(settings_.attack_ms))));
        release_=float(std::exp(-1000./(rate_*double(settings_.release_ms))));
    }
    void prepare_alignment(){
        for(unsigned k=0;k<settings_.stages;++k)
            stage_q_[k]=1./(2.*std::cos(std::numbers::pi*(double(k)+.5)/(2.*settings_.stages)));
        cached_mode_=FilterMode::Bypass;
    }
    void clear_state(){
        for(auto& v:voices_){v.state={};v.delay.fill(0);v.pos=0;}
    }
    float source(float peak){
        const float coef=peak>envelope_?attack_:release_;
        envelope_=coef*envelope_+(1-coef)*peak;
        if(envelope_<1e-20f)envelope_=0;
        if(settings_.source==ModSource::Envelope)return std::clamp(envelope_,0.f,1.f);
        if(settings_.source!=ModSource::Lfo)return 0;
        float w=0;
        switch(settings_.shape){
        case WaveShape::Sine:w=float(std::sin(2*std::numbers::pi*phase_));break;
        case WaveShape::Triangle:w=float(1.-4.*std::abs(phase_-.5));break;
        case WaveShape::Square:w=phase_<.5?1.f:-1.f;break;
        case WaveShape::Saw:w=float(2.*phase_-1.);break;
        }
        phase_+=double(settings_.hz)/rate_;
        phase_-=std::floor(phase_);
        return w;
    }
public:
    void reset(unsigned rate,FilterSettings s){
        rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);
        settings_=sanitize(s);clear_state();phase_=0;envelope_=0;prepare_alignment();cached_hz_=0;
        frequency_.reset(std::log2(settings_.frequency));q_.reset(std::log2(settings_.q));
        gain_.reset(settings_.gain_db);depth_.reset(settings_.depth);
        comb_.reset(settings_.comb_depth);
        mixer_.reset(rate_,MixSettings::from(settings_.wet,settings_.input_db,settings_.output_db));
        set_envelope_coefficients();current_hz_=settings_.frequency;
    }
    void set(FilterSettings s){
        s=sanitize(s);
        const bool topology_changed=s.mode!=settings_.mode||s.stages!=settings_.stages||s.alignment!=settings_.alignment;
        if(topology_changed)clear_state();
        settings_=s;
        if(topology_changed)prepare_alignment();
        frequency_.set(std::log2(s.frequency),ramp_);q_.set(std::log2(s.q),ramp_);
        gain_.set(s.gain_db,ramp_);comb_.set(s.comb_depth,ramp_);depth_.set(s.depth,ramp_);
        mixer_.set(MixSettings::from(s.wet,s.input_db,s.output_db),{});
        set_envelope_coefficients();
    }
    float current_frequency()const{return current_hz_;}
    void process(float* const* buffers,unsigned channels,unsigned frames){
        if(!buffers||channels>channels_)return;
        for(unsigned i=0;i<frames;++i){
            float peak=0;
            for(unsigned c=0;c<channels;++c)if(buffers[c]&&std::isfinite(buffers[c][i]))
                peak=std::max(peak,std::abs(buffers[c][i]));
            const float mod=source(peak)*depth_.next();
            float freq=frequency_.next(),q=q_.next(),gain=gain_.next();
            auto mix=mixer_.next();
            switch(settings_.target){
            case ModTarget::Cutoff:freq+=4*mod;break;
            case ModTarget::Resonance:q+=2*mod;break;
            case ModTarget::Gain:gain+=12*mod;break;
            case ModTarget::Mix:mix.wet=std::clamp(mix.wet+mod,0.f,1.f);break;
            case ModTarget::Output:mix.output_gain*=1+mod;break;
            }
            current_hz_=std::clamp(std::exp2(freq),20.f,max_hz());
            const float resonance=settings_.alignment==FilterAlignment::Butterworth?0.f:std::clamp(std::exp2(q),.25f,12.f);
            gain=std::clamp(gain,-24.f,24.f);
            const auto mode=settings_.mode;
            const float amount=comb_.next();
            if(mode==FilterMode::Bypass)continue;
            if(mode!=FilterMode::Comb &&
                (cached_mode_!=mode||cached_hz_!=current_hz_||cached_q_!=resonance||cached_gain_!=gain)){
                for(unsigned k=0;k<settings_.stages;++k)
                    cached_[k]=SvfCoefficients::design(mode,rate_,current_hz_,
                        settings_.alignment==FilterAlignment::Butterworth?stage_q_[k]:resonance,gain);
                cached_mode_=mode;cached_hz_=current_hz_;cached_q_=resonance;cached_gain_=gain;
            }
            const float delay=std::clamp(float(rate_)/current_hz_,1.f,float(delay_size_-2));
            for(unsigned c=0;c<channels;++c)if(buffers[c]){
                const float dry=buffers[c][i];
                if(!std::isfinite(dry)){buffers[c][i]=0;continue;}
                const float in=mix.input(dry);
                float wet=in;
                if(mode==FilterMode::Comb)wet=voices_[c].comb(in,delay,amount);
                else for(unsigned k=0;k<settings_.stages;++k)wet=voices_[c].state[k].run(wet,cached_[k]);
                buffers[c][i]=mix.output(dry,in,wet);
            }
        }
    }
};
}
