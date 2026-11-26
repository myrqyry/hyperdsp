#include "framework/dsp/node_telemetry.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QTest>
#include <QPointer>
#include <array>
#include <numbers>
#include <cstdio>
using namespace hyperdsp;
static const char* name(void*){return "Analysis test";}
static void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void destroy(void*){}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_source_info fixture{.id="analysis_test_input",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&fixture);
    auto* source=obs_source_create("analysis_test_input","Analysis test",nullptr,nullptr);
    bool ok=true;unsigned samples=0;auto start=os_gettime_ns();
    for(auto id:{"hyperdsp_dynamic_eq_filter","hyperdsp_dynamics_filter","hyperdsp_multiband_filter"}){
        auto* s=obs_data_create();obs_data_set_double(s,"frequency",1500);obs_data_set_double(s,"threshold",-30);obs_data_set_double(s,"range_db",-9);
        obs_data_set_double(s,"mid_threshold",-30);obs_data_set_double(s,"mid_ratio",4);obs_data_set_double(s,"low_ratio",1);obs_data_set_double(s,"high_ratio",1);
        auto* f=obs_source_create_private(id,"Test filter",s);obs_source_filter_add(source,f);
        FilterTelemetryFrame frame{};
        auto read=[&](){
            calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&frame);
            bool ready=proc_handler_call(obs_source_get_proc_handler(f),"hyperdsp_filter_telemetry_v1",&d)&&calldata_bool(&d,"available");calldata_free(&d);return ready;
        };
        ok=ok&&!read();
        auto* props=obs_source_properties(f);auto* button=obs_properties_get(props,"open_analysis");
        ok=ok&&button;
        if(button){obs_property_button_clicked(button,nullptr);obs_property_button_clicked(button,nullptr);}
        QTest::qWait(60);
        QPointer<QDialog> dialog;int count=0;
        for(auto* w:QApplication::topLevelWidgets())if(w->objectName()=="hyperdspFilterAnalysis"){dialog=qobject_cast<QDialog*>(w);++count;}
        ok=ok&&count==1&&dialog&&dialog->isVisible();
        auto stream=[&](){
            std::array<float,480> l{},r{};
            for(unsigned k=0;k<100;++k){
                for(unsigned j=0;j<480;++j){l[j]=.4f*std::sin(2*std::numbers::pi*1500*(samples+j)/48000);r[j]=-l[j];}
                obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
                a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
                a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
            }
        };
        stream();QTest::qWait(80);ok=ok&&read()&&frame.rate==48000&&frame.node.kind!=TelemetryKind::None;
        auto n=frame.node;
        if(n.kind==TelemetryKind::DynamicEq)ok=ok&&std::abs(n.gain_db[0]+9)<.02;
        else if(n.kind==TelemetryKind::Dynamics)ok=ok&&n.gain_db[0]<-10;
        else if(n.kind==TelemetryKind::Multiband)ok=ok&&n.gain_db[1]<-10&&n.gain_db[0]==0&&n.gain_db[2]==0;
        if(dialog){
            auto* status=dialog->findChild<QLabel*>("filterAnalysisStatus");ok=ok&&status&&status->text().startsWith("Live");
            dialog->resize(360,300);QTest::qWait(30);
            if(qEnvironmentVariableIsSet("HYPERDSP_FILTER_SCREENSHOT"))dialog->grab().save(qEnvironmentVariable("HYPERDSP_FILTER_SCREENSHOT")+"."+id+".png");
            dialog->hide();QTest::qWait(1050);
            // A hidden window stops renewing its capture lease. Sending audio
            // before the next read must not produce a fresh snapshot.
            stream();ok=ok&&!read();
            dialog->show();stream();QTest::qWait(60);ok=ok&&read();
        }
        frame.size=0;ok=ok&&!read();frame.size=sizeof(frame);
        obs_data_set_int(s,"mode",0);obs_source_update(f,s);ok=ok&&!read();stream();ok=ok&&!read();
        obs_data_set_int(s,"mode",1);obs_source_update(f,s);read();stream();ok=ok&&read();
        obs_source_set_enabled(f,false);ok=ok&&!read();obs_source_set_enabled(f,true);
        stream();QTest::qWait(550);ok=ok&&!read();
        obs_properties_destroy(props);obs_source_filter_remove(source,f);obs_source_release(f);obs_data_release(s);
        QTest::qWait(150);ok=ok&&!dialog;
        std::printf("FILTER_ANALYSIS %s gain=%g/%g pass=%d\n",id,n.gain_db[0],n.gain_db[1],ok);
    }
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
