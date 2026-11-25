#pragma once
#include "framework/dsp/modulation_matrix.hpp"
#include <obs.h>
#include <obs-properties.h>
#include <memory>
#include <string>
namespace hyperdsp {
struct SoundSubscription;
void start_sound_polling();
void stop_sound_polling();
class SoundInputs {
    std::array<std::shared_ptr<SoundSubscription>,modulation_slots> slots_;
public:
    void configure(unsigned slot,int source,const char* channel,const char* category);
    std::array<float,modulation_slots> snapshot()const;
};
// Properties-only discovery; keeps saved UUID even if absent.
void sound_channel_choices(obs_property_t* p,const char* saved);
}
