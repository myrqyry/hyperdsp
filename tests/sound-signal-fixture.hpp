#pragma once
#include <obs.h>
#include <callback/proc.h>
#include <atomic>
#include <string>
#include <chrono>
#include <thread>
// Contract fixture, not an inference model: real libobs procedure invocation.
namespace sound_fixture {
inline std::atomic<double> score{.5};
inline std::atomic<bool> available{true};
inline std::atomic<unsigned> calls{0};
inline std::atomic<unsigned> delay_ms{0};
inline void read(void*,calldata_t* c){
    ++calls;
    if(auto delay=delay_ms.load())std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    const auto* stream=calldata_string(c,"stream");
    const auto* category=calldata_string(c,"category");
    const bool ready=available&&stream&&std::string(stream)=="category"&&category&&std::string(category)=="Speech";
    calldata_set_string(c,"kind","classification");
    calldata_set_bool(c,"connected",true);calldata_set_bool(c,"available",ready);
    calldata_set_float(c,"score",score.load());
}
inline const char* name(void*){return "Sound contract fixture";}
inline void* create(obs_data_t*,obs_source_t* s){
    proc_handler_add(obs_source_get_proc_handler(s),"void sound_signal_read_v1(in string channel, in string stream, in string category, out string kind, out bool connected, out bool available, out string status, out string text, out bool final, out int sequence, out int revision, out string label, out float score, out int completed, out int failed)",read,nullptr);
    return reinterpret_cast<void*>(1);
}
inline void destroy(void*){}
inline obs_source_info info={
    .id="yamnet_classifier_source",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_AUDIO,
    .get_name=name,.create=create,.destroy=destroy
};
}
