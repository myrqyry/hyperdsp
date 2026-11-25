#pragma once
#include "native_filter.hpp"
#include <array>
namespace hyperdsp {
enum class FormantMode { Bypass, Vowel, Custom };
struct FormantSettings {
    FormantMode mode=FormantMode::Vowel;
    unsigned from=0,to=2;
    float morph=0,shift=0,width=1,wet=1,input_db=0,output_db=0;
    std::array<float,3> frequency{800,1150,2900},bandwidth{90,120,180},gain{0,-6,-12};
    std::array<RouteConfig,modulation_slots> routes{};
};
// Illustrative vowel targets, not speaker-specific measured formants.
struct VowelTarget {std::array<float,3> frequency,bandwidth,gain;};
inline constexpr std::array<VowelTarget,5> vowel_targets{{
    {{800,1150,2900},{90,120,180},{0,-6,-12}},
    {{500,1900,2600},{80,130,180},{0,-6,-12}},
    {{270,2290,3010},{60,120,180},{0,-6,-12}},
    {{450,800,2830},{80,100,180},{0,-6,-12}},
    {{300,870,2240},{60,100,160},{0,-6,-12}}
}};
class Formant {
    FormantSettings settings_{};
    std::array<std::array<SvfState,3>,8> state_{};
    std::array<SvfCoefficients,3> coeff_{};
    std::array<float,3> weights_{},last_log_f_{},last_log_bw_{},last_offset_{},last_gain_{};
    std::array<bool,3> coefficients_ready_{};
    float last_width_=0;
    std::array<LinearRamp,3> f_,bw_,gain_,to_f_,to_bw_,to_gain_;
    LinearRamp morph_,shift_,width_;
    MixModulator mixer_;
    ModulationMatrix matrix_;
    unsigned rate_=48000,ramp_=480;
    bool any_route_=false;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    void configure(FormantSettings s,bool reset){
        if(unsigned(s.mode)>2)s.mode=FormantMode::Bypass;
        s.from=std::min(s.from,4u);s.to=std::min(s.to,4u);
        s.morph=bound(s.morph,0,1,0);s.shift=bound(s.shift,-24,24,0);s.width=bound(s.width,.25,4,1);
        const bool topology=reset||s.mode!=settings_.mode;
        if(topology){state_={};coefficients_ready_={};}
        auto param=[&](LinearRamp& r,float v){if(topology)r.reset(v);else r.set(v,ramp_);};
        const auto& a=vowel_targets[s.from];const auto& b=vowel_targets[s.to];
        for(unsigned j=0;j<3;++j){
            s.frequency[j]=bound(s.frequency[j],40,16000,800);
            s.bandwidth[j]=bound(s.bandwidth[j],20,4000,100);
            s.gain[j]=bound(s.gain[j],-36,6,0);
            const bool vowel=s.mode==FormantMode::Vowel;
            param(f_[j],std::log2(vowel?a.frequency[j]:s.frequency[j]));
            param(bw_[j],std::log2(vowel?a.bandwidth[j]:s.bandwidth[j]));
            param(gain_[j],vowel?a.gain[j]:s.gain[j]);
            param(to_f_[j],std::log2(vowel?b.frequency[j]:s.frequency[j]));
            param(to_bw_[j],std::log2(vowel?b.bandwidth[j]:s.bandwidth[j]));
            param(to_gain_[j],vowel?b.gain[j]:s.gain[j]);
        }
        param(morph_,s.morph);param(shift_,s.shift);param(width_,s.width);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        settings_=s;any_route_=false;
        constexpr float limits[]={1,24,3.75f,1,1,8000,8000,8000,42,42,42};
        for(unsigned j=0;j<modulation_slots;++j){
            auto c=s.routes[j];
            if(c.destination<0||c.destination>=11)c.source=0;
            else c.amount=bound(c.amount,-limits[c.destination],limits[c.destination],0);
            matrix_.configure(j,rate_,c,reset);any_route_|=matrix_.active(j);
        }
    }
public:
    void reset(unsigned rate,FormantSettings s){rate_=std::clamp(rate,8000u,192000u);ramp_=std::max(1u,rate_/100);configure(s,true);}
    void set(FormantSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    void process(float* const* p,unsigned count,unsigned frames){
        if(!p||count>state_.size())return;
        for(unsigned i=0;i<frames;++i){
            auto mix=mixer_.next();
            float morph=morph_.next(),shift=shift_.next(),width=width_.next();
            ModulationFrame mod{};
            if(any_route_){
                std::array<float,8> samples{};
                for(unsigned c=0;c<count;++c)if(p[c]&&std::isfinite(p[c][i]))samples[c]=p[c][i];
                mod=matrix_.next(samples.data(),count);
                morph=bound(morph+mod.sum(0),0,1,0);shift=bound(shift+mod.sum(1),-24,24,0);
                width=bound(width+mod.sum(2),.25,4,1);
                mix.wet=std::clamp(mix.wet+mod.sum(3),0.f,1.f);
                mix.output_gain*=std::max(0.f,1+mod.sum(4));
            }
            for(unsigned j=0;j<3;++j){
                const float a=f_[j].next(),b=to_f_[j].next(),ab=bw_[j].next(),bb=to_bw_[j].next();
                const float ag=gain_[j].next(),bg=to_gain_[j].next();
                if(settings_.mode==FormantMode::Bypass)continue;
                const float log_f=std::lerp(a,b,morph)+shift/12;
                const float log_bw=std::lerp(ab,bb,morph)+shift/12;
                const float offset=any_route_?mod.sum(5+j):0;
                const float g=bound(std::lerp(ag,bg,morph)+(any_route_?mod.sum(8+j):0),-36,6,0);
                if(!coefficients_ready_[j]||log_f!=last_log_f_[j]||log_bw!=last_log_bw_[j]||
                    offset!=last_offset_[j]||width!=last_width_){
                    const float f=std::clamp(std::exp2(log_f)+offset,40.f,.45f*rate_);
                    const float bw=std::exp2(log_bw)*width;
                    coeff_[j]=SvfCoefficients::design(FilterMode::BandPass,rate_,f,std::clamp(f/bw,.25f,80.f),0);
                    last_log_f_[j]=log_f;last_log_bw_[j]=log_bw;last_offset_[j]=offset;coefficients_ready_[j]=true;
                }
                if(g!=last_gain_[j]||weights_[j]==0){
                    weights_[j]=std::pow(10.f,g/20)/3;last_gain_[j]=g;
                }
            }
            last_width_=width;
            if(settings_.mode==FormantMode::Bypass)continue;
            for(unsigned c=0;c<count;++c)if(p[c]){
                const float dry=std::isfinite(p[c][i])?p[c][i]:0;
                const float in=std::clamp(mix.input(dry),-128.f,128.f);
                float wet=0;
                for(unsigned j=0;j<3;++j)wet+=weights_[j]*state_[c][j].run(in,coeff_[j]);
                p[c][i]=mix.output(dry,in,wet);
            }
        }
    }
};
}
