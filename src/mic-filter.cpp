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
#include <mutex>
#include <new>
#include "engines/mic_dsp_engine.hpp"

namespace {
struct MicFilter {
    std::array<stts::MicDspEngine, MAX_AUDIO_CHANNELS> channels;
    std::mutex configuration_mutex;
    std::atomic<int> profile{0};
    uint32_t sample_rate{48000};
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
    std::lock_guard<std::mutex> lock(data->configuration_mutex);
    if (data->profile.load(std::memory_order_relaxed) != selected) {
        data->profile.store(0, std::memory_order_release);
        for (auto& channel : data->channels)
            channel.configure(static_cast<stts::MicDspEngine::Profile>(selected),
                              data->sample_rate);
    }
    for (auto& channel : data->channels)
        channel.set_parameters(parameters.vibe, parameters.resonance);
    data->profile.store(selected, std::memory_order_release);
}

void* create(obs_data_t* settings, obs_source_t*) {
    auto* data = new (std::nothrow) MicFilter();
    if (!data) return nullptr;
    const audio_t* audio = obs_get_audio();
    if (audio) data->sample_rate = audio_output_get_sample_rate(audio);
    if (!data->sample_rate) data->sample_rate = 48000;
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
    if (!data || !frame || data->profile.load(std::memory_order_acquire) == 0)
        return frame;

    // Audio callbacks do not allocate, wait for a UI update, or access files.
    std::unique_lock<std::mutex> lock(data->configuration_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return frame;
    if (data->profile.load(std::memory_order_relaxed) == 0) return frame;

    const audio_t* audio = obs_get_audio();
    if (!audio || audio_output_get_sample_rate(audio) != data->sample_rate)
        return frame; // A rate change needs a fresh configured instance.

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
