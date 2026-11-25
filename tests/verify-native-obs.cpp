// Actual OBS audio routing test: load the built module, instantiate its
// production native filter, attach it to a synthetic stereo source, and capture
// source output after real filter execution. No fake filter callbacks.
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include "sound-signal-fixture.hpp"
#include "sight-signal-fixture.hpp"

struct Stats {
    std::atomic<uint64_t> samples{0};
    std::atomic<double> sum_l{0}, sum_r{0};
};
static void add(std::atomic<double>& val, double x) {
    double old = val.load();
    while (!val.compare_exchange_weak(old, old+x)) {}
}
static void capture(void* param, obs_source_t*, const struct audio_data* data, bool) {
    auto& s = *static_cast<Stats*>(param);
    auto* l = reinterpret_cast<float*>(data->data[0]);
    auto* r = reinterpret_cast<float*>(data->data[1]);
    if (!l || !r) return;
    double sl = 0, sr = 0;
    for (unsigned i=0;i<data->frames;++i) {
        sl += l[i]*l[i];
        sr += r[i]*r[i];
    }
    add(s.sum_l, sl); add(s.sum_r, sr);
    s.samples += data->frames;
}
static const char* fixture_name(void*) { return "Verification stereo generator"; }
static void* fixture_create(obs_data_t*,obs_source_t*) { return reinterpret_cast<void*>(1); }
static void fixture_destroy(void*) {}
static obs_source_info fixture_source = {
    .id="test_mic_audio_generator",
    .type=OBS_SOURCE_TYPE_INPUT,
    .output_flags=OBS_SOURCE_AUDIO,
    .get_name=fixture_name,
    .create=fixture_create,
    .destroy=fixture_destroy,
};
static void stream(obs_source_t* source, int rounds, double hz) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
    uint64_t start=os_gettime_ns();
    for (int k=0;k<rounds;++k) {
        for (unsigned i=0;i<FRAMES;++i) {
            const float phase=float((uint64_t(k)*FRAMES+i)*6.283185307179586*hz/48000.0);
            l[i]=.4f*std::sin(phase);
            r[i]=.16f*std::sin(phase);
        }
        obs_source_audio audio{};
        audio.data[0]=reinterpret_cast<uint8_t*>(l.data());
        audio.data[1]=reinterpret_cast<uint8_t*>(r.data());
        audio.frames=FRAMES;
        audio.speakers=SPEAKERS_STEREO;
        audio.format=AUDIO_FORMAT_FLOAT_PLANAR;
        audio.samples_per_sec=48000;
        audio.timestamp=start+uint64_t(k)*10000000ULL;
        obs_source_output_audio(source,&audio);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int main(int argc,char**argv){
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_native_filter"))return 5;
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_int(settings,"mode",1);
    obs_data_set_double(settings,"frequency",1000.);
    auto* filter=obs_source_create("hyperdsp_native_filter","filter",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool controls=props&&obs_property_list_item_count(obs_properties_get(props,"mode"))==10 &&
        obs_properties_get(props,"frequency")&&obs_properties_get(props,"q")&&
        obs_properties_get(props,"stages")&&obs_properties_get(props,"alignment")&&obs_properties_get(props,"mod_source")&&
        obs_properties_get(props,"mod_target")&&obs_properties_get(props,"band_sensitivity")&&obs_properties_get(props,"mix");
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);
    obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);
    obs_source_inc_active(source);obs_source_set_audio_active(source,true);
    obs_source_set_async_unbuffered(source,true);
    auto measure=[&](double hz,int warmup=10){
        stream(source,warmup,hz);
        Stats stats;obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40,hz);
        obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),
                                  std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto low=measure(100);
    auto high=measure(8000);
    obs_data_set_int(settings,"stages",4);obs_source_update(filter,settings);
    auto steep=measure(8000);
    auto resonant_cutoff=measure(1000);
    obs_data_set_int(settings,"alignment",1);obs_source_update(filter,settings);
    auto butter_cutoff=measure(1000);
    obs_data_set_int(settings,"alignment",0);obs_source_update(filter,settings);
    obs_data_set_int(settings,"mode",2);obs_data_set_int(settings,"stages",1);obs_source_update(filter,settings);
    auto hp=measure(8000);
    obs_data_set_int(settings,"mode",4);obs_source_update(filter,settings);
    auto notch=measure(1000);
    obs_data_set_int(settings,"mode",1);
    obs_data_set_double(settings,"frequency",200.);
    obs_source_update(filter,settings);
    auto closed=measure(1000);
    obs_data_set_int(settings,"mod_source",2);
    obs_data_set_int(settings,"mod_target",0);
    obs_data_set_double(settings,"mod_depth",1.);
    obs_source_update(filter,settings);
    auto opened=measure(1000);
    obs_data_set_int(settings,"mod_source",0);
    obs_data_set_double(settings,"mix",0.);
    obs_source_update(filter,settings);
    auto mix_dry=measure(1000);
    obs_data_set_int(settings,"mode",9);
    obs_data_set_double(settings,"mix",1.);
    obs_data_set_int(settings,"mod_source",3);
    obs_data_set_int(settings,"mod_target",4);
    obs_data_set_double(settings,"mod_depth",1.);
    obs_data_set_double(settings,"band_sensitivity",6.);
    obs_source_update(filter,settings);
    auto bass_active=measure(100);
    obs_data_set_int(settings,"mod2_source",3);
    obs_data_set_int(settings,"mod2_target",4);
    obs_data_set_double(settings,"mod2_depth",-.5);
    obs_data_set_double(settings,"mod2_sensitivity",6.);
    obs_source_update(filter,settings);
    auto combined=measure(100,60);
    obs_data_set_int(settings,"mod3_source",3);
    obs_data_set_int(settings,"mod3_target",4);
    obs_data_set_double(settings,"mod3_depth",.5);
    obs_data_set_double(settings,"mod3_sensitivity",6.);
    obs_source_update(filter,settings);
    auto canceled=measure(100,60);
    obs_data_set_int(settings,"mod2_source",0);
    obs_data_set_int(settings,"mod3_source",0);
    obs_source_update(filter,settings);
    auto restored=measure(100,60);
    auto* matrix_props=obs_source_properties(filter);
    bool matrix_ok=matrix_props&&obs_properties_get(matrix_props,"mod2_target")&&obs_properties_get(matrix_props,"mod4_target");
    if(matrix_props)obs_properties_destroy(matrix_props);
    matrix_ok=matrix_ok&&combined[0]<bass_active[0]*.9&&combined[0]>.30&&
        std::abs(canceled[0]-restored[0])<.002&&std::abs(restored[1]/restored[0]-.4)<.001;
    std::printf("NATIVE_MATRIX base=%g combined=%g canceled=%g restored=%g pass=%d\n",bass_active[0],combined[0],canceled[0],restored[0],matrix_ok);
    auto bass_reject=measure(8000,60);
    obs_data_set_int(settings,"mod_source",5);
    obs_source_update(filter,settings);
    auto treble_active=measure(8000);
    auto treble_reject=measure(100,60);
    obs_data_set_int(settings,"mod_source",0);
    obs_data_set_double(settings,"mix",1.);
    obs_data_set_int(settings,"mode",0);obs_source_update(filter,settings);
    auto dry=measure(8000);
    obs_register_source(&sound_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    const auto channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    obs_data_set_int(settings,"mode",9);
    obs_data_set_int(settings,"mod_source",6);
    obs_data_set_int(settings,"mod_target",4);
    obs_data_set_double(settings,"mod_depth",1.);
    obs_data_set_string(settings,"sound_channel",channel.c_str());
    obs_data_set_string(settings,"sound_category","Speech");
    obs_source_update(filter,settings);
    auto external=measure(1000,60);
    sound_fixture::available=false;
    auto unavailable=measure(1000,80);
    sound_fixture::available=true;
    obs_source_set_enabled(producer,false);
    auto disabled=measure(1000,80);
    obs_source_filter_remove(source,producer);
    obs_source_release(producer);
    auto detached=measure(1000,60);
    const bool external_ok=sound_fixture::calls>0&&std::abs(external[0]-.424264)<.004&&
        std::abs(unavailable[0]-.282843)<.004&&std::abs(disabled[0]-.282843)<.004&&
        std::abs(detached[0]-.282843)<.004;
    std::printf("SOUND_NATIVE_OBS active=%g unavailable=%g disabled=%g detached=%g calls=%u pass=%d\n",external[0],unavailable[0],disabled[0],detached[0],sound_fixture::calls.load(),external_ok);
    obs_register_source(&sight_fixture::info);
    obs_data_set_int(settings,"mod_source",8);
    obs_data_set_string(settings,"sight_channel","test-hand");
    obs_source_update(filter,settings);
    auto hand_x=measure(1000,60);
    sight_fixture::ambiguous=true;
    auto duplicate=measure(1000,60);
    sight_fixture::ambiguous=false;
    sight_fixture::bad_abi=true;
    auto invalid=measure(1000,60);
    sight_fixture::bad_abi=false;
    obs_data_set_int(settings,"mod_source",9);
    obs_source_update(filter,settings);
    auto hand_y=measure(1000,60);
    obs_data_set_int(settings,"mod_source",10);
    obs_source_update(filter,settings);
    auto pinch=measure(1000,60);
    sight_fixture::available=false;
    auto absent=measure(1000,60);
    bool sight_ok=sight_fixture::calls>0&&std::abs(hand_x[0]-.494975)<.005&&
        std::abs(hand_y[0]-.353553)<.005&&std::abs(pinch[0]-.339411)<.005&&
        std::abs(duplicate[0]-.282843)<.005&&std::abs(invalid[0]-.282843)<.005&&std::abs(absent[0]-.282843)<.005;
    std::printf("SIGHT_NATIVE_OBS x=%g y=%g pinch=%g duplicate=%g invalid=%g absent=%g pass=%d\n",hand_x[0],hand_y[0],pinch[0],duplicate[0],invalid[0],absent[0],sight_ok);
    bool ok=sight_ok&&external_ok&&matrix_ok&&controls&&bass_active[0]>.40&&bass_reject[0]<.32&&treble_active[0]>.40&&treble_reject[0]<.32&&std::abs(resonant_cutoff[0]-.4/std::sqrt(2.)*.25)<.002&&std::abs(butter_cutoff[0]-.2)<.002&&opened[0]>2*closed[0]&&std::abs(opened[1]/opened[0]-.4)<.001&&std::abs(mix_dry[0]-.4/std::sqrt(2.))<.001&&low[0]>.27&&high[0]<.006&&steep[0]<.0001&&hp[0]>.27&&notch[0]<.0001&&
        std::abs(dry[0]-.4/std::sqrt(2.))<.001&&std::abs(dry[1]-.16/std::sqrt(2.))<.001&&
        std::abs(low[1]/low[0]-.4)<.001&&std::abs(hp[1]/hp[0]-.4)<.001;
    std::printf("NATIVE_ENVELOPE_OBS closed=%g opened=%g mix_dry=%g\n",closed[0],opened[0],mix_dry[0]);
    std::printf("BUTTERWORTH_OBS resonant=%g butterworth=%g\n",resonant_cutoff[0],butter_cutoff[0]);
    std::printf("BAND_OBS bass_active=%g bass_reject=%g treble_active=%g treble_reject=%g\n",bass_active[0],bass_reject[0],treble_active[0],treble_reject[0]);
    std::printf("NATIVE_OBS low=%g high=%g steep=%g hp=%g notch=%g dry=%g,%g controls=%d pass=%d\n",
        low[0],high[0],steep[0],hp[0],notch[0],dry[0],dry[1],controls,ok);
    obs_set_output_source(1,nullptr);
    obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();
    return ok?0:1;
}
