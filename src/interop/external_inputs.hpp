#pragma once
#include "framework/dsp/modulation_matrix.hpp"
#include <obs.h>
#include <obs-properties.h>
#include <memory>
#include <string>
namespace hyperdsp {
struct ExternalSubscription;
void start_external_polling();
void stop_external_polling();
class ExternalInputs {
    std::array<std::shared_ptr<ExternalSubscription>,modulation_slots> slots_;
public:
    void configure(unsigned slot,int source,const char* channel,const char* category);
    std::array<float,modulation_slots> snapshot()const;
};
// Properties-only discovery; keeps saved UUID even if absent.
void sound_channel_choices(obs_property_t* p,const char* saved);
void sight_channel_choices(obs_property_t* p,const char* saved,int source);
}
