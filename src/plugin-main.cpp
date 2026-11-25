#include <obs-module.h>
#include "interop/sound_inputs.hpp"

extern "C" struct obs_source_info mic_filter;
extern "C" struct obs_source_info ladspa_filter;
extern "C" struct obs_source_info native_filter;
OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    hyperdsp::start_sound_polling();
    obs_register_source(&mic_filter);
    obs_register_source(&ladspa_filter);
    obs_register_source(&native_filter);
    return true;
}

void obs_module_unload(void){hyperdsp::stop_sound_polling();}
