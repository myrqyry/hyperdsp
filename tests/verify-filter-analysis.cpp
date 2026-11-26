#include "framework/dsp/node_telemetry.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QTest>
#include <QPointer>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QWheelEvent>
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
        bool prior=ok;ok=true;auto* s=obs_data_create();obs_data_set_double(s,"frequency",1500);obs_data_set_double(s,"threshold",-30);obs_data_set_double(s,"range_db",-9);
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
            if(n.kind==TelemetryKind::DynamicEq){
                auto* plot=dialog->findChild<QWidget*>("filterAnalysisPlot");
                auto* freq=dialog->findChild<QDoubleSpinBox*>("eqFrequency");
                auto* depth=dialog->findChild<QDoubleSpinBox*>("eqRange");
                auto* q=dialog->findChild<QDoubleSpinBox*>("eqQ");
                ok=ok&&plot&&freq&&depth&&q;
                if(plot&&freq&&depth&&q){
                    QRectF area(42,24,plot->width()-51,plot->height()-65);
                    auto point=[&](double hz,double db){return QPointF(area.left()+area.width()*std::log(hz/20)/std::log(1000.),area.top()+area.height()*(12-db)/36).toPoint();};
                    auto before=point(1500,-9),after=point(3000,-15);
                    QTest::mousePress(plot,Qt::LeftButton,Qt::NoModifier,before);
                    QTest::mouseMove(plot,after,40);QTest::qWait(60);
                    auto* live=obs_source_get_settings(f);
                    bool moving=std::abs(obs_data_get_double(live,"frequency")-3000)<100&&std::abs(obs_data_get_double(live,"range_db")+15)<.5;
                    obs_data_release(live);ok=ok&&moving;
                    QTest::mouseRelease(plot,Qt::LeftButton,Qt::NoModifier,after);
                    QWheelEvent wheel(QPointF(after),plot->mapToGlobal(after),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                    QApplication::sendEvent(plot,&wheel);QTest::qWait(80);
                    live=obs_source_get_settings(f);std::printf("EQ_WHEEL q=%g threshold=%g\n",obs_data_get_double(live,"q"),obs_data_get_double(live,"threshold"));ok=ok&&obs_data_get_double(live,"q")>2&&obs_data_get_double(live,"threshold")==-30;obs_data_release(live);
                    // Configure numerically and confirm a separate settings edit is reflected.
                    freq->setValue(1500);depth->setValue(-12);q->setValue(2);QTest::qWait(90);
                    stream();QTest::qWait(70);bool got=read();std::printf("EQ_NUMERIC ready=%d gain=%g freq=%g q=%g\n",got,frame.node.gain_db[0],frame.node.frequency,frame.node.q);ok=ok&&got&&std::abs(frame.node.gain_db[0]+12)<.02;
                    live=obs_source_get_settings(f);obs_data_set_double(live,"frequency",1800);obs_source_update(f,live);obs_data_release(live);
                    QTest::qWait(80);std::printf("EQ_EXTERNAL freq=%g\n",freq->value());ok=ok&&freq->value()==1800;
                    auto* line=freq->findChild<QLineEdit*>();
                    ok=ok&&line;
                    if(line){
                        freq->setFocus();line->selectAll();QTest::keyClicks(line,"2200");
                        QTest::qWait(100);ok=ok&&line->text().contains("2200");
                        QTest::keyClick(line,Qt::Key_Return);QTest::qWait(80);
                        if(!dialog){std::fprintf(stderr,"EQ_EDITOR dialog closed on numeric Enter\n");return 1;}
                        live=obs_source_get_settings(f);ok=ok&&obs_data_get_double(live,"frequency")==2200;obs_data_release(live);
                    }
                    // Editing remains possible without fresh audio.
                    QTest::qWait(550);depth->setValue(-6);QTest::qWait(80);
                    live=obs_source_get_settings(f);std::printf("EQ_STALE_EDIT range=%g\n",obs_data_get_double(live,"range_db"));ok=ok&&obs_data_get_double(live,"range_db")==-6;obs_data_release(live);
                    freq->setValue(1500);depth->setValue(-9);QTest::qWait(80);
                    std::printf("EQ_EDITOR drag_live=%d frequency=%g range=%g q=%g pass=%d\n",moving,freq->value(),depth->value(),q->value(),ok);
                }
            }
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
        std::printf("FILTER_ANALYSIS %s gain=%g/%g pass=%d\n",id,n.gain_db[0],n.gain_db[1],ok);ok=ok&&prior;
    }
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
