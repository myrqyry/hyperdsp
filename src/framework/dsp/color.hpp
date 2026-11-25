#pragma once
#include "modulation_matrix.hpp"
#include <array>
#include <cmath>
#include <numbers>

namespace hyperdsp {
enum class ColorMode { Bypass, Saturation, SoftClip, HardClip, Fold, Crush };
struct ColorSettings {
    ColorMode mode=ColorMode::Saturation;
    float drive=0,bits=8,reduce=4,wet=1,input_db=0,output_db=0;
    unsigned oversample=1;
    std::array<RouteConfig,modulation_slots> routes{};
};
// Integer-factor, linear-phase FIR interpolation and decimation.
// Each FIR delays 12 base-rate samples; the dry branch delays 24.
class Color {
    static constexpr unsigned radius=12,history=2*radius+1,max_taps=4*2*radius+1;
    struct Channel {
        std::array<float,history> input{},dry{};
        std::array<float,max_taps> high{};
        unsigned base_pos=0,high_pos=0,hold_left=0;
        float held=0;
    };
    std::array<Channel,8> channels_{};
    std::array<float,max_taps> taps_{};
    unsigned factor_=1,tap_count_=1,rate_=48000,ramp_=480;
    ColorSettings settings_{};
    LinearRamp drive_,bits_,reduce_;
    MixModulator mixer_;
    ModulationMatrix matrix_;
    bool any_route_=false;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    void design(){
        taps_={};tap_count_=2*radius*factor_+1;
        const double cutoff=.45/factor_;
        double sum=0;
        for(unsigned i=0;i<tap_count_;++i){
            const double n=double(i)-radius*factor_;
            const double sinc=n==0?2*cutoff:std::sin(2*std::numbers::pi*cutoff*n)/(std::numbers::pi*n);
            const double a=2*std::numbers::pi*i/(tap_count_-1);
            const double window=.42-.5*std::cos(a)+.08*std::cos(2*a);
            taps_[i]=float(sinc*window);sum+=taps_[i];
        }
        for(unsigned i=0;i<tap_count_;++i)taps_[i]=float(taps_[i]/sum);
    }
    void configure(ColorSettings s,bool reset){
        if(unsigned(s.mode)>unsigned(ColorMode::Crush))s.mode=ColorMode::Bypass;
        s.drive=bound(s.drive,-24,36,0);s.bits=bound(s.bits,2,16,8);s.reduce=bound(s.reduce,1,64,4);
        if(s.oversample!=2&&s.oversample!=4)s.oversample=1;
        const unsigned factor=s.mode==ColorMode::Crush||s.mode==ColorMode::Bypass?1:s.oversample;
        if(reset||s.mode!=settings_.mode||factor!=factor_){
            channels_={};factor_=factor;if(factor_>1)design();
        }
        settings_=s;
        auto param=[&](LinearRamp& r,float v){if(reset)r.reset(v);else r.set(v,ramp_);};
        param(drive_,s.drive);param(bits_,s.bits);param(reduce_,s.reduce);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        any_route_=false;
        constexpr float limits[]={60,14,63,1,1};
        for(unsigned i=0;i<modulation_slots;++i){
            auto c=s.routes[i];
            if(c.destination<0||c.destination>=5)c.source=0;
            else c.amount=bound(c.amount,-limits[c.destination],limits[c.destination],0);
            matrix_.configure(i,rate_,c,reset);any_route_=any_route_||matrix_.active(i);
        }
    }
    float oversampled(Channel& ch,float in,float gain){
        float out=0;
        ch.input[ch.base_pos]=in;
        for(unsigned phase=0;phase<factor_;++phase){
            double interpolated=0;
            unsigned pos=ch.base_pos;
            for(unsigned tap=phase;tap<tap_count_;tap+=factor_){
                interpolated+=ch.input[pos]*taps_[tap];
                pos=pos?pos-1:history-1;
            }
            ch.high[ch.high_pos]=shape(settings_.mode,float(interpolated*factor_)*gain);
            if(phase==0){
                double sum=0;pos=ch.high_pos;
                for(unsigned tap=0;tap<tap_count_;++tap){
                    sum+=ch.high[pos]*taps_[tap];
                    pos=pos?pos-1:tap_count_-1;
                }
                out=float(sum);
            }
            if(++ch.high_pos==tap_count_)ch.high_pos=0;
        }
        return out;
    }
public:
    static float shape(ColorMode mode,float x){
        if(!std::isfinite(x))return 0;
        switch(mode){
        case ColorMode::Saturation:return std::tanh(x);
        case ColorMode::SoftClip:{x=std::clamp(x,-1.f,1.f);return x*(1.5f-.5f*x*x);}
        case ColorMode::HardClip:return std::clamp(x,-1.f,1.f);
        case ColorMode::Fold:{
            float v=std::fmod(x+1.f,4.f);if(v<0)v+=4.f;
            return v<=2?v-1:3-v;
        }
        default:return x;
        }
    }
    unsigned latency_samples()const{return factor_>1?2*radius:0;}
    void reset(unsigned rate,ColorSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(ColorSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    void process(float* const* p,unsigned count,unsigned frames){
        if(!p||count>channels_.size())return;
        for(unsigned i=0;i<frames;++i){
            float drive=drive_.next(),bits=bits_.next(),reduce=reduce_.next();
            auto mix=mixer_.next();
            if(any_route_){
                std::array<float,8> samples{};
                for(unsigned c=0;c<count;++c)if(p[c])samples[c]=p[c][i];
                const auto f=matrix_.next(samples.data(),count);
                drive=bound(drive+f.sum(0),-24,36,0);
                bits=bound(bits+f.sum(1),2,16,8);reduce=bound(reduce+f.sum(2),1,64,4);
                mix.wet=std::clamp(mix.wet+f.sum(3),0.f,1.f);
                mix.output_gain*=std::max(0.f,1+f.sum(4));
            }
            if(settings_.mode==ColorMode::Bypass)continue;
            const float gain=std::pow(10.f,drive/20.f);
            const bool crush=settings_.mode==ColorMode::Crush;
            const float scale=crush?float(1u<<(unsigned(std::round(bits))-1)):1.f;
            const unsigned period=crush?unsigned(std::round(reduce)):1;
            for(unsigned c=0;c<count;++c)if(p[c]){
                auto& ch=channels_[c];
                float dry=p[c][i];if(!std::isfinite(dry))dry=0;
                const float in=mix.input(dry);
                float aligned=in,wet=0;
                if(crush){
                    ch.hold_left=std::min(ch.hold_left,period);
                    if(!ch.hold_left){
                        ch.held=std::clamp(std::round(std::clamp(in*gain,-1.f,1.f)*scale),-scale,scale-1)/scale;
                        ch.hold_left=period;
                    }
                    --ch.hold_left;wet=ch.held;
                }else if(factor_>1){
                    wet=oversampled(ch,in,gain);
                    ch.dry[ch.base_pos]=in;
                    aligned=ch.dry[(ch.base_pos+1)%history];
                    ch.base_pos=(ch.base_pos+1)%history;
                }else wet=shape(settings_.mode,in*gain);
                p[c][i]=mix.output(dry,aligned,wet);
            }
        }
    }
};
}
