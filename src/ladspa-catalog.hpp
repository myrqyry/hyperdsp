#pragma once
#include <obs-module.h>
#include <util/bmem.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace hyperdsp {
struct CatalogEntry {
    std::string path,label,name,maker,category;
    int channels;
    std::string id() const { return path+"\t"+label; }
};
using Catalog=std::vector<CatalogEntry>;
inline std::string lower(std::string text){
    for(char& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
inline Catalog read_catalog(){
    Catalog entries;
    const char* override_path=std::getenv("HYPERDSP_CATALOG");
    char* config=override_path?nullptr:obs_module_config_path("ladspa-catalog.json");
    auto* root=obs_data_create_from_json_file(override_path?override_path:(config?config:""));
    bfree(config);
    if(!root)return entries;
    if(obs_data_get_int(root,"schema")==1){
        auto* items=obs_data_get_array(root,"effects");
        if(items){
            for(size_t i=0;i<std::min<size_t>(obs_data_array_count(items),20000);++i){
                auto* item=obs_data_array_item(items,i);
                CatalogEntry e{obs_data_get_string(item,"path"),obs_data_get_string(item,"label"),
                    obs_data_get_string(item,"name"),obs_data_get_string(item,"maker"),
                    obs_data_get_string(item,"category"),int(obs_data_get_int(item,"channels"))};
                if(!e.path.empty()&&e.path.front()=='/'&&!e.label.empty()&&!e.name.empty()&&
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
}
