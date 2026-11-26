#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace hyperdsp {
enum class TelemetryKind : uint32_t {None,Dynamics,DynamicEq,Multiband};
// End-of-host-block detector and smoothed gain state, before makeup or mix.
struct NodeTelemetry {
    TelemetryKind kind=TelemetryKind::None;
    uint32_t mode=0;
    std::array<float,3> peak{},gain_db{};
    float frequency=0,q=0,low_hz=0,high_hz=0;
};
using NodeTelemetryArray=std::array<NodeTelemetry,16>;
// Frozen wet EQ response, not a time-varying transfer-function estimate.
// H = 1 + (A-1)*B, where B is the unity-center TPT bandpass.
inline double dynamic_eq_frozen_db(const NodeTelemetry& n,unsigned rate,double hz){
    if(n.kind!=TelemetryKind::DynamicEq||!rate||n.frequency<=0||n.frequency>=rate*.5||n.q<=0||hz<=0||hz>=rate*.5)return 0;
    const double r=std::tan(std::numbers::pi*hz/rate)/std::tan(std::numbers::pi*n.frequency/rate);
    const double b=(1-r*r)*n.q, a=std::pow(10.,n.gain_db[0]/20.);
    return 10*std::log10(std::max(1e-12,(b*b+a*a*r*r)/(b*b+r*r)));
}
}
