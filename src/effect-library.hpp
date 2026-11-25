#pragma once
#include "ladspa-catalog.hpp"
#include <span>
#include <set>
namespace hyperdsp {
struct NativeMode {int value;const char* name;const char* category;const char* description;};
struct NativeFamily {const char* source;const char* name;const char* mode_key;std::span<const NativeMode> modes;};
inline constexpr NativeMode mic_modes[]={
    {0,"None (bypass)","Utility","Unprocessed audio"},
    {1,"Dynamic: low cut and presence","Microphone","Procedural dynamic microphone coloration"},
    {2,"Condenser: extended high end","Microphone","Procedural condenser coloration"},
    {3,"Ribbon: warm top end","Microphone","Procedural ribbon coloration"},
    {4,"Telephone: narrow band","Microphone","Narrow-band telephone coloration"},
    {5,"Vintage radio: band-limited","Microphone","Band-limited radio coloration"},
    {6,"Tape: saturation and roll-off","Microphone","Tape-inspired coloration"}};
inline constexpr NativeMode filter_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Low-pass","EQ / Filter","Attenuate high frequencies"},
    {2,"High-pass","EQ / Filter","Attenuate low frequencies"},
    {3,"Band-pass","EQ / Filter","Isolate a frequency band"},
    {4,"Notch","EQ / Filter","Reject a narrow frequency band"},
    {5,"Peaking EQ","EQ / Filter","Boost or cut around a center frequency"},
    {6,"Low shelf","EQ / Filter","Shape low-frequency gain"},
    {7,"High shelf","EQ / Filter","Shape high-frequency gain"},
    {8,"Comb (feedforward notch)","EQ / Filter","Periodic cancellation notches"},
    {9,"All-pass","EQ / Filter","Phase shaping"}};
inline constexpr NativeMode dynamics_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Compressor","Dynamics","Reduce dynamic range"},
    {2,"Downward expander","Dynamics","Attenuate quieter signals"},
    {3,"Noise gate","Dynamics","Gate with hold and hysteresis"},
    {4,"Sample-peak limiter","Dynamics","Immediate wet-branch sample attenuation; not true peak"}};
inline constexpr NativeMode color_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Tanh saturation","Distortion","Gradual nonlinear saturation"},
    {2,"Cubic soft clipping","Distortion","Rounded clipping"},
    {3,"Hard clipping","Distortion","Bounded hard clipping"},
    {4,"Wavefolding","Distortion","Fold waveform peaks"},
    {5,"Bitcrusher / sample hold","Distortion","Quantization and sample repetition"}};
inline constexpr NativeMode time_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Echo / delay","Delay / Reverb","Fractional delay with feedback"},
    {2,"Ping-pong delay","Delay / Reverb","Cross-channel feedback repeats"},
    {3,"Chorus (two taps)","Modulation","Two modulated delay taps"},
    {4,"Flanger","Modulation","Short moving delay with feedback"},
    {5,"Vibrato","Modulation","Moving delay for pitch modulation"}};
inline constexpr NativeMode space_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Phaser","Modulation","Swept all-pass cascade"},
    {2,"Algorithmic reverb","Delay / Reverb","Damped feedback delay network"}};
inline constexpr NativeMode spectral_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Spectral gate","Spectral","Attenuate quiet frequency bins"},
    {2,"Spectral freeze","Spectral","Sustain captured spectral magnitudes and phase advance"},
    {3,"Spectral smear","Spectral","Temporal magnitude memory"},
    {4,"Pitch shift","Pitch","Multiply spectral partial frequencies"},
    {5,"Frequency shift","Pitch","Add a fixed frequency offset"}};
inline constexpr NativeMode formant_modes[]={
    {0,"Bypass","Utility","Unprocessed audio"},
    {1,"Vowel morph","Resonators","Interpolate illustrative vowel resonances"},
    {2,"Custom resonator bank","Resonators","Three independently tunable bands"}};
inline constexpr NativeFamily native_families[]={
    {"mic_modeler_filter","HyperDSP Microphone DSP","mic_type",mic_modes},
    {"hyperdsp_native_filter","HyperDSP Filter","mode",filter_modes},
    {"hyperdsp_dynamics_filter","HyperDSP Dynamics","mode",dynamics_modes},
    {"hyperdsp_color_filter","HyperDSP Color","mode",color_modes},
    {"hyperdsp_time_filter","HyperDSP Time","mode",time_modes},
    {"hyperdsp_space_filter","HyperDSP Space","mode",space_modes},
    {"hyperdsp_spectral_filter","HyperDSP Spectral","mode",spectral_modes},
    {"hyperdsp_formant_filter","HyperDSP Formant","mode",formant_modes}};
inline void add_native_modes(obs_property_t* property,std::span<const NativeMode> modes){
    for(auto& m:modes)obs_property_list_add_int(property,m.name,m.value);
}
struct EffectEntry {
    std::string id,name,category,description,format,source,mode_key,path,label,maker;
    int mode=0,channels=0;
    bool compatible=false;
    std::vector<ControlHint> controls;
    bool controls_complete=false;
};
// Metadata-only inventory. No native libraries opened by this function.
inline std::vector<EffectEntry> effect_library(const Catalog& catalog,unsigned channels){
    std::vector<EffectEntry> entries;std::set<std::string> ids;
    for(auto& family:native_families)for(auto& m:family.modes)if(m.value){
        EffectEntry e;e.id=std::string("native:")+family.source+":"+std::to_string(m.value);
        e.name=m.name;e.category=m.category;e.description=m.description;e.format="native";
        e.source=family.source;e.mode_key=family.mode_key;e.mode=m.value;e.maker="HyperDSP";
        e.compatible=channels>=1&&channels<=8;
        entries.push_back(std::move(e));
    }
    for(auto& plugin:catalog){
        EffectEntry e;e.id="ladspa:"+plugin.id();
        if(!ids.insert(e.id).second)continue;
        e.name=plugin.name;e.category=plugin.category.empty()?"Other":plugin.category;
        e.format="ladspa";e.source="hyperdsp_ladspa_filter";e.path=plugin.path;e.label=plugin.label;
        e.maker=plugin.maker;e.channels=plugin.channels;e.controls=plugin.controls;e.controls_complete=plugin.controls_complete;
        e.description="Scanned descriptor; processing not individually qualified";
        std::error_code ec;
        e.compatible=channels>=1&&channels<=8&&(plugin.channels==1||channels==2)&&std::filesystem::is_regular_file(e.path,ec);
        entries.push_back(std::move(e));
    }
    return entries;
}
inline std::vector<EffectEntry> search_effects(const std::vector<EffectEntry>& entries,const std::string& query,const std::string& format=""){
    std::vector<EffectEntry> result;
    for(auto& e:entries){
        if(!format.empty()&&format!=e.format)continue;
        if(match_words(e.name+" "+e.category+" "+e.description+" "+e.format+" "+e.source+" "+e.maker+" "+e.label+" "+
            std::filesystem::path(e.path).filename().string()+" "+(e.channels==1?"mono":e.channels==2?"stereo":""),query))
            result.push_back(e);
    }
    return result;
}
}
