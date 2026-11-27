#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace hyperdsp::platform {
inline std::filesystem::path native_path(std::string_view value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
inline std::string utf8_path(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
inline bool absolute_library_path(std::string_view value) {
    if(value.empty()||value.find_first_of("\t\r\n")!=value.npos||value.find('\0')!=value.npos)return false;
    try {return native_path(value).is_absolute();} catch(const std::filesystem::filesystem_error&) {return false;}
}
inline void* open_library(std::string_view value) {
    if(!absolute_library_path(value))return nullptr;
#ifdef _WIN32
    // Never search the working directory for a plugin or its dependencies.
    return LoadLibraryExW(native_path(value).c_str(),nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    return dlopen(native_path(value).c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
}
template<class Function> Function library_symbol(void* library,const char* name) {
    if(!library)return nullptr;
#ifdef _WIN32
    return reinterpret_cast<Function>(GetProcAddress(static_cast<HMODULE>(library),name));
#else
    return reinterpret_cast<Function>(dlsym(library,name));
#endif
}
inline void close_library(void* library) {
    if(!library)return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
inline constexpr const char* probe_name=
#ifdef _WIN32
    "hyperdsp-ladspa-probe.exe";
#else
    "hyperdsp-ladspa-probe";
#endif
inline constexpr const char* library_filter=
#ifdef _WIN32
    "Shared library (*.dll)";
#elif defined(__APPLE__)
    "Shared library (*.so *.dylib)";
#else
    "Shared library (*.so *.SO)";
#endif
}
