#include "effect-library.hpp"
#include "framework/obs/parameter_properties.hpp"
/**
 * @file mic-filter.cpp
 * @brief OBS filter for verified, in-place microphone-style DSP coloration.
 *
 * No model IO and no inference in this filter. Custom ONNX processing is
 * quarantined separately until its actual audio/model contract is verified.
 */
#include <obs-module.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "framework/obs/realtime_settings.hpp"
#include <new>
#include "engines/mic_dsp_engine.hpp"

namespace {
struct MicConfig{int profile=0;hyperdsp::MicSettings parameters;};
struct MicFilter {
    std::array<stts::MicDspEngine, MAX_AUDIO_CHANNELS> channels;
    hyperdsp::RealtimeSettings<MicConfig> settings;
    int profile=0;uint32_t sample_rate=0;uint64_t generation=0;
};

const char* get_name(void*) { return "HyperDSP Microphone DSP"; }

void defaults(obs_data_t* settings) {
    obs_data_set_default_int(settings, "mic_type", 0);
    hyperdsp::obs_parameters::defaults(settings,hyperdsp::MicSettings{},hyperdsp::mic_parameters);
}

void update(void* ptr, obs_data_t* settings) {
    if (!ptr) return;
    auto* data = static_cast<MicFilter*>(ptr);
    int selected = int(obs_data_get_int(settings, "mic_type"));
    if (!stts::MicDspEngine::valid(selected)) {
        // Also covers saved scenes that formerly selected type 7 (custom ONNX).
        // Those scenes become dry, never silently mapped to a DSP preset.
        selected = 0;
    }
    hyperdsp::MicSettings parameters;hyperdsp::obs_parameters::read(settings,parameters,hyperdsp::mic_parameters);
    obs_audio_info ai{};if(!obs_get_audio_info(&ai))return;
    try{data->settings.publish(MicConfig{selected,parameters},settings,ai.samples_per_sec,get_audio_channels(ai.speakers));}
    catch(const std::exception& e){blog(LOG_WARNING,"[HyperDSP Microphone] Settings preparation failed: %s",e.what());}
}

void* create(obs_data_t* settings, obs_source_t*) {
    auto* data = new (std::nothrow) MicFilter();
    if (!data) return nullptr;
    update(data, settings);
    return data;
}

void destroy(void* ptr) {
    delete static_cast<MicFilter*>(ptr);
}

obs_properties_t* properties(void*) {
    obs_properties_t* props = obs_properties_create();
    obs_property_t* profile = obs_properties_add_list(
        props, "mic_type", "Microphone-style tone", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    hyperdsp::add_native_modes(profile,hyperdsp::mic_modes);
    obs_property_set_long_description(profile,
        "Procedural tone profiles, not measurements or reproductions of named microphones. "
        "These profiles use conventional DSP only.");
    for(auto& p:hyperdsp::mic_parameters)hyperdsp::obs_parameters::add(props,p);
    return props;
}

obs_audio_data* filter_audio(void* ptr, obs_audio_data* frame) {
    auto* data = static_cast<MicFilter*>(ptr);
    if (!data || !frame)return frame;
    auto* state=data->settings.consume();
    obs_audio_info ai{};
    if(!state||!obs_get_audio_info(&ai)||ai.samples_per_sec!=state->rate||state->rate<8000||state->rate>192000||get_audio_channels(ai.speakers)!=state->channels)return frame;
    if(state->generation!=data->generation){
        const auto& config=state->settings;
        if(data->profile!=config.profile||data->sample_rate!=state->rate){
            data->profile=config.profile;data->sample_rate=state->rate;
            for(auto& channel:data->channels)channel.configure(static_cast<stts::MicDspEngine::Profile>(config.profile),state->rate);
        }
        for(auto& channel:data->channels)channel.set_parameters(config.parameters.vibe,config.parameters.resonance);
        data->generation=state->generation;
    }
    if(!data->profile)return frame;
    for (size_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
        if (frame->data[channel]) {
            data->channels[channel].process(
                reinterpret_cast<float*>(frame->data[channel]), frame->frames);
        }
    }
    return frame;
}
} // namespace

extern "C" {
struct obs_source_info mic_filter = {
    .id = "hyperdsp_mic_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_AUDIO,
    .get_name = get_name,
    .create = create,
    .destroy = destroy,
    .get_defaults = defaults,
    .get_properties = properties,
    .update = update,
    .filter_audio = filter_audio,
};
}
