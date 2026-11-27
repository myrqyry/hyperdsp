#pragma once
#include <obs-module.h>
#include <util/bmem.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <sstream>
#include <filesystem>
#include <vector>
#include "framework/dsp/ladspa_controls.hpp"
#include "platform/native_library.hpp"

namespace hyperdsp {
struct ControlHint {unsigned port;std::string name;LADSPA_PortRangeHint hint;};
struct CatalogEntry {
    std::string path,label,name,maker,category;
    int channels;
    std::vector<ControlHint> controls;
    bool controls_complete=false;
    std::string id() const { return path+"\t"+label; }
};
using Catalog=std::vector<CatalogEntry>;
inline std::string lower(std::string text){
    for(char& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
inline std::string catalog_path(){
#ifdef _WIN32
    if(const wchar_t* path=_wgetenv(L"HYPERDSP_CATALOG"))return platform::utf8_path(std::filesystem::path(path));
#else
    if(const char* path=std::getenv("HYPERDSP_CATALOG"))return path;
#endif
    char* config=obs_module_config_path("ladspa-catalog.json");
    std::string result=config?config:"";bfree(config);return result;
}
inline bool match_words(std::string searchable,const std::string& query){
    searchable=lower(std::move(searchable));
    std::istringstream words(lower(query));std::string word;
    while(words>>word)if(searchable.find(word)==std::string::npos)return false;
    return true;
}
inline bool catalog_matches(const CatalogEntry& e,const std::string& query){
    return match_words(e.name+" "+e.label+" "+e.maker+" "+e.category+" "+
        platform::utf8_path(platform::native_path(e.path).filename())+" "+(e.channels==1?"mono":"stereo")+" ladspa",query);
}
struct CatalogScan {
    static std::string start();
    static std::string status();
    static void stop();
};
inline Catalog read_catalog(const std::string& path){
    Catalog entries;
    if(path.empty())return entries;
    auto* root=obs_data_create_from_json_file(path.c_str());
    if(!root)return entries;
    if(obs_data_get_int(root,"schema")==1){
        auto* items=obs_data_get_array(root,"effects");
        if(items){
            for(size_t i=0;i<std::min<size_t>(obs_data_array_count(items),20000);++i){
                auto* item=obs_data_array_item(items,i);
                CatalogEntry e{obs_data_get_string(item,"path"),obs_data_get_string(item,"label"),
                    obs_data_get_string(item,"name"),obs_data_get_string(item,"maker"),
                    obs_data_get_string(item,"category"),int(obs_data_get_int(item,"channels"))};
                if(auto* controls=obs_data_get_array(item,"controls")){
                    const auto count=obs_data_array_count(controls);
                    bool valid=count<=256;
                    for(size_t j=0;j<std::min<size_t>(count,256);++j){
                        auto* c=obs_data_array_item(controls,j);
                        auto port=obs_data_get_int(c,"port"),flags=obs_data_get_int(c,"flags");
                        auto lo=obs_data_get_double(c,"lower"),hi=obs_data_get_double(c,"upper");
                        if(port>=0&&port<256&&flags>=0&&flags<=65535&&std::isfinite(float(lo))&&std::isfinite(float(hi))&&
                            std::none_of(e.controls.begin(),e.controls.end(),[&](auto& h){return h.port==port;}))
                            e.controls.push_back({unsigned(port),obs_data_get_string(c,"name"),{LADSPA_PortRangeHintDescriptor(flags),float(lo),float(hi)}});
                        else valid=false;
                        obs_data_release(c);
                    }
                    e.controls_complete=valid&&obs_data_get_bool(item,"controls_complete");
                    obs_data_array_release(controls);
                }
#ifdef _WIN32
                std::replace(e.path.begin(),e.path.end(),'\\','/');
#endif
                if(platform::absolute_library_path(e.path)&&!e.label.empty()&&!e.name.empty()&&
                    e.path.find('\t')==std::string::npos&&e.label.find('\t')==std::string::npos&&
                    (e.channels==1||e.channels==2))entries.push_back(std::move(e));
                obs_data_release(item);
            }
            obs_data_array_release(items);
        }
    }
    obs_data_release(root);
    return entries;
}
inline Catalog read_catalog(){return read_catalog(catalog_path());}
}
