#pragma once
#include "color.hpp"
#include "spectral.hpp"
#include "native_filter.hpp"
#include "dynamics.hpp"
#include "time_effect.hpp"
#include "space.hpp"
#include "formant.hpp"
#include "multiband.hpp"
#include "stereo.hpp"
#include "transient.hpp"
#include <map>
#include <string>
#include <string_view>
#include <span>
namespace hyperdsp {
template<class S> struct FloatParameter {
    const char* key;const char* name;double min,max,step;const char* unit;
    float S::*member;
    std::array<float,3> S::*array=nullptr;
    unsigned index=0;
    float& get(S& s)const{return member?s.*member:(s.*array)[index];}
    float get(const S& s)const{return member?s.*member:(s.*array)[index];}
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
inline constexpr FloatParameter<FilterSettings> native_parameters[]={
    {"frequency","Frequency",20,20000,1," Hz",&FilterSettings::frequency},
    {"q","Resonance (Q)",.25,12,.01,"",&FilterSettings::q},
    {"gain_db","EQ gain",-24,24,.1," dB",&FilterSettings::gain_db},
    {"comb_depth","Comb depth",0,1,.01,"",&FilterSettings::comb_depth},
    {"input_db","Input gain",-24,24,.1," dB",&FilterSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&FilterSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&FilterSettings::output_db}};
inline constexpr FloatParameter<DynamicsSettings> dynamics_parameters[]={
    {"threshold","Threshold / ceiling",-80,0,.1," dBFS",&DynamicsSettings::threshold},
    {"ratio","Ratio",1,20,.1,":1",&DynamicsSettings::ratio},
    {"knee","Soft knee",0,24,.1," dB",&DynamicsSettings::knee},
    {"attack","Attack",.1,200,.1," ms",&DynamicsSettings::attack},
    {"release","Release",5,2000,1," ms",&DynamicsSettings::release},
    {"range","Attenuation range",0,100,.1," dB",&DynamicsSettings::range},
    {"hold","Gate hold",0,1000,1," ms",&DynamicsSettings::hold},
    {"hysteresis","Gate hysteresis",0,24,.1," dB",&DynamicsSettings::hysteresis},
    {"makeup","Makeup gain",-24,24,.1," dB",&DynamicsSettings::makeup},
    {"input_db","Input gain",-24,24,.1," dB",&DynamicsSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&DynamicsSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&DynamicsSettings::output_db}};
inline constexpr FloatParameter<TransientSettings> transient_parameters[]={
    {"attack_db","Attack gain",-12,12,.1," dB",&TransientSettings::attack_db},
    {"sustain_db","Sustain gain",-12,12,.1," dB",&TransientSettings::sustain_db},
    {"attack_ms","Attack contour",5,100,1," ms",&TransientSettings::attack_ms},
    {"sustain_ms","Sustain contour",50,500,1," ms",&TransientSettings::sustain_ms},
    {"input_db","Input gain",-24,24,.1," dB",&TransientSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&TransientSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&TransientSettings::output_db}};
inline constexpr FloatParameter<StereoSettings> stereo_parameters[]={
    {"width","Stereo width",0,2,.01," ×",&StereoSettings::width},
    {"mid_db","Mid gain",-24,24,.1," dB",&StereoSettings::mid_db},
    {"side_db","Side gain",-24,24,.1," dB",&StereoSettings::side_db},
    {"balance","Balance",-1,1,.01,"",&StereoSettings::balance},
    {"input_db","Input gain",-24,24,.1," dB",&StereoSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&StereoSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&StereoSettings::output_db}};
inline constexpr FloatParameter<MultibandSettings> multiband_crossovers[]={
    {"low_hz","Low crossover",60,500,1," Hz",&MultibandSettings::low_hz},
    {"high_hz","High crossover",1000,8000,1," Hz",&MultibandSettings::high_hz}};
inline constexpr FloatParameter<MultibandSettings> multiband_parameters[]={
    {"low_threshold","Low threshold",-80,0,.1," dBFS",nullptr,&MultibandSettings::threshold,0},
    {"low_ratio","Low ratio",1,20,.1,":1",nullptr,&MultibandSettings::ratio,0},
    {"low_makeup","Low makeup",-24,24,.1," dB",nullptr,&MultibandSettings::makeup,0},
    {"mid_threshold","Mid threshold",-80,0,.1," dBFS",nullptr,&MultibandSettings::threshold,1},
    {"mid_ratio","Mid ratio",1,20,.1,":1",nullptr,&MultibandSettings::ratio,1},
    {"mid_makeup","Mid makeup",-24,24,.1," dB",nullptr,&MultibandSettings::makeup,1},
    {"high_threshold","High threshold",-80,0,.1," dBFS",nullptr,&MultibandSettings::threshold,2},
    {"high_ratio","High ratio",1,20,.1,":1",nullptr,&MultibandSettings::ratio,2},
    {"high_makeup","High makeup",-24,24,.1," dB",nullptr,&MultibandSettings::makeup,2},
    {"attack","Attack",.1,200,.1," ms",&MultibandSettings::attack},
    {"release","Release",5,2000,1," ms",&MultibandSettings::release},
    {"knee","Soft knee",0,24,.1," dB",&MultibandSettings::knee},
    {"input_db","Input gain",-24,24,.1," dB",&MultibandSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&MultibandSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&MultibandSettings::output_db}};
inline constexpr FloatParameter<TimeSettings> time_parameters[]={
    {"delay_ms","Delay / center",.001,2000,.001," ms",&TimeSettings::delay_ms},
    {"feedback","Feedback",-.95,.95,.01,"",&TimeSettings::feedback},
    {"damping","Feedback damping",0,.99,.01,"",&TimeSettings::damping},
    {"depth_ms","Modulation depth",0,30,.1," ms",&TimeSettings::depth_ms},
    {"rate_hz","Modulation rate",.05,10,.05," Hz",&TimeSettings::rate_hz},
    {"stereo","Stereo phase spread",0,1,.01,"",&TimeSettings::stereo},
    {"input_db","Input gain",-24,24,.1," dB",&TimeSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&TimeSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&TimeSettings::output_db}};
inline constexpr FloatParameter<SpaceSettings> space_parameters[]={
    {"center","Center frequency",40,8000,1," Hz",&SpaceSettings::center},
    {"depth","Sweep depth",0,4,.05," oct",&SpaceSettings::depth},
    {"rate_hz","Sweep rate",.05,10,.05," Hz",&SpaceSettings::rate_hz},
    {"feedback","Phaser feedback",-.85,.85,.01,"",&SpaceSettings::feedback},
    {"predelay","Pre-delay",0,200,.1," ms",&SpaceSettings::predelay},
    {"decay","Nominal decay (RT60)",.1,10,.05," s",&SpaceSettings::decay},
    {"damping","Feedback damping",0,.99,.01,"",&SpaceSettings::damping},
    {"size","Room size",.5,1.9,.01,"",&SpaceSettings::size},
    {"width","Stereo width / phase spread",0,1,.01,"",&SpaceSettings::width},
    {"input_db","Input gain",-24,24,.1," dB",&SpaceSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&SpaceSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&SpaceSettings::output_db}};
inline constexpr FloatParameter<FormantSettings> formant_parameters[]={
    {"morph","Vowel morph",0,1,.01,"",&FormantSettings::morph},
    {"shift","Resonance shift",-24,24,.1," semitones",&FormantSettings::shift},
    {"width","Bandwidth scale",.25,4,.01," ×",&FormantSettings::width},
    {"input_db","Input gain",-24,24,.1," dB",&FormantSettings::input_db},
    {"mix","Wet / dry",0,1,.01,"",&FormantSettings::wet},
    {"output_db","Output gain",-24,24,.1," dB",&FormantSettings::output_db},
    {"f1","Formant 1 · Frequency",40,16000,1," Hz",nullptr,&FormantSettings::frequency,0},
    {"b1","Formant 1 · Bandwidth",20,4000,1," Hz",nullptr,&FormantSettings::bandwidth,0},
    {"g1","Formant 1 · Gain",-36,6,0.1," dB",nullptr,&FormantSettings::gain,0},
    {"f2","Formant 2 · Frequency",40,16000,1," Hz",nullptr,&FormantSettings::frequency,1},
    {"b2","Formant 2 · Bandwidth",20,4000,1," Hz",nullptr,&FormantSettings::bandwidth,1},
    {"g2","Formant 2 · Gain",-36,6,0.1," dB",nullptr,&FormantSettings::gain,1},
    {"f3","Formant 3 · Frequency",40,16000,1," Hz",nullptr,&FormantSettings::frequency,2},
    {"b3","Formant 3 · Bandwidth",20,4000,1," Hz",nullptr,&FormantSettings::bandwidth,2},
    {"g3","Formant 3 · Gain",-36,6,0.1," dB",nullptr,&FormantSettings::gain,2}};
inline constexpr double short_delay_default(int mode){return mode==3?15:mode==4?2:8;}
inline constexpr double short_delay_max(int mode){return mode==4?10:40;}
struct MicSettings {float vibe=.5f,resonance=.707f;};
inline constexpr FloatParameter<MicSettings> mic_parameters[]={
    {"vibe","Spectral sweep",0,1,.01,"",&MicSettings::vibe},
    {"resonance","Sweep resonance",.1,5,.1,"",&MicSettings::resonance}};
inline constexpr unsigned filter_stages[]={1,2,3,4},alignment_choices[]={0,1},space_stages[]={2,4,6,8,10,12},vowel_choices[]={0,1,2,3,4};
template<size_t N> bool valid_choice(double value,const unsigned(&choices)[N]){
    for(auto v:choices)if(value==v)return true;
    return false;
}
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
        p.get(s)=float(value);return true;
    }
    return false;
}
}
