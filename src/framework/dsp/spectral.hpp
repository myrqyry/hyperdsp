#pragma once
#include "fft.hpp"
#include "modulation_matrix.hpp"
#include <vector>
namespace hyperdsp {
enum class SpectralMode { Bypass, Gate, Freeze, Smear };
struct SpectralSettings {
    SpectralMode mode=SpectralMode::Gate;
    float threshold=-45,range=60,attack=10,release=100,hold=0,smear_ms=200;
    float wet=1,input_db=0,output_db=0;
    std::array<RouteConfig,modulation_slots> routes{};
};
class Spectral {
    static constexpr unsigned n=1024,hop=n/4,bins=n/2+1;
    using Complex=std::complex<float>;
    struct Channel {
        std::array<float,n> input{},output{};
        std::array<float,bins> gain{},magnitude{};
        std::array<Complex,bins> previous{},phase{},advance{},frozen{};
        bool captured=false;
        Channel(){gain.fill(1);phase.fill({1,0});advance.fill({1,0});}
    };
    FFT<n> fft_;
    std::array<float,n> window_{};
    std::array<Complex,n> spectrum_{};
    std::vector<Channel> channels_;
    SpectralSettings settings_{};
    ModulationMatrix matrix_;
    MixModulator mixer_;
    LinearRamp threshold_,range_,hold_,smear_;
    unsigned rate_=48000,pos_=0,tick_=0,ramp_=480;
    bool any_route_=false;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    static Complex unit(Complex x,Complex fallback){
        const float m=std::abs(x);return m>1e-12f&&std::isfinite(m)?x/m:fallback;
    }
    void frame(Channel& ch,float threshold,float range,float hold,float smear){
        for(unsigned j=0;j<n;++j)spectrum_[j]={ch.input[(pos_+j)&(n-1)]*window_[j],0};
        fft_.transform(spectrum_);
        const auto mode=settings_.mode;
        const float threshold_amp=mode==SpectralMode::Gate?std::pow(10.f,threshold/20):0;
        const float floor_gain=mode==SpectralMode::Gate?std::pow(10.f,-range/20):0;
        const float attack=mode==SpectralMode::Gate?std::exp(-1000.f*hop/(rate_*settings_.attack)):0;
        const float release=mode==SpectralMode::Gate?std::exp(-1000.f*hop/(rate_*settings_.release)):0;
        const float memory=mode==SpectralMode::Smear?std::exp(-1000.f*hop/(rate_*smear)):0;
        const bool capture=mode==SpectralMode::Freeze&&hold>=.5f&&!ch.captured;
        for(unsigned k=0;k<bins;++k){
            const auto x=spectrum_[k];
            const float mag=std::abs(x);
            // Coherent Hann amplitude calibration for a bin-centered sine;
            // DC and Nyquist are not doubled.
            if(mode==SpectralMode::Gate){
                const float amp=mag*((k==0||k==n/2)?2.f/n:4.f/n);
                const float target=amp>=threshold_amp?1.f:floor_gain;
                const float coeff=target>ch.gain[k]?attack:release;
                ch.gain[k]=target+coeff*(ch.gain[k]-target);
                spectrum_[k]=x*ch.gain[k];
            }else{
                // Quarter-window hops imply the nominal bin advance i^k.
                constexpr Complex ideal[]={{1,0},{0,1},{-1,0},{0,-1}};
                const auto expected=ideal[k&3];
                const auto prev=ch.previous[k];
                const auto advance=mag>1e-8f&&std::abs(prev)>1e-8f?unit(x*std::conj(prev),expected):expected;
                if(mode==SpectralMode::Freeze){
                    if(capture){ch.frozen[k]=x;ch.advance[k]=advance;ch.magnitude[k]=mag;}
                    else if(hold>=.5f&&ch.captured)
                        ch.frozen[k]=unit(ch.frozen[k]*ch.advance[k],expected)*ch.magnitude[k];
                    if(hold>=.5f)spectrum_[k]=ch.frozen[k];
                }else if(mode==SpectralMode::Smear){
                    ch.magnitude[k]=memory*ch.magnitude[k]+(1-memory)*mag;
                    if(mag>1e-6f){
                        ch.phase[k]=unit(x,ch.phase[k]);
                        ch.advance[k]=advance;
                    }else ch.phase[k]=unit(ch.phase[k]*ch.advance[k],expected);
                    spectrum_[k]=ch.phase[k]*ch.magnitude[k];
                }
                ch.previous[k]=x;
            }
        }
        ch.captured=mode==SpectralMode::Freeze&&hold>=.5f;
        spectrum_[0]={spectrum_[0].real(),0};spectrum_[n/2]={spectrum_[n/2].real(),0};
        for(unsigned k=1;k<n/2;++k)spectrum_[n-k]=std::conj(spectrum_[k]);
        fft_.transform(spectrum_,true);
        // Four overlaps of a periodic Hann squared sum to 3/2.
        for(unsigned j=0;j<n;++j)ch.output[(pos_+j)&(n-1)]+=spectrum_[j].real()*window_[j]*(2.f/3);
    }
    void configure(SpectralSettings s,bool reset){
        if(unsigned(s.mode)>3)s.mode=SpectralMode::Bypass;
        s.threshold=bound(s.threshold,-100,0,-45);s.range=bound(s.range,0,100,60);
        s.attack=bound(s.attack,1,500,10);s.release=bound(s.release,1,2000,100);
        s.hold=bound(s.hold,0,1,0);s.smear_ms=bound(s.smear_ms,10,2000,200);
        const bool changed=reset||s.mode!=settings_.mode;
        if(changed){for(auto& ch:channels_)ch=Channel{};pos_=tick_=0;}
        settings_=s;
        auto param=[&](LinearRamp& r,float v){if(changed)r.reset(v);else r.set(v,ramp_);};
        param(threshold_,s.threshold);param(range_,s.range);param(hold_,s.hold);param(smear_,s.smear_ms);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        any_route_=false;
        constexpr float limits[]={100,100,1,2000,1,1};
        for(unsigned i=0;i<modulation_slots;++i){
            auto c=s.routes[i];
            if(c.destination<0||c.destination>=6)c.source=0;
            else c.amount=bound(c.amount,-limits[c.destination],limits[c.destination],0);
            matrix_.configure(i,rate_,c,reset);any_route_|=matrix_.active(i);
        }
    }
public:
    Spectral(){
        for(unsigned i=0;i<n;++i)window_[i]=float(.5-.5*std::cos(2*std::numbers::pi*i/n));
    }
    unsigned latency_samples()const{return settings_.mode==SpectralMode::Bypass?0:n;}
    void reset(unsigned rate,unsigned count,SpectralSettings s){
        rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);
        channels_.resize(std::clamp(count,1u,8u));configure(s,true);
    }
    void set(SpectralSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    void process(float* const* p,unsigned count,unsigned frames){
        if(!p||count!=channels_.size()||!count)return;
        for(unsigned i=0;i<frames;++i){
            auto mix=mixer_.next();
            float threshold=threshold_.next(),range=range_.next(),hold=hold_.next(),smear=smear_.next();
            std::array<float,8> dry{};
            for(unsigned c=0;c<count;++c)if(p[c]&&std::isfinite(p[c][i]))dry[c]=p[c][i];
            if(any_route_){
                const auto f=matrix_.next(dry.data(),count);
                threshold=bound(threshold+f.sum(0),-100,0,-45);range=bound(range+f.sum(1),0,100,60);
                hold=bound(hold+f.sum(2),0,1,0);smear=bound(smear+f.sum(3),10,2000,200);
                mix.wet=std::clamp(mix.wet+f.sum(4),0.f,1.f);mix.output_gain*=std::max(0.f,1+f.sum(5));
            }
            if(settings_.mode==SpectralMode::Bypass)continue;
            for(unsigned c=0;c<count;++c){
                auto& ch=channels_[c];
                const float aligned=ch.input[pos_],wet=ch.output[pos_];
                ch.output[pos_]=0;ch.input[pos_]=std::clamp(mix.input(dry[c]),-128.f,128.f);
                if(p[c])p[c][i]=mix.output(aligned,aligned,wet);
            }
            pos_=(pos_+1)&(n-1);
            if(++tick_==hop){
                tick_=0;for(auto& ch:channels_)frame(ch,threshold,range,hold,smear);
            }
        }
    }
};
}
