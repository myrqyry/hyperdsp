#include <obs.h>
#include <obs-module.h>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
int main(int argc,char** argv){
    if(argc!=4||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    namespace fs=std::filesystem;
    const auto temp=fs::temp_directory_path()/("hyperdsp-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp);
    const auto output=temp/"catalog.json";
    fs::create_symlink(fs::canonical(argv[3]),temp/"amp.so");
    setenv("HYPERDSP_CATALOG",output.c_str(),1);
    setenv("LADSPA_PATH",temp.c_str(),1);
    auto* settings=obs_data_create();
    auto* filter=obs_source_create_private("hyperdsp_ladspa_filter","catalog fixture",settings);
    auto* props=filter?obs_source_properties(filter):nullptr;
    auto* scan=props?obs_properties_get(props,"catalog_scan"):nullptr;
    auto* reload=props?obs_properties_get(props,"catalog_reload"):nullptr;
    auto* search=props?obs_properties_get(props,"catalog_search"):nullptr;
    auto* list=props?obs_properties_get(props,"catalog_effect"):nullptr;
    auto* collections=props?obs_properties_get(props,"catalog_collections"):nullptr;
    auto* group=collections?obs_property_group_content(collections):nullptr;
    auto* lsp=group?obs_properties_get(group,"catalog_lsp"):nullptr;
    bool ok=lsp&&obs_property_button_type(lsp)==OBS_BUTTON_URL&&
        std::string(obs_property_button_url(lsp))=="https://lsp-plug.in/"&&scan&&reload&&search&&list&&obs_property_list_item_count(list)==1&&
        obs_properties_get(props,"catalog_sources");
    if(ok){
        obs_data_set_string(settings,"catalog_search","amplifier mono");
        obs_property_modified(search,settings);
        obs_property_button_clicked(scan,nullptr);
        ok=ok&&std::string(obs_property_description(reload)).find("scan running")!=std::string::npos;
        for(int i=0;i<100&&!fs::exists(output);++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        obs_property_button_clicked(reload,nullptr);
        ok=ok&&fs::exists(output)&&obs_property_list_item_count(list)==2;
        const char* selected=ok?obs_property_list_item_string(list,1):nullptr;
        ok=ok&&selected&&std::string(selected).ends_with("\tamp_mono");
        obs_data_set_string(settings,"catalog_search","amplifier stereo");
        obs_property_modified(search,settings);
        selected=obs_property_list_item_count(list)==2?obs_property_list_item_string(list,1):nullptr;
        ok=ok&&selected&&std::string(selected).ends_with("\tamp_stereo");
        ok=ok&&std::string(obs_data_get_string(settings,"library")).empty();
        obs_data_set_string(settings,"catalog_search","no-such-effect");
        obs_property_modified(search,settings);
        ok=ok&&obs_property_list_item_count(list)==1;
        obs_data_set_string(settings,"catalog_search","utility amp.so stereo");
        obs_property_modified(search,settings);
        ok=ok&&obs_property_list_item_count(list)==2;
    }
    if(props)obs_properties_destroy(props);
    if(filter)obs_source_release(filter);
    obs_data_release(settings);obs_shutdown();
    fs::remove_all(temp);
    std::printf("CATALOG_OBS_PASS=%d\n",ok);return ok?0:1;
}
