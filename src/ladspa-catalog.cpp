#include "ladspa-catalog.hpp"
#include <cerrno>
#include <csignal>
#include <mutex>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace hyperdsp {
namespace {
std::mutex mutex;
pid_t child=-1;
std::string state="Ready to scan trusted installed effects.";
void reap(){
    if(child<=0)return;
    int status=0;pid_t result;
    do{result=waitpid(child,&status,WNOHANG);}while(result<0&&errno==EINTR);
    if(result==0)return;
    state=result==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0?
        "Scan finished. Reload catalog to see results.":
        "Scan failed. Previous catalog preserved if available; see OBS log.";
    child=-1;
}
}
std::string CatalogScan::start(){
    std::lock_guard lock(mutex);reap();
    if(child>0)return state;
    namespace fs=std::filesystem;
    const char* binary=obs_get_module_binary_path(obs_current_module());
    if(!binary)return state="Scanner location unavailable.";
    const fs::path base=fs::path(binary).parent_path();
    // Build tree: adjacent tools. OBS33: module-root/tools.
    // Legacy OBS: plugin-root/bin/64bit/module.so and plugin-root/tools.
    std::error_code ec;
    const auto complete=[&](const fs::path& dir){
        return fs::is_regular_file(dir/"scan-ladspa.py",ec)&&
               fs::is_regular_file(dir/"hyperdsp-ladspa-probe",ec);
    };
    fs::path tools=base;
    if(!complete(tools))tools=base/"tools";
    if(!complete(tools)&&base.filename()=="64bit"&&base.parent_path().filename()=="bin")
        tools=base.parent_path().parent_path()/"tools";
    const auto script=(tools/"scan-ladspa.py").string(),probe=(tools/"hyperdsp-ladspa-probe").string();
    auto output=catalog_path();
    if(output.empty()||!fs::is_regular_file(script,ec)||!fs::is_regular_file(probe,ec))
        return state="Scanner files missing; install HyperDSP with its tools.";
    std::string python="python3",probe_flag="--probe",output_flag="--output";
    char* argv[]={python.data(),const_cast<char*>(script.c_str()),probe_flag.data(),const_cast<char*>(probe.c_str()),output_flag.data(),output.data(),nullptr};
    posix_spawnattr_t attr;
    if(posix_spawnattr_init(&attr)!=0)return state="Cannot prepare scanner process.";
    int rc=posix_spawnattr_setpgroup(&attr,0);
    if(!rc)rc=posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);
    pid_t pid=-1;
    if(!rc)rc=posix_spawnp(&pid,python.c_str(),nullptr,&attr,argv,environ);
    posix_spawnattr_destroy(&attr);
    if(rc){blog(LOG_WARNING,"[HyperDSP] LADSPA scanner launch failed: %d",rc);return state="Could not start scanner (Python 3 required).";}
    child=pid;
    return state="Scanning installed effects. Click Reload catalog when ready.";
}
std::string CatalogScan::status(){std::lock_guard lock(mutex);reap();return state;}
void CatalogScan::stop(){
    std::lock_guard lock(mutex);reap();
    if(child>0){
        // Terminate the dedicated scan process group, including its current probe.
        kill(-child,SIGKILL);
        int status;while(waitpid(child,&status,0)<0&&errno==EINTR){}
        child=-1;
    }
}
}
