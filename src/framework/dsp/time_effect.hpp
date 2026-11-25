#pragma once
#include "modulation_matrix.hpp"
#include "fractional_delay.hpp"
#include <array>
#include <vector>
#include <cmath>
#include <numbers>

namespace hyperdsp {
enum class TimeMode { Bypass, Echo, PingPong, Chorus, Flanger, Vibrato };
struct TimeSettings {
    TimeMode mode=TimeMode::Echo;
    float delay_ms=250,feedback=.35f,damping=0,depth_ms=2,rate_hz=.5f,stereo=1;
    float wet=.5f,input_db=0,output_db=0;
    std::array<RouteConfig,modulation_slots> routes{};
};
class TimeEffect {
    TimeSettings settings_{};
    FractionalDelay line_;
    MixModulator mixer_;
    ModulationMatrix matrix_;
    LinearRamp delay_,feedback_,depth_,speed_,damping_,stereo_;
    std::array<float,8> damp_state_{};
    unsigned rate_=48000,channels_=0,ramp_=480;
    double phase_=0;
    bool any_route_=false;
    static float bound(double v,double lo,double hi,double fallback){return MixSettings::bounded(v,lo,hi,fallback);}
    static bool moving(TimeMode m){return m==TimeMode::Chorus||m==TimeMode::Flanger||m==TimeMode::Vibrato;}
    void configure(TimeSettings s,bool reset){
        if(unsigned(s.mode)>5)s.mode=TimeMode::Bypass;
        s.delay_ms=bound(s.delay_ms,.001,2000,250);
        if(moving(s.mode)){
            const float maximum=s.mode==TimeMode::Flanger?10.f:40.f;
            if(s.delay_ms>maximum)s.delay_ms=s.mode==TimeMode::Flanger?2.f:s.mode==TimeMode::Chorus?15.f:8.f;
        }
        s.feedback=bound(s.feedback,-.95,.95,.35);
        s.depth_ms=bound(s.depth_ms,0,30,2);s.rate_hz=bound(s.rate_hz,.05,10,.5);
        s.damping=bound(s.damping,0,.99,0);s.stereo=bound(s.stereo,0,1,1);
        const bool topology=reset||s.mode!=settings_.mode;
        if(topology){line_.clear();damp_state_={};phase_=0;}
        settings_=s;
        auto param=[&](LinearRamp& r,float v){if(topology)r.reset(v);else r.set(v,ramp_);};
        param(delay_,s.delay_ms);param(feedback_,s.feedback);param(depth_,s.depth_ms);
        param(speed_,s.rate_hz);param(damping_,s.damping);param(stereo_,s.stereo);
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
        any_route_=false;
        constexpr float limits[]={2000,.95f,30,10,1,1};
        for(unsigned i=0;i<modulation_slots;++i){
            auto c=s.routes[i];
            if(c.destination<0||c.destination>=6)c.source=0;
            else c.amount=bound(c.amount,-limits[c.destination],limits[c.destination],0);
            matrix_.configure(i,rate_,c,reset);any_route_=any_route_||matrix_.active(i);
        }
    }
public:
    void reset(unsigned rate,unsigned channels,TimeSettings s){
        rate_=std::clamp(rate,8000u,192000u);channels_=std::clamp(channels,1u,8u);
        ramp_=std::max(1u,rate_/100);line_.prepare(rate_,channels_);configure(s,true);
    }
    void set(TimeSettings s){configure(s,false);}
    void external_values(const std::array<float,modulation_slots>& v){matrix_.external_values(v);}
    void process(float* const* p,unsigned count,unsigned frames){
        if(!p||!count||count!=channels_)return;
        const auto mode=settings_.mode;
        const bool modulated=moving(mode);
        for(unsigned i=0;i<frames;++i){
            auto mix=mixer_.next();
            float delay=delay_.next(),fb=feedback_.next(),depth=depth_.next(),hz=speed_.next();
            const float damping=damping_.next(),width=stereo_.next();
            std::array<float,8> dry{},wet{},trimmed{};
            for(unsigned c=0;c<count;++c)if(p[c]&&std::isfinite(p[c][i]))dry[c]=p[c][i];
            if(any_route_){
                const auto f=matrix_.next(dry.data(),count);
                delay=bound(delay+f.sum(0),.001,2000,250);fb=bound(fb+f.sum(1),-.95,.95,0);
                depth=bound(depth+f.sum(2),0,30,2);hz=bound(hz+f.sum(3),.05,10,.5);
                mix.wet=std::clamp(mix.wet+f.sum(4),0.f,1.f);
                mix.output_gain*=std::max(0.f,1+f.sum(5));
            }
            if(mode==TimeMode::Bypass)continue;
            // Short-delay modes use an explicitly bounded center and excursion.
            if(modulated){
                const float maximum=mode==TimeMode::Flanger?10.f:40.f;
                delay=std::clamp(delay,.1f,maximum);
                depth=std::min(depth,std::min(delay-.02f,maximum-delay));
            }
            if(mode==TimeMode::Vibrato||mode==TimeMode::Chorus)fb=0;
            const float base=delay*rate_*.001f,excursion=depth*rate_*.001f;
            const float wave=modulated?float(std::sin(2*std::numbers::pi*phase_)):0;
            const float odd=modulated&&width!=0?float(std::sin(2*std::numbers::pi*(phase_+.5*width))):wave;
            const float second=mode==TimeMode::Chorus?float(std::cos(2*std::numbers::pi*phase_)):0;
            const float second_odd=mode==TimeMode::Chorus&&width!=0?float(std::cos(2*std::numbers::pi*(phase_+.5*width))):second;
            for(unsigned c=0;c<count;++c){
                trimmed[c]=mix.input(dry[c]);
                if(!std::isfinite(trimmed[c]))trimmed[c]=0;
                wet[c]=line_.read(c,base+excursion*(c%2?odd:wave));
                if(mode==TimeMode::Chorus)
                    wet[c]=.5f*(wet[c]+line_.read(c,base+excursion*(c%2?second_odd:second)));
                damp_state_[c]=(1-damping)*wet[c]+damping*damp_state_[c];
                if(std::abs(damp_state_[c])<1e-20f)damp_state_[c]=0;
            }
            for(unsigned c=0;c<count;++c){
                // Ping-pong crosses adjacent channel pairs; unpaired mono
                // remains an ordinary feedback delay.
                const unsigned partner=mode==TimeMode::PingPong&&(c^1u)<count?(c^1u):c;
                const float value=trimmed[c]+fb*damp_state_[partner];
                line_.write(c,std::isfinite(value)?std::clamp(value,-128.f,128.f):0.f);
                if(p[c])p[c][i]=mix.output(dry[c],trimmed[c],wet[c]);
            }
            line_.advance();
            if(modulated){phase_+=hz/double(rate_);phase_-=std::floor(phase_);}
        }
    }
};
}
