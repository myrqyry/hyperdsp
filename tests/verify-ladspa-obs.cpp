#include "history-contention-fixture.hpp"
// Actual OBS audio routing test: load the built module, instantiate its
// production Mic filter, attach it to a synthetic stereo source, and capture
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
#include <string>
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
static void stream(obs_source_t* source, int rounds, double hz=9000.) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
    const auto release=std::chrono::steady_clock::now();
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
        std::this_thread::sleep_until(release+std::chrono::milliseconds(10*(k+1)));
    }
}
int main(int argc,char**argv) {
    if(argc!=4 || !obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{}; ai.samples_per_sec=48000; ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS || !obs_init_module(module))return 4;
    if(!obs_get_source_output_flags("hyperdsp_ladspa_filter"))return 5;
    obs_register_source(&fixture_source);
    auto* source=obs_source_create("test_mic_audio_generator","tone",nullptr,nullptr);
    auto* settings=obs_data_create();
    obs_data_set_string(settings,"library",argv[3]);
    obs_data_set_string(settings,"label","amp_stereo");
    obs_data_set_double(settings,"port_0",.25);
    auto* filter=obs_source_create("hyperdsp_ladspa_filter","ladspa",settings,nullptr);
    if(!source||!filter)return 6;
    auto* props=obs_source_properties(filter);
    bool prop_ok=props && obs_properties_get(props,"port_0") && obs_properties_get(props,"mix") && obs_properties_get(props,"input_db") && obs_properties_get(props,"output_db") && obs_properties_get(props,"lfo_target") && obs_properties_get(props,"lfo_rate") && obs_properties_get(props,"lfo_depth");
    auto* browser=props?obs_properties_get(props,"catalog_effect"):nullptr;
    auto* search=props?obs_properties_get(props,"catalog_search"):nullptr;
    auto* category=props?obs_properties_get(props,"catalog_category"):nullptr;
    bool browser_ok=browser&&search&&category;
    std::string browser_choice;
    if(browser_ok){
        obs_data_set_string(settings,"catalog_search","mono amplifier");
        obs_property_modified(search,settings);
        std::string choice;
        for(size_t i=0;i<obs_property_list_item_count(browser);++i){
            auto* value=obs_property_list_item_string(browser,i);
            if(value&&std::string(value).ends_with("\tamp_mono"))choice=value;
        }
        browser_ok=!choice.empty()&&obs_property_list_item_count(browser)<=3;
        if(browser_ok){
            browser_choice=choice;
            obs_data_set_string(settings,"catalog_effect",choice.c_str());
            obs_property_modified(browser,settings);
            browser_ok=std::string(obs_data_get_string(settings,"label"))=="amp_mono" &&
                !obs_data_has_user_value(settings,"port_0") && obs_properties_get(props,"port_0");
        }
        obs_data_set_string(settings,"catalog_effect","");
        obs_data_set_string(settings,"catalog_search","");
        obs_data_set_string(settings,"library",argv[3]);
        obs_data_set_string(settings,"label","amp_stereo");
        obs_data_set_double(settings,"port_0",.25);
        obs_source_update(filter,settings);
    }
    if(props)obs_properties_destroy(props);
    obs_source_filter_add(source,filter);
    obs_set_output_source(1,source);
    obs_source_set_audio_mixers(source,1);
    obs_source_inc_active(source);
    obs_source_set_audio_active(source,true);
    obs_source_set_async_unbuffered(source,true);
    auto measure=[&](double hz=9000.,int warmup=0) {
        if(warmup)stream(source,warmup,hz);
        Stats stats;
        obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40,hz);
        obs_source_remove_audio_capture_callback(source,capture,&stats);
        return std::array<double,2>{std::sqrt(stats.sum_l.load()/std::max<uint64_t>(1,stats.samples.load())),
                                   std::sqrt(stats.sum_r.load()/std::max<uint64_t>(1,stats.samples.load()))};
    };
    auto quiet=measure();
    if(browser_ok){
        auto* live=obs_source_properties(filter);
        auto* live_browser=obs_properties_get(live,"catalog_effect");
        obs_data_set_string(settings,"catalog_effect",browser_choice.c_str());
        obs_property_modified(live_browser,settings);
        obs_source_update(filter,settings);
        auto catalog_default=measure();
        browser_ok=std::abs(catalog_default[0]-.4/std::sqrt(2.))<.005 &&
                   std::abs(catalog_default[1]-.16/std::sqrt(2.))<.005;
        obs_data_set_double(settings,"port_0",.25);
        obs_source_update(filter,settings);
        auto catalog_gain=measure();
        browser_ok=browser_ok&&std::abs(catalog_gain[0]-quiet[0])<.005 &&
                   std::abs(catalog_gain[1]-quiet[1])<.005;
        obs_properties_destroy(live);
        obs_data_set_string(settings,"catalog_effect","");
        obs_data_set_string(settings,"label","amp_stereo");
        obs_source_update(filter,settings);
    }
    obs_data_set_double(settings,"port_0",.5);
    obs_source_update(filter,settings);
    auto louder=measure();
    // 2 Hz output gain LFO, depth .5: over two periods RMS multiplier sqrt(1+.5^2/2).
    obs_data_set_int(settings,"lfo_target",2);
    obs_data_set_double(settings,"lfo_rate",2.);
    obs_data_set_double(settings,"lfo_depth",.5);
    obs_source_update(filter,settings);
    Stats lfo_stats;
    obs_source_add_audio_capture_callback(source,capture,&lfo_stats);
    stream(source,100);
    obs_source_remove_audio_capture_callback(source,capture,&lfo_stats);
    const double lfo_l=std::sqrt(lfo_stats.sum_l.load()/std::max<uint64_t>(1,lfo_stats.samples.load()));
    const double lfo_r=std::sqrt(lfo_stats.sum_r.load()/std::max<uint64_t>(1,lfo_stats.samples.load()));
    const bool lfo_ok=std::abs(lfo_l/louder[0]-std::sqrt(1.125))<.02 &&
                      std::abs(lfo_r/louder[1]-std::sqrt(1.125))<.02;
    obs_data_set_int(settings,"lfo_target",0);
    obs_source_update(filter,settings);
    obs_data_set_double(settings,"mix",.5);
    obs_source_update(filter,settings);
    auto blend=measure();
    obs_data_set_double(settings,"mix",0.);
    obs_data_set_double(settings,"input_db",-6.020599913);
    obs_data_set_double(settings,"output_db",6.020599913);
    obs_source_update(filter,settings);
    auto bypass=measure();
    obs_data_set_double(settings,"mix",1.);
    obs_source_update(filter,settings);
    auto unity=measure();
    obs_data_set_double(settings,"output_db",0.);
    obs_source_update(filter,settings);
    auto trim=measure();
    obs_data_set_double(settings,"input_db",0.);
    obs_source_update(filter,settings);
    obs_data_set_string(settings,"label","amp_mono");
    obs_data_set_double(settings,"port_0",.75);
    obs_source_update(filter,settings);
    auto mono=measure();
    auto* mod_props=obs_source_properties(filter);
    auto* dest=mod_props?obs_properties_get(mod_props,"react_target"):nullptr;
    std::string port;
    if(dest)for(size_t i=0;i<obs_property_list_item_count(dest);++i){
        const char* v=obs_property_list_item_string(dest,i);
        if(v&&std::string(v).ends_with("\tamp_mono\t0"))port=v;
    }
    bool react_ok=!port.empty();
    if(mod_props)obs_properties_destroy(mod_props);
    obs_data_set_double(settings,"port_0",.25);
    obs_data_set_int(settings,"react_source",3);
    obs_data_set_string(settings,"react_target",port.c_str());
    obs_data_set_double(settings,"react_amount",.5);
    obs_data_set_double(settings,"react_sensitivity",6.);
    obs_source_update(filter,settings);
    auto bass=measure(100,60);
    // Second route shares the first destination, third drives the mixer.
    obs_data_set_int(settings,"react2_source",3);
    obs_data_set_string(settings,"react2_target",port.c_str());
    obs_data_set_double(settings,"react2_amount",.25);
    obs_data_set_double(settings,"react2_sensitivity",6.);
    obs_data_set_int(settings,"react3_source",3);
    obs_data_set_string(settings,"react3_target","output");
    obs_data_set_double(settings,"react3_amount",-.5);
    obs_data_set_double(settings,"react3_sensitivity",6.);
    obs_source_update(filter,settings);
    auto simultaneous=measure(100,60);
    // Disable only route 3; the two same-destination routes remain summed.
    obs_data_set_int(settings,"react3_source",0);
    obs_source_update(filter,settings);
    auto summed=measure(100,60);
    // The fourth slot cancels the second route at the same destination.
    obs_data_set_int(settings,"react4_source",3);
    obs_data_set_string(settings,"react4_target",port.c_str());
    obs_data_set_double(settings,"react4_amount",-.25);
    obs_data_set_double(settings,"react4_sensitivity",6.);
    obs_source_update(filter,settings);
    auto canceled=measure(100,60);
    obs_data_set_int(settings,"react4_source",0);
    obs_data_set_int(settings,"react2_source",0);
    obs_source_update(filter,settings);
    auto restored=measure(100,60);
    auto* matrix_props=obs_source_properties(filter);
    bool matrix_ok=matrix_props&&obs_properties_get(matrix_props,"react2_target")&&
        obs_properties_get(matrix_props,"react3_target")&&obs_properties_get(matrix_props,"react4_target");
    if(matrix_props)obs_properties_destroy(matrix_props);
    matrix_ok=matrix_ok&&std::abs(canceled[0]-bass[0])<.002&&summed[0]>bass[0]*1.2&&simultaneous[0]<summed[0]*.8&&
        simultaneous[0]>.1&&std::abs(restored[0]-bass[0])<.002&&
        std::abs(summed[1]/summed[0]-.4)<.001;
    std::printf("LADSPA_MATRIX single=%g combined=%g summed=%g canceled=%g restored=%g pass=%d\n",
       bass[0],simultaneous[0],summed[0],canceled[0],restored[0],matrix_ok);

    auto reject=measure(9000,100);
    obs_data_set_string(settings,"label","amp_stereo");
    obs_source_update(filter,settings);
    auto stale=measure(100,20);
    auto* stale_props=obs_source_properties(filter);
    auto* stale_dest=stale_props?obs_properties_get(stale_props,"react_target"):nullptr;
    bool stale_visible=false;
    if(stale_dest)for(size_t i=0;i<obs_property_list_item_count(stale_dest);++i){
        auto* value=obs_property_list_item_string(stale_dest,i);
        auto* name=obs_property_list_item_name(stale_dest,i);
        if(value&&name&&port==value&&std::string(name).starts_with("Unavailable"))stale_visible=true;
    }
    if(stale_props)obs_properties_destroy(stale_props);
    react_ok=react_ok&&stale_visible;
    obs_data_set_string(settings,"react_target","output");
    obs_data_set_double(settings,"react_amount",1.);
    obs_source_update(filter,settings);
    auto output=measure(100,60);
    obs_data_set_int(settings,"react_source",0);
    obs_source_update(filter,settings);
    auto disabled=measure(100,20);
    react_ok=react_ok&&bass[0]>.15&&reject[0]<.08&&
       std::abs(bass[1]/bass[0]-.4)<.001&&
       std::abs(stale[0]-.4/std::sqrt(2.)*.25)<.002&&
       output[0]>.11&&std::abs(disabled[0]-stale[0])<.002;
    std::printf("LADSPA_REACT bass=%g reject=%g stale=%g output=%g disabled=%g pass=%d\n",
       bass[0],reject[0],stale[0],output[0],disabled[0],react_ok);
    obs_register_source(&sound_fixture::info);
    auto* producer=obs_source_create_private("yamnet_classifier_source","classifier",nullptr);
    obs_source_filter_add(source,producer);
    const auto channel=std::string(obs_source_get_uuid(source))+"/"+obs_source_get_uuid(producer);
    const auto stereo_port=std::string("port:")+argv[3]+"\tamp_stereo\t0";
    obs_data_set_int(settings,"react2_source",6);
    obs_data_set_string(settings,"react2_target",stereo_port.c_str());
    obs_data_set_double(settings,"react2_amount",.5);
    obs_data_set_string(settings,"react2_sound_channel",channel.c_str());
    obs_data_set_string(settings,"react2_sound_category","Speech");
    obs_source_update(filter,settings);
    auto external=measure(100,60);
    auto* sound_props=obs_source_properties(filter);
    auto* sound_source=sound_props?obs_properties_get(sound_props,"react2_source"):nullptr;
    auto* sound_choice=sound_props?obs_properties_get(sound_props,"react2_sound_channel"):nullptr;
    if(sound_source)obs_property_modified(sound_source,settings);
    bool picker_ok=false;
    if(sound_choice)for(size_t i=0;i<obs_property_list_item_count(sound_choice);++i){
        const auto* v=obs_property_list_item_string(sound_choice,i);
        if(v&&channel==v)picker_ok=true;
    }
    if(sound_props)obs_properties_destroy(sound_props);
    sound_fixture::delay_ms=900;
    auto stalled=measure(100,140);
    sound_fixture::delay_ms=0;
    obs_data_set_string(settings,"react2_sound_category","No such category");
    obs_source_update(filter,settings);
    auto wrong=measure(100,60);
    obs_data_set_string(settings,"react2_sound_category","Speech");
    obs_source_update(filter,settings);
    auto recovered=measure(100,140);
    obs_data_set_int(settings,"react2_source",0);
    obs_source_update(filter,settings);
    obs_source_filter_remove(source,producer);
    obs_source_release(producer);
    bool sound_ok=picker_ok&&sound_fixture::calls>0&&std::abs(external[0]-.141421)<.004&&
        std::abs(stalled[0]-.070711)<.004&&std::abs(wrong[0]-.070711)<.004&&
        std::abs(recovered[0]-.141421)<.004&&std::abs(external[1]/external[0]-.4)<.001;
    std::printf("SOUND_LADSPA_OBS active=%g stalled=%g wrong=%g recovered=%g picker=%d pass=%d\n",
        external[0],stalled[0],wrong[0],recovered[0],picker_ok,sound_ok);
    obs_register_source(&sight_fixture::info);
    obs_register_source(&sight_fixture::video_info);
    obs_register_source(&sight_fixture::motion_info);
    auto* video=obs_source_create("hyperdsp_test_video","camera fixture",nullptr,nullptr);
    auto* motion=obs_source_create_private("sight_u2net_filter","motion fixture",nullptr);
    obs_source_filter_add(video,motion);
    const auto motion_channel=std::string(obs_source_get_uuid(video))+"/"+obs_source_get_uuid(motion);
    obs_data_set_int(settings,"react2_source",7);
    obs_data_set_string(settings,"react2_sight_channel",motion_channel.c_str());
    obs_source_update(filter,settings);
    auto movement=measure(100,60);
    sight_fixture::bad_abi=true;
    auto wrong_dimensions=measure(100,60);
    sight_fixture::bad_abi=false;
    obs_data_set_int(settings,"react2_source",8);
    obs_data_set_string(settings,"react2_sight_channel","test-hand");
    obs_source_update(filter,settings);
    auto tracking=measure(100,60);
    auto* sight_props=obs_source_properties(filter);
    auto* sight_src=sight_props?obs_properties_get(sight_props,"react2_source"):nullptr;
    if(sight_src)obs_property_modified(sight_src,settings);
    auto* sight_choice=sight_props?obs_properties_get(sight_props,"react2_sight_channel"):nullptr;
    bool sight_picker=false;
    if(sight_choice)for(size_t i=0;i<obs_property_list_item_count(sight_choice);++i){
        const char* v=obs_property_list_item_string(sight_choice,i);
        if(v&&std::string(v)=="test-hand")sight_picker=true;
    }
    if(sight_props)obs_properties_destroy(sight_props);
    obs_data_set_int(settings,"react2_source",7);
    obs_data_set_string(settings,"react2_sight_channel",motion_channel.c_str());
    obs_source_update(filter,settings);
    obs_source_filter_remove(video,motion);obs_source_release(motion);obs_source_release(video);
    auto motion_missing=measure(100,60);
    obs_data_set_int(settings,"react2_source",0);
    obs_source_update(filter,settings);
    bool sight_ok=sight_picker&&sight_fixture::motion_calls>0&&sight_fixture::calls>0&&
        std::abs(movement[0]-.127279)<.005&&std::abs(wrong_dimensions[0]-.070711)<.005&&
        std::abs(tracking[0]-.176777)<.005&&std::abs(motion_missing[0]-.070711)<.005;
    std::printf("SIGHT_LADSPA_OBS motion=%g invalid=%g tracking=%g missing=%g picker=%d pass=%d\n",
        movement[0],wrong_dimensions[0],tracking[0],motion_missing[0],sight_picker,sight_ok);
    obs_data_set_double(settings,"output_db",12.);
    obs_data_set_string(settings,"label","unknown_label");
    obs_source_update(filter,settings);
    auto unknown=measure();
    obs_data_set_string(settings,"library","/nonexistent/hyperdsp.so");
    obs_source_update(filter,settings);
    auto dry=measure();
    auto near=[](double a,double b){return std::abs(a-b)<.005;};
    bool pass=sight_ok && sound_ok && matrix_ok && react_ok && prop_ok && browser_ok && lfo_ok && near(quiet[0],.4/std::sqrt(2.)*.25) && near(quiet[1],.16/std::sqrt(2.)*.25)
        && near(louder[0],quiet[0]*2) && near(louder[1],quiet[1]*2)
        && near(blend[0],.4/std::sqrt(2.)*.75) && near(blend[1],.16/std::sqrt(2.)*.75)
        && near(bypass[0],.4/std::sqrt(2.)) && near(bypass[1],.16/std::sqrt(2.))
        && near(unity[0],louder[0]) && near(unity[1],louder[1])
        && near(trim[0],louder[0]*.5) && near(trim[1],louder[1]*.5)
        && near(mono[0],.4/std::sqrt(2.)*.75) && near(mono[1],.16/std::sqrt(2.)*.75)
        && near(unknown[0],.4/std::sqrt(2.)) && near(unknown[1],.16/std::sqrt(2.))
        && near(dry[0],.4/std::sqrt(2.)) && near(dry[1],.16/std::sqrt(2.));
    std::printf("LFO_OBS rms_ratio=%g,%g pass=%d\n",lfo_l/louder[0],lfo_r/louder[1],lfo_ok);
    std::printf("MIX_OBS blend=%g,%g bypass=%g,%g unity=%g,%g trim=%g,%g\n",blend[0],blend[1],bypass[0],bypass[1],unity[0],unity[1],trim[0],trim[1]);
    std::printf("LADSPA_OBS_ACTUAL quiet=%g,%g louder=%g,%g mono=%g,%g unknown=%g,%g dry=%g,%g controls=%d browser=%d pass=%d\n",
        quiet[0],quiet[1],louder[0],louder[1],mono[0],mono[1],unknown[0],unknown[1],dry[0],dry[1],prop_ok,browser_ok,pass);
    obs_set_output_source(1,nullptr);
    obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);
    auto* history=obs_data_create();
    obs_data_set_string(history,"library",argv[3]);obs_data_set_string(history,"label","amp_stereo");obs_data_set_double(history,"port_0",.25);
    // The SDK mono and stereo amplifiers have the same gain contract.
    bool continuous=history_contention::verify(source,"hyperdsp_ladspa_filter",history,"lfo_rate",1,2);
    bool replacement=history_contention::verify(source,"hyperdsp_ladspa_filter",history,"lfo_rate",1,2,"amp_mono");
    obs_data_release(history);pass=pass&&continuous&&replacement;
    obs_source_release(source);obs_data_release(settings);obs_shutdown();
    return pass?0:1;
}
