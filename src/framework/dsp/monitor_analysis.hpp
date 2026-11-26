#pragma once
#include "monitor_spectrum.hpp"
#include "stereo.hpp"
namespace hyperdsp {
// Unweighted metrics over the exact displayed sample window, not the
// standalone filter's exponentially averaged diagnostics.
inline StereoMetrics window_stereo(const std::array<float,monitor_samples>& left,const std::array<float,monitor_samples>& right){
    double l=0,r=0,lr=0;
    for(unsigned i=0;i<monitor_samples;++i){
        double a=left[i],b=right[i];
        if(!std::isfinite(a)||!std::isfinite(b))continue;
        l+=a*a;r+=b*b;lr+=a*b;
    }
    StereoMetrics m;double total=l+r;
    if(total/monitor_samples<1e-12)return m;
    m.valid=true;m.correlation_valid=std::min(l,r)/monitor_samples>1e-12;
    if(m.correlation_valid)m.correlation=std::clamp(lr/std::sqrt(l*r),-1.,1.);
    double mid=std::clamp(.5+lr/total,0.,1.);
    m.mono_loss_db=10*std::log10(std::max(mid,1e-12));m.side_fraction=1-mid;
    return m;
}
struct SpectrumDifference {
    MonitorSpectrum::Bins db{};
    std::array<bool,monitor_samples/2+1> valid{};
};
inline SpectrumDifference spectrum_difference(const MonitorSpectrum::Bins& input,const MonitorSpectrum::Bins& output){
    SpectrumDifference result;
    for(unsigned i=1;i<input.size();++i){
        // A missing input component cannot establish a response. Also mask
        // outputs below the FFT amplitude floor rather than inventing a cut.
        result.valid[i]=std::isfinite(input[i])&&std::isfinite(output[i])&&input[i]>=-60&&output[i]>-119;
        if(result.valid[i])result.db[i]=output[i]-input[i];
    }
    return result;
}
}
