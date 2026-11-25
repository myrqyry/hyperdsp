#include "effect-library.hpp"
#include <obs.h>
#include <cstdio>
#include <set>
#include <string>
int main(int argc,char** argv){
    if(argc!=4||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    bool ok=true;unsigned modes=0;std::set<std::string> ids;
    for(const auto& family:hyperdsp::native_families){
        auto* props=obs_get_source_properties(family.source);
        auto* p=props?obs_properties_get(props,family.mode_key):nullptr;
        ok=ok&&p&&obs_property_list_item_count(p)==family.modes.size();
        if(p)for(size_t i=0;i<family.modes.size();++i){
            const auto& m=family.modes[i];
            ok=ok&&obs_property_list_item_int(p,i)==m.value&&
                std::string(obs_property_list_item_name(p,i))==m.name;
            if(m.value)++modes;
        }
        if(props)obs_properties_destroy(props);
    }
    auto entries=hyperdsp::effect_library(hyperdsp::read_catalog(argv[3]),2);
    unsigned native=0,external=0;
    for(auto& e:entries){
        ok=ok&&ids.insert(e.id).second&&!e.name.empty()&&!e.category.empty();
        if(e.format=="native")++native;else if(e.format=="ladspa")++external;
    }
    ok=ok&&native==modes&&native==38&&external==2;
    auto mono=hyperdsp::effect_library(hyperdsp::read_catalog(argv[3]),1);
    unsigned incompatible=0;for(auto& e:mono)if(!e.compatible)++incompatible;
    ok=ok&&incompatible==1;
    auto matched=hyperdsp::search_effects(entries,"amplifier mono","ladspa");
    ok=ok&&matched.size()==1&&matched[0].label=="amp_mono";
    matched=hyperdsp::search_effects(entries,"freeze spectral","native");
    ok=ok&&matched.size()==1&&matched[0].mode==2;
    std::printf("LIBRARY native=%u external=%u incompatible_mono=%u pass=%d\n",native,external,incompatible,ok);
    obs_shutdown();return ok?0:1;
}
