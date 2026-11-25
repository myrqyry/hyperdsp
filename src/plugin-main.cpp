#include <obs-module.h>

extern "C" struct obs_source_info mic_filter;
extern "C" struct obs_source_info ladspa_filter;
OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    obs_register_source(&mic_filter);
    obs_register_source(&ladspa_filter);
    return true;
}
