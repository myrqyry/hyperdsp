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
    std::array<std::complex<float>,N/2> twiddle_{};
public:
    FFT(){
        for(unsigned i=0;i<N;++i){
            unsigned x=i,r=0;
            for(unsigned n=N;n>1;n>>=1){r=(r<<1)|(x&1);x>>=1;}
            reverse_[i]=r;
        }
        for(unsigned i=0;i<N/2;++i){
            const double a=-2*std::numbers::pi*i/N;
            twiddle_[i]={float(std::cos(a)),float(std::sin(a))};
        }
    }
    void transform(std::array<std::complex<float>,N>& x,bool inverse=false)const{
        for(unsigned i=0;i<N;++i)if(reverse_[i]>i)std::swap(x[i],x[reverse_[i]]);
        for(unsigned len=2;len<=N;len*=2){
            const unsigned half=len/2,step=N/len;
            for(unsigned start=0;start<N;start+=len)for(unsigned j=0;j<half;++j){
                const auto w=twiddle_[j*step];
                const auto a=x[start+j],b=x[start+j+half]*(inverse?std::conj(w):w);
                x[start+j]=a+b;x[start+j+half]=a-b;
            }
        }
        if(inverse)for(auto& v:x)v/=float(N);
    }
};
}
