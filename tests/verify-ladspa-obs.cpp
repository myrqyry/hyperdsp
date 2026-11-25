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
static void stream(obs_source_t* source, int rounds) {
    static constexpr uint32_t FRAMES=480;
    std::array<float, FRAMES> l{},r{};
    uint64_t start=os_gettime_ns();
    for (int k=0;k<rounds;++k) {
        for (unsigned i=0;i<FRAMES;++i) {
            const float phase=float((uint64_t(k)*FRAMES+i)*6.283185307179586*9000.0/48000.0);
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
    bool prop_ok=props && obs_properties_get(props,"port_0");
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
    auto measure=[&] {
        Stats stats;
        obs_source_add_audio_capture_callback(source,capture,&stats);
        stream(source,40);
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
    obs_data_set_string(settings,"label","amp_mono");
    obs_data_set_double(settings,"port_0",.75);
    obs_source_update(filter,settings);
    auto mono=measure();
    obs_data_set_string(settings,"label","unknown_label");
    obs_source_update(filter,settings);
    auto unknown=measure();
    obs_data_set_string(settings,"library","/nonexistent/hyperdsp.so");
    obs_source_update(filter,settings);
    auto dry=measure();
    auto near=[](double a,double b){return std::abs(a-b)<.005;};
    bool pass=prop_ok && browser_ok && near(quiet[0],.4/std::sqrt(2.)*.25) && near(quiet[1],.16/std::sqrt(2.)*.25)
        && near(louder[0],quiet[0]*2) && near(louder[1],quiet[1]*2)
        && near(mono[0],.4/std::sqrt(2.)*.75) && near(mono[1],.16/std::sqrt(2.)*.75)
        && near(unknown[0],.4/std::sqrt(2.)) && near(unknown[1],.16/std::sqrt(2.))
        && near(dry[0],.4/std::sqrt(2.)) && near(dry[1],.16/std::sqrt(2.));
    std::printf("LADSPA_OBS_ACTUAL quiet=%g,%g louder=%g,%g mono=%g,%g unknown=%g,%g dry=%g,%g controls=%d browser=%d pass=%d\n",
        quiet[0],quiet[1],louder[0],louder[1],mono[0],mono[1],unknown[0],unknown[1],dry[0],dry[1],prop_ok,browser_ok,pass);
    obs_set_output_source(1,nullptr);
    obs_source_dec_active(source);obs_source_filter_remove(source,filter);
    obs_source_release(filter);obs_source_release(source);obs_data_release(settings);obs_shutdown();
    return pass?0:1;
}
