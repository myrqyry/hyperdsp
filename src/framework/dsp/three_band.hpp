#pragma once
#include "native_filter.hpp"
#include "modulation.hpp"
#include <array>
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
// Shared preallocated processing bank for any linked three-band effect.
class ThreeBandBank {
    static constexpr unsigned chunk=256;
    ThreeBandCrossover crossover_;
    MixModulator mixer_;
    std::array<std::array<std::array<float,chunk>,8>,3> bands_{};
    std::array<std::array<float,chunk>,8> reference_{};
    std::array<MixSettings,chunk> mixes_{};
public:
    void reset(unsigned rate,float low,float high,MixSettings mix){
        crossover_.reset(rate,low,high);mixer_.reset(rate,mix);
    }
    void set_mix(MixSettings mix){mixer_.set(mix,{});}
    template<class Engine>
    void process(float*const* p,unsigned channels,unsigned frames,std::array<Engine,3>& processors){
        if(!p||!channels||channels>8)return;
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
                processors[b].process(buffers.data(),channels,n);
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
