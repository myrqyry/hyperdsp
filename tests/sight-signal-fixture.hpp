#pragma once
#include "../src/interop/sight/sight_snapshot_v1.h"
#include "../src/interop/sight/sight_motion_v1.h"
#include <obs.h>
#include <callback/proc.h>
#include <atomic>
#include <cstring>
namespace sight_fixture {
inline std::atomic<bool> available{true},ambiguous{false},bad_abi{false};
inline std::atomic<unsigned> calls{0},motion_calls{0};
inline void read(void*,calldata_t* c){
    ++calls;
    void* raw=nullptr;
    const char* channel=calldata_string(c,"channel");
    bool valid=available&&channel&&!strcmp(channel,"test-hand")&&calldata_get_ptr(c,"snapshot",&raw)&&raw;
    if(valid){
        auto& s=*static_cast<sight_snapshot_v1*>(raw);
        valid=s.abi_version==1&&s.struct_size==sizeof(s);
        if(valid){
            s.kind=SIGHT_SNAPSHOT_HAND;s.sequence=1;s.source_width=640;s.source_height=480;
            s.point_count=21;s.hands_count=1;
            s.hands[0][0]={.75f,.25f,0,1};s.pinch[0]=.2f;
            if(bad_abi)s.abi_version=9;
        }
    }
    calldata_set_bool(c,"available",valid);
}
inline void list(void*,calldata_t* c){
    void* raw=nullptr;if(!calldata_get_ptr(c,"inventory",&raw)||!raw)return;
    auto* d=static_cast<obs_data_t*>(raw);
    auto* a=obs_data_array_create();auto* r=obs_data_create();
    obs_data_set_string(r,"channel","test-hand");obs_data_set_string(r,"label","Test hand");
    obs_data_set_int(r,"kind",SIGHT_SNAPSHOT_HAND);obs_data_set_bool(r,"enabled",true);
    obs_data_set_bool(r,"ambiguous",ambiguous);
    obs_data_array_push_back(a,r);obs_data_set_array(d,"channels",a);obs_data_set_int(d,"version",1);
    obs_data_release(r);obs_data_array_release(a);
}
inline const char* name(void*){return "Sight contract fixture";}
inline void* create(obs_data_t*,obs_source_t* s){
    auto* h=obs_source_get_proc_handler(s);
    proc_handler_add(h,"void sight_tracking_read_v1(in string channel, in int max_age_ms, in ptr snapshot, out bool available)",read,nullptr);
    proc_handler_add(h,"void sight_tracking_list_v1(in ptr inventory)",list,nullptr);
    return reinterpret_cast<void*>(1);
}
inline void destroy(void*){}
inline obs_source_info info={.id="sight_tracking_receiver_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_VIDEO,
    .get_name=name,.create=create,.destroy=destroy};
inline uint32_t width(void*){return 640;}
inline uint32_t height(void*){return 480;}
inline void motion(void*,calldata_t* c){
    ++motion_calls;
    void* raw=nullptr;
    bool valid=available&&calldata_get_ptr(c,"snapshot",&raw)&&raw;
    if(valid){
        auto& s=*static_cast<sight_motion_v1*>(raw);
        valid=s.abi_version==1&&s.struct_size==sizeof(s);
        if(valid){s.sequence=1;s.source_width=640;s.source_height=480;s.energy=.4f;if(bad_abi)s.source_width=999;}
    }
    calldata_set_bool(c,"available",valid);
}
inline void* motion_create(obs_data_t*,obs_source_t* s){
    proc_handler_add(obs_source_get_proc_handler(s),"void sight_motion_read_v1(in int max_age_ms, in ptr snapshot, out bool available)",motion,nullptr);
    return reinterpret_cast<void*>(1);
}
inline obs_source_info motion_info={.id="sight_u2net_filter",.type=OBS_SOURCE_TYPE_FILTER,.output_flags=OBS_SOURCE_VIDEO,
    .get_name=name,.create=motion_create,.destroy=destroy,.get_width=width,.get_height=height};
inline void* video_create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
inline obs_source_info video_info={.id="hyperdsp_test_video",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_VIDEO,
    .get_name=name,.create=video_create,.destroy=destroy,.get_width=width,.get_height=height};
}
