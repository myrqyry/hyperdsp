#include "ladspa-catalog.hpp"
#include "platform/scan_process.hpp"
#include <mutex>
namespace hyperdsp {
namespace {
std::mutex mutex;
platform::ScanProcess child;
std::string state="Ready to scan trusted installed effects.";
void reap(){
    auto result=child.poll();
    if(result==platform::ScanResult::success)state="Scan finished. Reload catalog to see results.";
    else if(result==platform::ScanResult::failed)state="Scan failed. Previous catalog preserved if available; see OBS log.";
}
}
std::string CatalogScan::start(){
    std::lock_guard lock(mutex);reap();
    if(child.poll()==platform::ScanResult::running)return state;
    namespace fs=std::filesystem;
    const char* binary=obs_get_module_binary_path(obs_current_module());
    if(!binary)return state="Scanner location unavailable.";
    const fs::path base=platform::native_path(binary).parent_path();
    // Build tree: adjacent tools. OBS33: module-root/tools.
    // Legacy OBS: plugin-root/bin/64bit/module.so and plugin-root/tools.
    std::error_code ec;
    const auto complete=[&](const fs::path& dir){
        return fs::is_regular_file(dir/"scan-ladspa.py",ec)&&
               fs::is_regular_file(dir/platform::probe_name,ec);
    };
    fs::path tools=base;
    if(!complete(tools))tools=base/"tools";
    if(!complete(tools)&&base.filename()=="64bit"&&base.parent_path().filename()=="bin")
        tools=base.parent_path().parent_path()/"tools";
    if(!complete(tools)&&base.filename()=="MacOS"&&base.parent_path().filename()=="Contents")
        tools=base.parent_path()/"Resources"/"tools";
    const auto script=tools/"scan-ladspa.py",probe=tools/platform::probe_name;
    auto output=catalog_path();
    if(output.empty()||!fs::is_regular_file(script,ec)||!fs::is_regular_file(probe,ec))
        return state="Scanner files missing; install HyperDSP with its tools.";
#ifdef _WIN32
    const char* python="python";
#else
    const char* python="python3";
#endif
    int rc=child.start(python,{platform::utf8_path(script),"--probe",platform::utf8_path(probe),"--output",output});
    if(rc){blog(LOG_WARNING,"[HyperDSP] LADSPA scanner launch failed: %d",rc);return state="Could not start scanner (Python 3 required).";}
    return state="Scanning installed effects. Click Reload catalog when ready.";
}
std::string CatalogScan::status(){std::lock_guard lock(mutex);reap();return state;}
void CatalogScan::stop(){
    std::lock_guard lock(mutex);reap();
    child.stop();
}
}
