#pragma once
#include "color.hpp"
#include "spectral.hpp"
#include <map>
#include <string>
#include <string_view>
#include <span>
namespace hyperdsp {
template<class S> struct FloatParameter {
    const char* key;const char* name;double min,max,step;const char* unit;
    float S::*member;
};
inline constexpr FloatParameter<ColorSettings> color_parameters[]={
    {"drive","Drive",-24,36,.1," dB",&ColorSettings::drive},
    {"bits","Bit depth",2,16,1," bits",&ColorSettings::bits},
    {"reduce","Sample hold",1,64,1," samples",&ColorSettings::reduce},
    {"input_db","Input gain",-24,24,.1," dB",&ColorSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&ColorSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&ColorSettings::output_db}};
inline constexpr FloatParameter<SpectralSettings> spectral_parameters[]={
    {"threshold","Bin threshold",-100,0,.1," dBFS",&SpectralSettings::threshold},
    {"range","Bin attenuation range",0,100,.1," dB",&SpectralSettings::range},
    {"attack","Gate opening time",1,500,1," ms",&SpectralSettings::attack},
    {"release","Gate closing time",1,2000,1," ms",&SpectralSettings::release},
    {"hold","Freeze hold",0,1,1,"",&SpectralSettings::hold},
    {"smear_ms","Spectral memory time",10,2000,10," ms",&SpectralSettings::smear_ms},
    {"pitch","Pitch shift",-24,24,.1," semitones",&SpectralSettings::pitch},
    {"shift_hz","Frequency shift",-5000,5000,1," Hz",&SpectralSettings::shift_hz},
    {"input_db","Input gain",-24,24,.1," dB",&SpectralSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&SpectralSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&SpectralSettings::output_db}};
inline constexpr unsigned oversampling_choices[]={1,2,4};
inline bool valid_oversampling(double value){
    for(auto choice:oversampling_choices)if(value==choice)return true;
    return false;
}
inline constexpr const char* gain_key="gain_db";
inline constexpr double gain_min=-24,gain_max=24,gain_step=.1;
using Parameters=std::map<std::string,double,std::less<>>;
template<class S,size_t N> bool assign_parameter(S& s,const FloatParameter<S>(&spec)[N],std::string_view key,double value){
    for(auto& p:spec)if(key==p.key){
        if(!std::isfinite(value)||value<p.min||value>p.max)return false;
        s.*p.member=float(value);return true;
    }
    return false;
}
}
