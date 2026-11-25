#include <obs-module.h>
#include "interop/external_inputs.hpp"
#include "ladspa-catalog.hpp"

extern "C" struct obs_source_info mic_filter;
extern "C" struct obs_source_info ladspa_filter;
extern "C" struct obs_source_info native_filter;
extern "C" struct obs_source_info dynamics_filter;
extern "C" struct obs_source_info color_filter;
extern "C" struct obs_source_info time_filter;
extern "C" struct obs_source_info space_filter;
extern "C" struct obs_source_info spectral_filter;
extern "C" struct obs_source_info formant_filter;
extern "C" struct obs_source_info graph_filter;
OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    hyperdsp::start_external_polling();
    obs_register_source(&mic_filter);
    obs_register_source(&ladspa_filter);
    obs_register_source(&native_filter);
    obs_register_source(&dynamics_filter);
    obs_register_source(&color_filter);
    obs_register_source(&time_filter);
    obs_register_source(&space_filter);
    obs_register_source(&spectral_filter);
    obs_register_source(&formant_filter);
    obs_register_source(&graph_filter);
    return true;
}

void obs_module_unload(void){hyperdsp::CatalogScan::stop();hyperdsp::stop_external_polling();}
