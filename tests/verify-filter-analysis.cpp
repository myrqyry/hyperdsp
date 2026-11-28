#include "framework/dsp/node_telemetry.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <QApplication>
#include <QLocale>
#include <QDialog>
#include <QLabel>
#include "widget-events.hpp"
#include <QPointer>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QWheelEvent>
#include <array>
#include <atomic>
#include <thread>
#include <numbers>
#include <cstdio>
using namespace hyperdsp;
static const char* name(void*){return "Analysis test";}
static void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void destroy(void*){}
int main(int argc,char** argv){
    QLocale::setDefault(QLocale::c());
    QApplication app(argc,argv);
    WidgetEvents widget_events;
    if(argc!=3||!obs_startup("en-US",nullptr,nullptr))return 2;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 3;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 4;
    obs_source_info fixture{.id="analysis_test_input",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&fixture);
    auto* source=obs_source_create("analysis_test_input","Analysis test",nullptr,nullptr);
    bool ok=true;unsigned samples=0;auto start=os_gettime_ns();
    for(auto id:{"hyperdsp_dynamic_eq_filter","hyperdsp_dynamics_filter","hyperdsp_multiband_filter","hyperdsp_transient_filter"}){
        bool prior=ok;ok=true;auto* s=obs_data_create();obs_data_set_double(s,"frequency",1500);obs_data_set_double(s,"threshold",-30);obs_data_set_double(s,"range_db",-9);
        obs_data_set_double(s,"mid_threshold",-30);obs_data_set_double(s,"mid_ratio",4);obs_data_set_double(s,"low_ratio",1);obs_data_set_double(s,"high_ratio",1);
        bool transient=std::string_view(id)=="hyperdsp_transient_filter";
        if(transient){obs_data_set_int(s,"mode",2);obs_data_set_double(s,"mid_attack_db",12);obs_data_set_double(s,"mid_sustain_db",-12);}
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
        widget_events.qWait(60);
        QPointer<QDialog> dialog;int count=0;
        for(auto* w:QApplication::topLevelWidgets())if(w->objectName()=="hyperdspFilterAnalysis"){dialog=qobject_cast<QDialog*>(w);++count;}
        ok=ok&&count==1&&dialog&&dialog->isVisible();
        auto stream=[&](unsigned blocks=100,double amplitude=.4){
            std::array<float,480> l{},r{};
            for(unsigned k=0;k<blocks;++k){
                for(unsigned j=0;j<480;++j){l[j]=amplitude*std::sin(2*std::numbers::pi*1500*(samples+j)/48000);r[j]=-l[j];}
                obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();a.frames=480;
                a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
                a.timestamp=start+uint64_t(samples)*1000000000ULL/48000;obs_source_output_audio(source,&a);samples+=480;
            }
        };
        stream();widget_events.qWait(80);ok=ok&&read()&&frame.rate==48000&&frame.node.kind!=TelemetryKind::None;
        auto n=frame.node;
        std::atomic<bool> telemetry_done{false},telemetry_ok{true};
        std::atomic<unsigned> telemetry_reads{0},telemetry_ready{0};
        std::array<std::thread,2> telemetry_readers;
        for(auto& reader:telemetry_readers)reader=std::thread([&]{
            uint64_t previous=0;++telemetry_ready;
            do{
                FilterTelemetryFrame snapshot{};
                calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&snapshot);
                bool ready=proc_handler_call(obs_source_get_proc_handler(f),"hyperdsp_filter_telemetry_v1",&d)&&calldata_bool(&d,"available");
                calldata_free(&d);
                if(!ready)continue;
                if(snapshot.rate!=48000||snapshot.node.kind!=n.kind||snapshot.stamp<previous||snapshot.epoch!=frame.epoch)telemetry_ok=false;
                for(float v:snapshot.node.peak)if(!std::isfinite(v))telemetry_ok=false;
                for(float v:snapshot.node.gain_db)if(!std::isfinite(v))telemetry_ok=false;
                previous=snapshot.stamp;++telemetry_reads;
            }while(!telemetry_done.load());
        });
        while(telemetry_ready.load()!=telemetry_readers.size())std::this_thread::yield();
        stream(300);
        telemetry_done=true;for(auto& reader:telemetry_readers)reader.join();
        bool concurrent_telemetry=telemetry_ok&&telemetry_reads>0;
        std::printf("TELEMETRY_CONCURRENT id=%s reads=%u pass=%d\n",id,telemetry_reads.load(),concurrent_telemetry);
        ok=ok&&concurrent_telemetry;

        if(n.kind==TelemetryKind::DynamicEq)ok=ok&&std::abs(n.gain_db[0]+9)<.02;
        else if(n.kind==TelemetryKind::Dynamics)ok=ok&&n.gain_db[0]<-10;
        else if(n.kind==TelemetryKind::Multiband)ok=ok&&n.gain_db[1]<-10&&n.gain_db[0]==0&&n.gain_db[2]==0;
        if(dialog){
            auto* status=dialog->findChild<QLabel*>("filterAnalysisStatus");ok=ok&&status&&status->text().startsWith("Live");
            dialog->resize(360,300);widget_events.qWait(30);
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
                    widget_events.mousePress(plot,Qt::LeftButton,Qt::NoModifier,before);
                    widget_events.mouseMove(plot,after,40);widget_events.qWait(60);
                    auto* live=obs_source_get_settings(f);
                    bool moving=std::abs(obs_data_get_double(live,"frequency")-3000)<100&&std::abs(obs_data_get_double(live,"range_db")+15)<.5;
                    obs_data_release(live);ok=ok&&moving;
                    widget_events.mouseRelease(plot,Qt::LeftButton,Qt::NoModifier,after);
                    QWheelEvent wheel(QPointF(after),plot->mapToGlobal(after),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                    QApplication::sendEvent(plot,&wheel);widget_events.qWait(80);
                    live=obs_source_get_settings(f);std::printf("EQ_WHEEL q=%g threshold=%g\n",obs_data_get_double(live,"q"),obs_data_get_double(live,"threshold"));ok=ok&&obs_data_get_double(live,"q")>2&&obs_data_get_double(live,"threshold")==-30;obs_data_release(live);
                    // Configure numerically and confirm a separate settings edit is reflected.
                    freq->setValue(1500);depth->setValue(-12);q->setValue(2);widget_events.qWait(90);
                    stream();widget_events.qWait(70);bool got=read();std::printf("EQ_NUMERIC ready=%d gain=%g freq=%g q=%g\n",got,frame.node.gain_db[0],frame.node.frequency,frame.node.q);ok=ok&&got&&std::abs(frame.node.gain_db[0]+12)<.02;
                    live=obs_source_get_settings(f);obs_data_set_double(live,"frequency",1800);obs_source_update(f,live);obs_data_release(live);
                    widget_events.qWait(80);std::printf("EQ_EXTERNAL freq=%g\n",freq->value());ok=ok&&freq->value()==1800;
                    auto* line=freq->findChild<QLineEdit*>();
                    ok=ok&&line;
                    if(line){
                        freq->setFocus();line->selectAll();widget_events.keyClicks(line,"2200");
                        widget_events.qWait(100);
                        std::printf("EQ_TEXT entered=%s focus=%d\n",qPrintable(line->text()),line->hasFocus());
                        ok=ok&&line->text().contains("2200");
                        widget_events.keyClick(line,Qt::Key_Return);widget_events.qWait(80);
                        if(!dialog){std::fprintf(stderr,"EQ_EDITOR dialog closed on numeric Enter\n");return 1;}
                        live=obs_source_get_settings(f);std::printf("EQ_COMMIT frequency=%g\n",obs_data_get_double(live,"frequency"));ok=ok&&obs_data_get_double(live,"frequency")==2200;obs_data_release(live);
                    }
                    // Editing remains possible without fresh audio.
                    widget_events.qWait(550);depth->setValue(-6);widget_events.qWait(80);
                    live=obs_source_get_settings(f);std::printf("EQ_STALE_EDIT range=%g\n",obs_data_get_double(live,"range_db"));ok=ok&&obs_data_get_double(live,"range_db")==-6;obs_data_release(live);
                    freq->setValue(1500);depth->setValue(-9);widget_events.qWait(80);
                    std::printf("EQ_EDITOR drag_live=%d frequency=%g range=%g q=%g pass=%d\n",moving,freq->value(),depth->value(),q->value(),ok);
                }
            }
            if(n.kind==TelemetryKind::Multiband){
                auto* crossover=dialog->findChild<QWidget*>("bandCrossover");
                auto* low=dialog->findChild<QDoubleSpinBox*>("bandLowHz");
                auto* high=dialog->findChild<QDoubleSpinBox*>("bandHighHz");
                auto* band=dialog->findChild<QComboBox*>("bandSelect");
                auto* ratio=dialog->findChild<QDoubleSpinBox*>("bandRatio");
                auto* threshold=dialog->findChild<QDoubleSpinBox*>("bandThreshold");
                auto* makeup=dialog->findChild<QDoubleSpinBox*>("bandMakeup");
                ok=ok&&crossover&&low&&high&&band&&ratio&&threshold&&makeup;
                if(crossover&&low&&high&&band&&ratio&&threshold&&makeup){
                    auto* editor=crossover->parentWidget();
                    int bottom=crossover->geometry().bottom(),spinTop=low->mapTo(editor,QPoint(0,0)).y();
                    std::printf("BAND_LAYOUT window=%dx%d editor=%dx%d crossover_bottom=%d spin_top=%d\n",dialog->width(),dialog->height(),editor->width(),editor->height(),bottom,spinTop);
                    auto* footer=dialog->findChild<QLabel*>("filterAnalysisDetail");
                    ok=ok&&spinTop-bottom>=20&&footer&&footer->geometry().top()>editor->geometry().bottom();
                    auto point=[&](double hz){return QPointF(12+(crossover->width()-24)*std::log(hz/20)/std::log(1000.),32).toPoint();};
                    auto from=point(250),to=point(400);
                    widget_events.mousePress(crossover,Qt::LeftButton,Qt::NoModifier,from);
                    widget_events.mouseMove(crossover,to,40);widget_events.qWait(90);
                    auto* live=obs_source_get_settings(f);bool deferred=obs_data_get_double(live,"low_hz")==250;obs_data_release(live);
                    widget_events.mouseRelease(crossover,Qt::LeftButton,Qt::NoModifier,to);widget_events.qWait(80);
                    live=obs_source_get_settings(f);double moved=obs_data_get_double(live,"low_hz");obs_data_release(live);
                    ok=ok&&deferred&&std::abs(moved-400)<15;
                    low->setValue(250);high->setValue(3500);widget_events.qWait(80);
                    band->setCurrentIndex(1);ratio->setValue(1);widget_events.qWait(80);
                    stream();widget_events.qWait(80);bool neutral=read()&&std::abs(frame.node.gain_db[1])<.01&&std::abs(frame.node.high_hz-3500)<.1;
                    band->setCurrentIndex(0);threshold->setValue(-42);makeup->setValue(3);widget_events.qWait(80);
                    live=obs_source_get_settings(f);
                    ok=ok&&neutral&&obs_data_get_double(live,"low_threshold")==-42&&obs_data_get_double(live,"low_makeup")==3&&obs_data_get_double(live,"mid_threshold")==-30&&obs_data_get_double(live,"high_ratio")==1;
                    obs_data_set_double(live,"high_hz",4000);obs_source_update(f,live);obs_data_release(live);
                    widget_events.qWait(80);ok=ok&&high->value()==4000;
                    // Click a region while a numeric field is being edited.
                    auto* entry=threshold->findChild<QLineEdit*>();
                    if(entry){
                        threshold->setFocus();entry->selectAll();widget_events.keyClicks(entry,"-36");
                        widget_events.mouseClick(crossover,Qt::LeftButton,Qt::NoModifier,point(1000));
                        widget_events.qWait(80);
                        live=obs_source_get_settings(f);
                        std::printf("BAND_COMMIT band=%d low=%g mid=%g\n",band->currentIndex(),obs_data_get_double(live,"low_threshold"),obs_data_get_double(live,"mid_threshold"));
                        ok=ok&&band->currentIndex()==1&&obs_data_get_double(live,"low_threshold")==-36&&obs_data_get_double(live,"mid_threshold")==-30;
                        obs_data_release(live);
                    }else ok=false;
                    // A drag cancelled by hiding the window must not commit.
                    widget_events.mousePress(crossover,Qt::LeftButton,Qt::NoModifier,point(4000));
                    widget_events.mouseMove(crossover,point(6000),40);dialog->hide();widget_events.qWait(80);dialog->show();widget_events.qWait(80);
                    live=obs_source_get_settings(f);ok=ok&&obs_data_get_double(live,"high_hz")==4000;obs_data_release(live);
                    band->setCurrentIndex(0);
                    // Restore the original compressor settings.
                    high->setValue(2500);threshold->setValue(-18);makeup->setValue(0);band->setCurrentIndex(1);ratio->setValue(4);widget_events.qWait(80);
                    stream();widget_events.qWait(80);bool restored=read();ok=ok&&restored&&frame.node.gain_db[1]<-10;
                    std::printf("BAND_EDITOR deferred=%d moved=%g neutral=%d restored_gain=%g pass=%d\n",deferred,moved,neutral,frame.node.gain_db[1],ok);
                }
            }
            if(transient){
                auto* crossover=dialog->findChild<QWidget*>("bandCrossover");
                auto* band=dialog->findChild<QComboBox*>("bandSelect");
                auto* attack=dialog->findChild<QDoubleSpinBox*>("bandAttack");
                auto* sustain=dialog->findChild<QDoubleSpinBox*>("bandSustain");
                ok=ok&&n.kind==TelemetryKind::BandTransient&&crossover&&band&&attack&&sustain;
                if(crossover&&band&&attack&&sustain){
                    band->setCurrentIndex(1);ok=ok&&attack->value()==12&&sustain->value()==-12;
                    stream(5,.04);widget_events.qWait(60);bool decay=read();
                    ok=ok&&decay&&frame.node.peak[1]<-.4&&frame.node.gain_db[1]<-4;
                    double decayGain=frame.node.gain_db[1];
                    if(qEnvironmentVariableIsSet("HYPERDSP_FILTER_SCREENSHOT"))dialog->grab().save(qEnvironmentVariable("HYPERDSP_FILTER_SCREENSHOT")+".transient-decay.png");
                    sustain->setValue(6);widget_events.qWait(60);stream(5,.4);stream(5,.04);
                    bool changed=read();double boosted=frame.node.gain_db[1];ok=ok&&changed&&frame.node.peak[1]<-.2&&boosted>1;
                    auto* settings=obs_source_get_settings(f);
                    ok=ok&&obs_data_get_double(settings,"mid_sustain_db")==6&&obs_data_get_double(settings,"low_sustain_db")==0;
                    obs_data_release(settings);
                    auto pt=[&](double hz){return QPointF(12+(crossover->width()-24)*std::log(hz/20)/std::log(1000.),32).toPoint();};
                    widget_events.mousePress(crossover,Qt::LeftButton,Qt::NoModifier,pt(2500));widget_events.mouseMove(crossover,pt(4000),40);widget_events.qWait(80);
                    settings=obs_source_get_settings(f);bool deferred=obs_data_get_double(settings,"high_hz")==2500;obs_data_release(settings);
                    widget_events.mouseRelease(crossover,Qt::LeftButton,Qt::NoModifier,pt(4000));widget_events.qWait(60);
                    settings=obs_source_get_settings(f);double high=obs_data_get_double(settings,"high_hz");obs_data_release(settings);
                    ok=ok&&deferred&&std::abs(high-4000)<100;
                    auto mode=[&](int value){auto* patch=obs_data_create();obs_data_set_int(patch,"mode",value);obs_source_update(f,patch);obs_data_release(patch);widget_events.qWait(70);};
                    mode(1);ok=ok&&!crossover->isVisible()&&!read();
                    auto* broadAttack=dialog->findChild<QDoubleSpinBox*>("transientAttack");
                    auto* broadSustain=dialog->findChild<QDoubleSpinBox*>("transientSustain");
                    auto* attackTime=dialog->findChild<QDoubleSpinBox*>("transientAttackTime");
                    auto* sustainTime=dialog->findChild<QDoubleSpinBox*>("transientSustainTime");
                    ok=ok&&broadAttack&&broadSustain&&attackTime&&sustainTime;
                    if(broadAttack&&broadSustain&&attackTime&&sustainTime){
                        ok=ok&&broadAttack->isVisible()&&attackTime->isVisible();
                        broadAttack->setValue(9);broadSustain->setValue(-6);
                        attackTime->setValue(40);sustainTime->setValue(220);
                        stream(1);ok=ok&&read()&&frame.node.kind==TelemetryKind::Transient&&frame.node.gain_db[0]>4;
                        settings=obs_source_get_settings(f);
                        ok=ok&&obs_data_get_double(settings,"attack_db")==9&&obs_data_get_double(settings,"mid_attack_db")==12&&obs_data_get_double(settings,"attack_ms")==40;
                        obs_data_release(settings);
                        mode(2);ok=ok&&!broadAttack->isVisible()&&attackTime->isVisible()&&attackTime->value()==40&&sustainTime->value()==220;
                        mode(0);ok=ok&&!attackTime->isVisible()&&!broadAttack->isVisible()&&!crossover->isVisible();
                    }
                    mode(2);ok=ok&&crossover->isVisible()&&sustain->value()==6;
                    if(qEnvironmentVariableIsSet("HYPERDSP_FILTER_SCREENSHOT"))dialog->grab().save(qEnvironmentVariable("HYPERDSP_FILTER_SCREENSHOT")+".transient-edited.png");
                    std::printf("TRANSIENT_EDITOR decay=%g boost=%g changed=%d deferred=%d high=%g pass=%d\n",decayGain,boosted,changed,deferred,high,ok);
                }
            }
            dialog->hide();widget_events.qWait(1050);
            // A hidden window stops renewing its capture lease. Sending audio
            // before the next read must not produce a fresh snapshot.
            stream();ok=ok&&!read();
            dialog->show();stream();widget_events.qWait(60);ok=ok&&read();
        }
        frame.size=0;ok=ok&&!read();frame.size=sizeof(frame);
        obs_data_set_int(s,"mode",0);obs_source_update(f,s);ok=ok&&!read();stream();ok=ok&&!read();
        obs_data_set_int(s,"mode",1);obs_source_update(f,s);read();stream();ok=ok&&read();
        obs_source_set_enabled(f,false);ok=ok&&!read();obs_source_set_enabled(f,true);
        stream();widget_events.qWait(550);ok=ok&&!read();
        obs_properties_destroy(props);obs_source_filter_remove(source,f);obs_source_release(f);obs_data_release(s);
        widget_events.qWait(150);ok=ok&&!dialog;
        std::printf("FILTER_ANALYSIS %s gain=%g/%g pass=%d\n",id,n.gain_db[0],n.gain_db[1],ok);ok=ok&&prior;
    }
    obs_source_release(source);obs_shutdown();return ok?0:1;
}
