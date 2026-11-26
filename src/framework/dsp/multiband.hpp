#pragma once
#include "native_filter.hpp"
#include "dynamics.hpp"
namespace hyperdsp {
// Three-way Linkwitz-Riley fourth-order crossover. The low branch receives
// the upper crossover's matching second-order all-pass, so the complete sum
// has unity magnitude (not zero phase). No per-band channel crossfeed.
class ThreeBandCrossover {
    struct Pair {
        std::array<SvfState,2> low{},high{};
        std::array<float,2> split(float x,const SvfCoefficients& l,const SvfCoefficients& h){
            float a=x,b=x;
            for(unsigned i=0;i<2;++i){a=low[i].run(a,l);b=high[i].run(b,h);}
            return {a,b};
        }
    };
    struct Channel {Pair lower,upper;SvfState align;};
    std::array<Channel,8> state_{};
    SvfCoefficients ll_,lh_,ul_,uh_,phase_;
public:
    void reset(unsigned rate,float low,float high){
        state_={};constexpr double q=.7071067811865475244;
        ll_=SvfCoefficients::design(FilterMode::LowPass,rate,low,q,0);
        lh_=SvfCoefficients::design(FilterMode::HighPass,rate,low,q,0);
        ul_=SvfCoefficients::design(FilterMode::LowPass,rate,high,q,0);
        uh_=SvfCoefficients::design(FilterMode::HighPass,rate,high,q,0);
        phase_=SvfCoefficients::design(FilterMode::AllPass,rate,high,q,0);
    }
    std::array<float,3> split(unsigned c,float x){
        auto& s=state_[c];auto lower=s.lower.split(x,ll_,lh_);
        auto upper=s.upper.split(lower[1],ul_,uh_);
        return {s.align.run(lower[0],phase_),upper[0],upper[1]};
    }
};
struct MultibandSettings {
    bool enabled=true;
    float low_hz=250,high_hz=2500;
    std::array<float,3> threshold{-18,-18,-18},ratio{2,2,2},makeup{0,0,0};
    float attack=10,release=120,knee=6,wet=1,input_db=0,output_db=0;
};
class Multiband {
    static constexpr unsigned chunk=256;
    unsigned rate_=48000;
    float low_=0,high_=0;
    bool enabled_=true;
    ThreeBandCrossover crossover_;
    std::array<Dynamics,3> dynamics_{};
    MixModulator mixer_;
    std::array<std::array<std::array<float,chunk>,8>,3> bands_{};
    std::array<std::array<float,chunk>,8> reference_{};
    std::array<MixSettings,chunk> mixes_{};
    static float bound(double x,double a,double b,double fallback){return MixSettings::bounded(x,a,b,fallback);}
    void configure(MultibandSettings s,bool reset){
        float low=bound(s.low_hz,60,500,250),high=bound(s.high_hz,1000,std::min(8000.,rate_*.45),2500);
        // Topology changes are control-path edits, never graph macro targets.
        if(reset||low!=low_||high!=high_||enabled_!=s.enabled){
            crossover_.reset(rate_,low,high);reset=true;low_=low;high_=high;
        }
        enabled_=s.enabled;
        for(unsigned b=0;b<3;++b){
            DynamicsSettings d;d.threshold=s.threshold[b];d.ratio=s.ratio[b];d.makeup=s.makeup[b];
            d.attack=s.attack;d.release=s.release;d.knee=s.knee;
            if(reset)dynamics_[b].reset(rate_,d);else dynamics_[b].set(d);
        }
        auto mix=MixSettings::from(s.wet,s.input_db,s.output_db);
        if(reset)mixer_.reset(rate_,mix);else mixer_.set(mix,{});
    }
public:
    void reset(unsigned rate,MultibandSettings s){rate_=std::clamp(rate,8000u,192000u);configure(s,true);}
    void set(MultibandSettings s){configure(s,false);}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!enabled_||!p||!channels||channels>8)return;
        for(unsigned offset=0;offset<frames;){
            unsigned n=std::min(chunk,frames-offset);
            for(unsigned j=0;j<n;++j){
                auto mix=mixer_.next();mixes_[j]=mix;
                for(unsigned c=0;c<channels;++c){
                    float x=p[c]&&std::isfinite(p[c][offset+j])?mix.input(p[c][offset+j]):0;
                    auto split=crossover_.split(c,x);
                    reference_[c][j]=split[0]+split[1]+split[2];
                    for(unsigned b=0;b<3;++b)bands_[b][c][j]=split[b];
                }
            }
            for(unsigned b=0;b<3;++b){
                std::array<float*,8> buffers{};
                for(unsigned c=0;c<channels;++c)buffers[c]=bands_[b][c].data();
                dynamics_[b].process(buffers.data(),channels,n);
            }
            for(unsigned c=0;c<channels;++c)if(p[c])for(unsigned j=0;j<n;++j){
                float dry=reference_[c][j],wet=bands_[0][c][j]+bands_[1][c][j]+bands_[2][c][j];
                p[c][offset+j]=mixes_[j].output(p[c][offset+j],dry,wet);
            }
            offset+=n;
        }
    }
};
}
