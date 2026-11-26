#include "ui/effect_builder.hpp"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QPalette>
#include <QGraphicsSimpleTextItem>
#include <QTableWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QTest>
#include <QTabWidget>
#include <QJsonObject>
#include <QJsonDocument>
#include <obs-module.h>
#include <cstdio>
#include <util/platform.h>
#include <numbers>
#include "framework/dsp/graph_monitor.hpp"
#include "sound-signal-fixture.hpp"
#include "sight-signal-fixture.hpp"
static obs_module_t* loaded_module=nullptr;
extern "C" obs_module_t* obs_current_module(){return loaded_module;}
static const char* name(void*){return "Test audio";}
static void* create(obs_data_t*,obs_source_t*){return reinterpret_cast<void*>(1);}
static void destroy(void*){}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    QApplication app(argc,argv);
    if(!obs_startup("en-US",nullptr,nullptr))return 3;
    obs_audio_info ai{};ai.samples_per_sec=48000;ai.speakers=SPEAKERS_STEREO;
    if(!obs_reset_audio(&ai))return 4;
    obs_module_t* module=nullptr;
    if(obs_open_module(&module,argv[1],argv[2])!=MODULE_SUCCESS||!obs_init_module(module))return 5;
    loaded_module=module;
    obs_source_info fixture{.id="builder_test_audio",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_AUDIO,.get_name=name,.create=create,.destroy=destroy};
    obs_register_source(&fixture);obs_register_source(&sound_fixture::info);obs_register_source(&sight_fixture::info);
    auto* source=obs_source_create("builder_test_audio","Builder test",nullptr,nullptr);
    auto* filter=obs_source_create_private("hyperdsp_graph_filter","Graph",nullptr);
    obs_source_filter_add(source,filter);
    bool ok=true;
    {
        hyperdsp::EffectBuilder builder;
        builder.resize(880,620);builder.show();QTest::qWait(40);
        auto* target=builder.findChild<QComboBox*>("target");
        auto* search=builder.findChild<QLineEdit*>("search");
        auto* library=builder.findChild<QListWidget*>("library");
        auto* add=builder.findChild<QPushButton*>("addNode");
        auto* apply=builder.findChild<QPushButton*>("applyGraph");
        auto* graph=builder.findChild<QGraphicsView*>("graph");
        ok=target&&search&&library&&add&&apply&&graph&&target->count()==1;
        if(ok){
            search->setText("hard clipping");QApplication::processEvents();
            ok=library->count()==1;
            library->setCurrentRow(0);QTest::mouseClick(add,Qt::LeftButton);
            auto draft=builder.draft();
            ok=ok&&draft.value("nodes").toArray().size()==1;
            auto* drive=builder.findChild<QDoubleSpinBox*>("param_drive");
            ok=ok&&drive;
            if(drive)drive->setValue(12);
            auto* before=obs_source_get_settings(filter);
            ok=ok&&std::string(obs_data_get_string(before,"graph_json")).find("color_filter")==std::string::npos;
            obs_data_release(before);
            auto* validate=builder.findChild<QPushButton*>("validateGraph");
            ok=ok&&validate;
            if(validate)QTest::mouseClick(validate,Qt::LeftButton);
            before=obs_source_get_settings(filter);
            ok=ok&&std::string(obs_data_get_string(before,"graph_json")).find("color_filter")==std::string::npos;obs_data_release(before);
            QTest::mouseClick(apply,Qt::LeftButton);
            auto* after=obs_source_get_settings(filter);
            std::string accepted=obs_data_get_string(after,"graph_json");obs_data_release(after);
            ok=ok&&accepted.find("hyperdsp_color_filter:3")!=std::string::npos&&accepted.find("12")!=std::string::npos;
            auto invalid=builder.draft();invalid.insert("output",9);
            builder.setDraft(invalid);QTest::mouseClick(apply,Qt::LeftButton);
            after=obs_source_get_settings(filter);
            ok=ok&&accepted==obs_data_get_string(after,"graph_json");obs_data_release(after);
            builder.setDraft(draft);
            auto* inputs=builder.findChild<QTableWidget*>("inputs");
            auto* addInput=builder.findChild<QPushButton*>("addInput");
            ok=ok&&inputs&&addInput;
            // Add a second effect and create a parallel branch to its input.
            search->setText("gain weighted");library->setCurrentRow(0);QTest::mouseClick(add,Qt::LeftButton);
            ok=ok&&builder.draft().value("nodes").toArray().size()==2;
            if(addInput)QTest::mouseClick(addInput,Qt::LeftButton);
            ok=ok&&builder.draft().value("nodes").toArray()[1].toObject().value("inputs").toArray().size()==2;
            // Real viewport gestures: move a node, cable from output to input,
            // select a cable and delete, and reject a cycle.
            auto item=[&](const QString& role,int id,int row=-1)->QGraphicsItem*{
                for(auto* i:graph->scene()->items())
                    if(i->data(1).toString()==role&&i->data(0).toInt()==id&&(row<0||i->data(2).toInt()==row))return i;
                return nullptr;
            };
            auto center=[&](QGraphicsItem* i){return graph->mapFromScene(i->sceneBoundingRect().center());};
            auto drag=[&](QPoint from,QPoint to){
                QTest::mousePress(graph->viewport(),Qt::LeftButton,Qt::NoModifier,from);
                QTest::mouseMove(graph->viewport(),to,20);
                QTest::mouseRelease(graph->viewport(),Qt::LeftButton,Qt::NoModifier,to);
                QApplication::processEvents();
            };
            auto* movable=item("node",0);
            auto* inputPort=item("out",-1);auto* toPort=item("in",1);
            bool canvas=movable&&inputPort&&toPort;
            if(canvas){
                auto before=builder.draft();auto pos=movable->pos();auto start=center(movable);
                drag(start,start+QPoint(0,40));
                movable=item("node",0);
                canvas=movable&&movable->pos()!=pos&&builder.draft()==before;
                std::printf("CANVAS_MOVE=%d\n",canvas);
                auto* fit=builder.findChild<QPushButton*>("fitCanvas");if(fit)QTest::mouseClick(fit,Qt::LeftButton);
                auto* path=item("cable",1,1);
                if(path){
                    auto* cable=static_cast<QGraphicsPathItem*>(path);
                    auto point=graph->mapFromScene(cable->path().pointAtPercent(.5));
                    QTest::mouseClick(graph->viewport(),Qt::LeftButton,Qt::NoModifier,point);
                    std::printf("CANVAS_SELECTED=%lld point=%d,%d\n",static_cast<long long>(graph->scene()->selectedItems().size()),point.x(),point.y());
                    QTest::keyClick(graph,Qt::Key_Delete);QApplication::processEvents();
                }else canvas=false;
                canvas=canvas&&builder.draft().value("nodes").toArray()[1].toObject().value("inputs").toArray().size()==1;
                std::printf("CANVAS_DISCONNECT=%d\n",canvas);
                auto* out=item("out",-1);auto* in=item("in",1);
                if(out&&in)drag(center(out),center(in));else canvas=false;
                canvas=canvas&&builder.draft().value("nodes").toArray()[1].toObject().value("inputs").toArray().size()==2;
                std::printf("CANVAS_CONNECT=%d\n",canvas);
                auto valid=builder.draft();
                out=item("out",1);in=item("in",0);
                if(out&&in)drag(center(out),center(in));else canvas=false;
                canvas=canvas&&builder.draft()==valid;
                out=item("out",-1);in=item("in",1);
                if(out&&in)drag(center(out),center(in));else canvas=false;
                canvas=canvas&&builder.draft()==valid; // duplicate rejected
                // Reroute output via cable, then restore. A disconnected node
                // is legal as a draft but production validation must reject it.
                auto* terminal=item("in",-2);out=item("out",0);
                if(terminal&&out)drag(center(out),center(terminal));else canvas=false;
                canvas=canvas&&builder.draft().value("output").toInt()==0;
                auto* current=obs_source_get_settings(filter);
                auto unchanged=std::string(obs_data_get_string(current,"graph_json"));obs_data_release(current);
                QTest::mouseClick(apply,Qt::LeftButton);
                current=obs_source_get_settings(filter);
                canvas=canvas&&unchanged==obs_data_get_string(current,"graph_json");obs_data_release(current);
                terminal=item("in",-2);out=item("out",1);
                if(terminal&&out)drag(center(out),center(terminal));else canvas=false;
                canvas=canvas&&builder.draft()==valid;
                // The only input cable on node 0 cannot be removed accidentally.
                auto* last=item("cable",0,0);
                if(last){
                    auto point=graph->mapFromScene(static_cast<QGraphicsPathItem*>(last)->path().pointAtPercent(.5));
                    QTest::mouseClick(graph->viewport(),Qt::LeftButton,Qt::NoModifier,point);
                    QTest::keyClick(graph,Qt::Key_Delete);
                    canvas=canvas&&builder.draft()==valid;
                }else canvas=false;
                auto* node=item("node",1);
                if(node)QTest::mouseClick(graph->viewport(),Qt::LeftButton,Qt::NoModifier,center(node));
            }
            auto beforeTheme=builder.draft();
            auto* originalNode=item("node",0);auto originalPos=originalNode->pos();
            auto originalPalette=app.palette();
            bool themed=true;
            for(bool light:{true,false}){
                QPalette p=originalPalette;
                auto bg=QColor(light?"#fff4df":"#18232e"),fg=QColor(light?"#242019":"#ecf6ff"),accent=QColor(light?"#7c237b":"#efb35e");
                p.setColor(QPalette::Window,bg);p.setColor(QPalette::Base,bg);p.setColor(QPalette::WindowText,fg);
                p.setColor(QPalette::Text,fg);p.setColor(QPalette::Highlight,accent);p.setColor(QPalette::ButtonText,fg);
                app.setPalette(p);QTest::qWait(30);
                auto* node=static_cast<QGraphicsPathItem*>(item("node",0));
                themed=themed&&graph->backgroundBrush().color()==bg&&node&&node==originalNode&&node->pos()==originalPos&&builder.draft()==beforeTheme;
                auto* wire=static_cast<QGraphicsPathItem*>(item("cable",1,1));
                themed=themed&&wire&&wire->pen().color()==accent;
                themed=themed&&builder.styleSheet().isEmpty()&&apply->styleSheet().isEmpty();
            }
            app.setPalette(originalPalette);QTest::qWait(30);
            std::printf("BUILDER_THEME_PASS=%d\n",themed);ok=ok&&themed;
            std::printf("BUILDER_CANVAS_PASS=%d\n",canvas);ok=ok&&canvas;
            auto* gain=builder.findChild<QDoubleSpinBox*>("param_gain_db");ok=ok&&gain;
            if(gain)gain->setValue(-6);
            QTest::mouseClick(apply,Qt::LeftButton);
            after=obs_source_get_settings(filter);
            ok=ok&&std::string(obs_data_get_string(after,"graph_json")).find("gain")!=std::string::npos;obs_data_release(after);
            auto* tabs=builder.findChild<QTabWidget*>("builderTabs");
            auto* macroAdd=builder.findChild<QPushButton*>("addMacro");
            auto* macroName=builder.findChild<QLineEdit*>("macroName");
            auto* macroDefault=builder.findChild<QDoubleSpinBox*>("macroDefault");
            auto* targets=builder.findChild<QComboBox*>("macroTarget");
            auto* bind=builder.findChild<QPushButton*>("addBinding");
            auto* bindings=builder.findChild<QTableWidget*>("macroBindings");
            bool macros=tabs&&macroAdd&&macroName&&macroDefault&&targets&&bind&&bindings;
            if(macros){
                tabs->setCurrentIndex(2);QTest::mouseClick(macroAdd,Qt::LeftButton);
                macroName->setText("Intensity");QTest::keyClick(macroName,Qt::Key_Return);
                macroDefault->setValue(.25);
                auto choose=[&](int node,const QString& key){
                    for(int i=0;i<targets->count();++i){
                        auto data=targets->itemData(i).toStringList();
                        if(data.size()==2&&data[0].toInt()==node&&data[1]==key){
                            targets->setCurrentIndex(i);QTest::mouseClick(bind,Qt::LeftButton);return true;
                        }
                    }
                    return false;
                };
                macros=choose(0,"drive")&&choose(1,"gain_db");
                auto control=builder.draft().value("controls").toArray()[0].toObject();
                auto id=control.value("id").toString();
                macros=macros&&control.value("name")=="Intensity"&&control.value("default").toDouble()==.25&&bindings->rowCount()==2;
                auto* low=qobject_cast<QDoubleSpinBox*>(bindings->cellWidget(0,1));
                auto* high=qobject_cast<QDoubleSpinBox*>(bindings->cellWidget(0,2));
                macros=macros&&low&&high;
                if(low&&high){low->setValue(0);high->setValue(24);}
                low=qobject_cast<QDoubleSpinBox*>(bindings->cellWidget(1,1));high=qobject_cast<QDoubleSpinBox*>(bindings->cellWidget(1,2));
                if(low&&high){low->setValue(0);high->setValue(-6);}else macros=false;
                auto* data=obs_source_get_settings(filter);
                auto beforeMacro=std::string(obs_data_get_string(data,"graph_json"));obs_data_release(data);
                macros=macros&&beforeMacro.find("Intensity")==std::string::npos;
                QTest::mouseClick(apply,Qt::LeftButton);
                auto* props=obs_source_properties(filter);
                auto key=("graph_control_"+id).toUtf8();
                auto* prop=props?obs_properties_get(props,key.constData()):nullptr;
                macros=macros&&prop&&obs_property_get_type(prop)==OBS_PROPERTY_FLOAT;
                if(props)obs_properties_destroy(props);
                data=obs_source_get_settings(filter);
                macros=macros&&std::string(obs_data_get_string(data,"graph_json")).find("Intensity")!=std::string::npos;
                obs_data_release(data);
                // Already-bound parameters cannot be bound a second time.
                for(int t=0;t<targets->count();++t){
                    auto entry=targets->itemData(t).toStringList();
                    macros=macros&&entry!=QStringList{"0","drive"}&&entry!=QStringList{"1","gain_db"}&&entry!=QStringList{"0","oversample"};
                }
                auto committed=builder.draft();
                auto* removeBinding=builder.findChild<QPushButton*>("removeBinding");
                auto* removeMacro=builder.findChild<QPushButton*>("removeMacro");
                macros=macros&&removeBinding&&removeMacro;
                if(removeBinding){
                    bindings->selectRow(1);QTest::mouseClick(removeBinding,Qt::LeftButton);
                    macros=macros&&bindings->rowCount()==1;
                }
                auto routed=committed;
                routed["routes"]=QJsonArray{QJsonObject{{"control",id},{"source",1},{"amount",.25}}};
                builder.setDraft(routed);
                if(removeMacro)QTest::mouseClick(removeMacro,Qt::LeftButton);
                macros=macros&&builder.draft()==routed;
                builder.setDraft(committed);
                if(removeMacro)QTest::mouseClick(removeMacro,Qt::LeftButton);
                macros=macros&&builder.draft().value("controls").toArray().isEmpty();
                data=obs_source_get_settings(filter);
                macros=macros&&std::string(obs_data_get_string(data,"graph_json")).find("Intensity")!=std::string::npos;
                obs_data_release(data);
                builder.setDraft(committed);
            }
            std::printf("BUILDER_MACROS_PASS=%d\n",macros);ok=ok&&macros;
            auto* addRoute=builder.findChild<QPushButton*>("addRoute");
            auto* removeRoute=builder.findChild<QPushButton*>("removeRoute");
            auto* routeList=builder.findChild<QComboBox*>("routeList");
            auto* routeSource=builder.findChild<QComboBox*>("routeSource");
            auto* routeTarget=builder.findChild<QComboBox*>("routeTarget");
            auto* routeChannel=builder.findChild<QComboBox*>("routeChannel");
            auto* category=builder.findChild<QLineEdit*>("routeCategory");
            auto* amount=builder.findChild<QDoubleSpinBox*>("routeAmount");
            auto* rate=builder.findChild<QDoubleSpinBox*>("routeRate");
            auto* shape=builder.findChild<QComboBox*>("routeShape");
            bool routes=addRoute&&removeRoute&&routeList&&routeSource&&routeTarget&&routeChannel&&category&&amount&&rate&&shape;
            if(routes){
                auto before=builder.draft();
                auto id=before.value("controls").toArray()[0].toObject().value("id").toString();
                tabs->setCurrentIndex(3);QTest::mouseClick(addRoute,Qt::LeftButton);
                routeSource->setCurrentIndex(routeSource->findData(1));amount->setValue(-.4);rate->setValue(2.5);shape->setCurrentIndex(shape->findData(2));
                auto r=builder.draft().value("routes").toArray()[0].toObject();
                routes=routes&&r.value("control")==id&&r.value("source").toInt()==1&&r.value("amount").toDouble()==-.4&&r.value("rate").toDouble()==2.5&&r.value("shape").toInt()==2;
                auto* producer=obs_source_create_private("yamnet_classifier_source","Speech detector",nullptr);
                obs_source_filter_add(source,producer);
                auto channel=QString::fromUtf8(obs_source_get_uuid(source))+"/"+QString::fromUtf8(obs_source_get_uuid(producer));
                QTest::mouseClick(addRoute,Qt::LeftButton);routeSource->setCurrentIndex(routeSource->findData(6));
                int idx=routeChannel->findData(channel);routes=routes&&idx>=0;
                routeChannel->setCurrentIndex(idx);category->setText("Speech");QTest::keyClick(category,Qt::Key_Return);
                QTest::mouseClick(addRoute,Qt::LeftButton);routeSource->setCurrentIndex(routeSource->findData(8));
                idx=routeChannel->findData("test-hand");routes=routes&&idx>=0;routeChannel->setCurrentIndex(idx);
                QTest::mouseClick(addRoute,Qt::LeftButton);routeSource->setCurrentIndex(routeSource->findData(3));
                routes=routes&&!addRoute->isEnabled()&&builder.draft().value("routes").toArray().size()==4;
                auto* data=obs_source_get_settings(filter);auto old=std::string(obs_data_get_string(data,"graph_json"));obs_data_release(data);
                routes=routes&&old.find("\"routes\"")==std::string::npos;
                QTest::mouseClick(apply,Qt::LeftButton);
                data=obs_source_get_settings(filter);
                auto applied=QJsonDocument::fromJson(obs_data_get_string(data,"graph_json")).object();obs_data_release(data);
                routes=routes&&applied==builder.draft();
                auto saved=builder.draft();obs_source_filter_remove(source,producer);obs_source_release(producer);
                builder.setDraft(saved);routeList->setCurrentIndex(1);
                routes=routes&&routeChannel->currentData().toString()==channel&&builder.draft()==saved;
                routeList->setCurrentIndex(2);routeSource->setCurrentIndex(routeSource->findData(6));
                routes=routes&&builder.draft().value("routes").toArray()[2].toObject().value("channel").toString().isEmpty();
                QTest::mouseClick(apply,Qt::LeftButton);
                data=obs_source_get_settings(filter);
                routes=routes&&QJsonDocument::fromJson(obs_data_get_string(data,"graph_json")).object()==applied;obs_data_release(data);
                QTest::mouseClick(removeRoute,Qt::LeftButton);
                routes=routes&&addRoute->isEnabled()&&builder.draft().value("routes").toArray().size()==3;
                builder.setDraft(saved);routeList->setCurrentIndex(0);QApplication::processEvents();
                routes=routes&&rate->isVisible()&&shape->isVisible()&&!category->isVisible()&&!routeChannel->isVisible();
                std::printf("ROUTE_VIS rate=%d shape=%d category=%d channel=%d\n",rate->isVisible(),shape->isVisible(),category->isVisible(),routeChannel->isVisible());
                routeList->setCurrentIndex(1);QApplication::processEvents();
                routes=routes&&!rate->isVisible()&&!shape->isVisible()&&category->isVisible()&&routeChannel->isVisible();
                routeList->setCurrentIndex(0);
                QTest::qWait(50);
                auto previousSize=builder.size();builder.resize(420,640);QTest::qWait(50);
                routes=routes&&rate->isVisible()&&shape->isVisible()&&rate->height()>12&&shape->height()>12&&rate->mapTo(&builder,QPoint()).y()>shape->mapTo(&builder,QPoint()).y()&&shape->mapTo(&builder,QPoint()).y()>amount->mapTo(&builder,QPoint()).y();
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".narrow.png");
                builder.resize(previousSize);QTest::qWait(50);
            }
            std::printf("BUILDER_ROUTES_PASS=%d\n",routes);ok=ok&&routes;
            auto* meter=builder.findChild<QWidget*>("graphMonitor");
            auto* monitorStatus=builder.findChild<QLabel*>("monitorStatus");
            bool monitored=meter&&monitorStatus;
            if(monitored){
                builder.setDraft(QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]}],"output":0})").object());
                QTest::mouseClick(apply,Qt::LeftButton);tabs->setCurrentIndex(4);QTest::qWait(100);
                std::array<float,480> l{},r{};uint64_t start=os_gettime_ns();
                for(unsigned k=0;k<10;++k){
                    for(unsigned j=0;j<480;++j){l[j]=.4f*std::sin(2*std::numbers::pi*(k*480+j)*1500/48000);r[j]=-l[j];}
                    obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();
                    a.frames=480;a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
                    a.timestamp=start+k*10000000ULL;obs_source_output_audio(source,&a);QTest::qWait(10);
                }
                QTest::qWait(80);
                monitored=monitored&&monitorStatus->text().startsWith("Live")&&meter->property("hasFrame").toBool();
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".monitor.png");
                auto oldSize=builder.size();builder.resize(420,640);QTest::qWait(40);
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".monitor-narrow.png");
                builder.resize(oldSize);
                QTest::qWait(600);
                monitored=monitored&&!meter->property("hasFrame").toBool()&&monitorStatus->text().contains("Waiting");
                tabs->setCurrentIndex(3);
            }
            std::printf("BUILDER_MONITOR_PASS=%d\n",monitored);ok=ok&&monitored;
            QTest::qWait(80);
            if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT"));
        }
    }
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_shutdown();
    std::printf("BUILDER_WIDGET_PASS=%d\n",ok);return ok?0:1;
}
