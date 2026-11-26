#pragma once
#include "graph_monitor.hpp"
#include "fft.hpp"
namespace hyperdsp {
class MonitorSpectrum {
    FFT<monitor_samples> fft_;
    std::array<float,monitor_samples> window_{};
    float sum_=0;
public:
    using Bins=std::array<float,monitor_samples/2+1>;
    MonitorSpectrum(){
        for(unsigned i=0;i<monitor_samples;++i){
            window_[i]=float(.5-.5*std::cos(2*std::numbers::pi*i/monitor_samples));sum_+=window_[i];
        }
    }
    Bins analyze(const std::array<float,monitor_samples>& samples)const{
        std::array<std::complex<float>,monitor_samples> data{};
        for(unsigned i=0;i<monitor_samples;++i)data[i]=samples[i]*window_[i];
        fft_.transform(data);Bins out{};
        for(unsigned i=0;i<out.size();++i){
            float amplitude=std::abs(data[i])*(i==0||i==monitor_samples/2?1:2)/sum_;
            out[i]=20*std::log10(std::max(amplitude,1e-6f));
        }
        return out;
    }
};
}
