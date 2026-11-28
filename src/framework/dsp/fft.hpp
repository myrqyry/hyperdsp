#pragma once
#include <array>
#include <complex>
#include <cmath>
#include <numbers>
#include <algorithm>
namespace hyperdsp {
template<unsigned N> class FFT {
    static_assert(N>=2&&(N&(N-1))==0);
    std::array<unsigned,N> reverse_{};
    // Contiguous stage-local weights allow the compiler to vectorize the
    // finite-input butterflies without complex-library exceptional paths.
    std::array<std::complex<float>,N-1> twiddle_{};
    template<bool Inverse> void butterflies(std::array<std::complex<float>,N>& x)const{
        for(unsigned i=0;i<N;++i)if(reverse_[i]>i)std::swap(x[i],x[reverse_[i]]);
        for(unsigned half=1;half<N;half*=2){
            const auto* weights=twiddle_.data()+half-1;
            for(unsigned start=0;start<N;start+=2*half){
                auto* low=x.data()+start;
                auto* high=low+half;
                for(unsigned j=0;j<half;++j){
                    const float wr=weights[j].real(),wi=Inverse?-weights[j].imag():weights[j].imag();
                    const float ar=low[j].real(),ai=low[j].imag();
                    const float br=high[j].real(),bi=high[j].imag();
                    const float tr=br*wr-bi*wi,ti=br*wi+bi*wr;
                    low[j]={ar+tr,ai+ti};high[j]={ar-tr,ai-ti};
                }
            }
        }
        if constexpr(Inverse)for(auto& v:x)v={v.real()/float(N),v.imag()/float(N)};
    }
public:
    FFT(){
        for(unsigned i=0;i<N;++i){
            unsigned x=i,r=0;
            for(unsigned n=N;n>1;n>>=1){r=(r<<1)|(x&1);x>>=1;}
            reverse_[i]=r;
        }
        for(unsigned half=1;half<N;half*=2)for(unsigned j=0;j<half;++j){
            const double a=-2*std::numbers::pi*j/(2*half);
            twiddle_[half-1+j]={float(std::cos(a)),float(std::sin(a))};
        }
    }
    // Audio/analysis callers supply bounded finite inputs. No global
    // fast-math flags, runtime dispatch, heap allocation or ISA requirement.
    void transform(std::array<std::complex<float>,N>& x,bool inverse=false)const{
        if(inverse)butterflies<true>(x);else butterflies<false>(x);
    }
};
}
