#include "framework/dsp/fft.hpp"
#include <array>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <numbers>

template<unsigned N> bool verify(){
    hyperdsp::FFT<N> fft;
    using Complex=std::complex<float>;
    double max_forward=0,max_inverse=0;
    for(unsigned pattern=0;pattern<5;++pattern){
        std::array<Complex,N> input{};
        uint32_t state=0x12345678u;
        auto random=[&]{state=state*1664525u+1013904223u;return float(state>>8)/8388608.f-1.f;};
        for(unsigned i=0;i<N;++i){
            if(pattern==0)input[i]={i==N/2?1.f:0.f,0};
            else if(pattern==1)input[i]={.25f,-.5f};
            else if(pattern==2)input[i]={std::cos(float(2*std::numbers::pi*i/N)),std::sin(float(2*std::numbers::pi*i/N))};
            else if(pattern==3)input[i]={random(),random()};
            else input[i]={random()*1e-7f,0};
        }
        for(bool inverse:{false,true}){
            auto out=input;fft.transform(out,inverse);
            for(unsigned k=0;k<N;++k){
                std::complex<double> expected{};
                for(unsigned i=0;i<N;++i){
                    const double a=(inverse?2:-2)*std::numbers::pi*k*i/N;
                    expected+=std::complex<double>(input[i])*std::complex<double>(std::cos(a),std::sin(a));
                }
                if(inverse)expected/=N;
                auto error=std::abs(std::complex<double>(out[k])-expected);
                if(!std::isfinite(error))return false;
                auto& worst=inverse?max_inverse:max_forward;
                worst=std::max(worst,error);
                if(error>(inverse?2e-7:1e-4))return false;
            }
        }
        auto roundtrip=input;fft.transform(roundtrip);fft.transform(roundtrip,true);
        for(unsigned i=0;i<N;++i)if(std::abs(roundtrip[i]-input[i])>5e-7f)return false;
    }
    std::printf("FFT_DFT n=%u forward_error=%g inverse_error=%g\n",N,max_forward,max_inverse);
    return true;
}
int main(){
    bool ok=verify<2>()&&verify<4>()&&verify<8>()&&verify<32>()&&verify<1024>();
    std::printf("FFT_PASS=%d\n",ok);return ok?0:1;
}
