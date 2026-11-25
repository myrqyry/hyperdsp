#include <obs-module.h>

extern "C" struct obs_source_info mic_filter;
extern "C" struct obs_source_info ladspa_filter;
extern "C" struct obs_source_info native_filter;
OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    obs_register_source(&mic_filter);
    obs_register_source(&ladspa_filter);
    obs_register_source(&native_filter);
    return true;
}
