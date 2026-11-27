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
#include <QFileDialog>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QDir>
#include <QDataStream>
#include <QTimer>
#include <obs-module.h>
#include <cstdio>
#include <util/platform.h>
#include <numbers>
#include <optional>
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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    QTemporaryDir storage;
    {
        hyperdsp::EffectBuilder builder(nullptr,storage.path(),QString::fromUtf8(argv[2])+"/examples/graphs");
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
            // A cable can run behind a card, and viewport rounding varies
            // with Qt/font metrics. Select an exposed point on this specific
            // wire, not an assumed midpoint that might hit a node or port.
            auto cablePoint=[&](QGraphicsPathItem* wire)->std::optional<QPoint>{
                if(!wire)return {};
                for(int step=0;step<=80;++step){
                    double t=.5+(step%2?1:-1)*((step+1)/2)*.01;
                    auto point=graph->mapFromScene(wire->mapToScene(wire->path().pointAtPercent(t)));
                    if(!graph->viewport()->rect().contains(point))continue;
                    QGraphicsItem* firstCable=nullptr;bool obstructed=false;
                    for(auto* candidate:graph->items(point)){
                        auto role=candidate->data(1).toString();
                        if(role=="out"||role=="node"){obstructed=true;break;}
                        if(role=="cable"&&!firstCable)firstCable=candidate;
                    }
                    if(!obstructed&&firstCable==wire)return point;
                }
                return {};
            };
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
                    auto point=cablePoint(cable);
                    if(point){
                        QTest::mouseClick(graph->viewport(),Qt::LeftButton,Qt::NoModifier,*point);
                        const bool selected=graph->scene()->selectedItems().contains(cable);
                        std::printf("CANVAS_SELECTED=%d point=%d,%d\n",selected,point->x(),point->y());
                        canvas=canvas&&selected;
                        QTest::keyClick(graph,Qt::Key_Delete);QApplication::processEvents();
                    }else{std::printf("CANVAS_NO_EXPOSED_CABLE=1\n");canvas=false;}
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
                    auto point=cablePoint(static_cast<QGraphicsPathItem*>(last));
                    if(point){
                        QTest::mousePress(graph->viewport(),Qt::LeftButton,Qt::NoModifier,*point);
                        std::printf("LAST_PRESS=%d\n",graph->scene()->selectedItems().contains(last));
                        QTest::mouseRelease(graph->viewport(),Qt::LeftButton,Qt::NoModifier,*point);
                        const bool selected=graph->scene()->selectedItems().contains(last);
                        std::printf("CANVAS_LAST_SELECTED=%d at %d,%d\n",selected,point->x(),point->y());canvas=canvas&&selected;
                        QTest::keyClick(graph,Qt::Key_Delete);
                        canvas=canvas&&builder.draft()==valid;
                    }else {std::printf("CANVAS_LAST_NO_EXPOSED_POINT=1\n");canvas=false;}
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
                auto* view=builder.findChild<QComboBox*>("monitorView");
                auto* detail=builder.findChild<QLabel*>("monitorDetail");
                monitored=monitored&&view&&detail;
                if(view&&detail){
                    view->setCurrentIndex(1);QTest::qWait(30);
                    monitored=monitored&&detail->text().contains("-1.000")&&detail->text().contains("-120.0");
                    if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                        builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".stereo.png");
                    view->setCurrentIndex(2);QTest::qWait(30);
                    monitored=monitored&&detail->text().contains("not a transfer function");
                    if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                        builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".difference.png");
                    view->setCurrentIndex(1);
                }
                auto oldSize=builder.size();builder.resize(420,640);QTest::qWait(40);
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".monitor-narrow.png");
                builder.resize(oldSize);
                QTest::qWait(600);
                monitored=monitored&&!meter->property("hasFrame").toBool()&&monitorStatus->text().contains("Waiting")&&detail&&detail->text().isEmpty();
                builder.setDraft(QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_dynamic_eq_filter:1","params":{"frequency":1500,"threshold":-30,"range_db":-9},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_dynamics_filter:1","params":{"threshold":-30,"ratio":4},"inputs":[{"source":-1,"weight":1}]},{"effect":"native:hyperdsp_multiband_filter:1","params":{"mid_threshold":-30,"mid_ratio":4,"low_ratio":1,"high_ratio":1},"inputs":[{"source":-1,"weight":1}]},{"effect":"gain","inputs":[{"source":0,"weight":0.3},{"source":1,"weight":0.3},{"source":2,"weight":0.3}]}],"output":3})").object());
                QTest::mouseClick(apply,Qt::LeftButton);QTest::qWait(80);
                if(view)view->setCurrentIndex(3);
                for(unsigned k=0;k<100;++k){
                    for(unsigned j=0;j<480;++j){l[j]=.4f*std::sin(2*std::numbers::pi*(k*480+j)*1500/48000);r[j]=-l[j];}
                    obs_source_audio a{};a.data[0]=(uint8_t*)l.data();a.data[1]=(uint8_t*)r.data();
                    a.frames=480;a.speakers=SPEAKERS_STEREO;a.format=AUDIO_FORMAT_FLOAT_PLANAR;a.samples_per_sec=48000;
                    a.timestamp=start+(100+k)*10000000ULL;obs_source_output_audio(source,&a);
                }
                QTest::qWait(80);
                auto* node=builder.findChild<QComboBox*>("monitorNode");
                monitored=monitored&&node&&node->count()==3&&node->currentText().contains("Dynamic EQ")&&detail&&detail->text().contains("-9.0 dB");
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".node.png");
                if(node&&node->count()==3){
                    node->setCurrentIndex(1);QTest::qWait(20);
                    monitored=monitored&&detail->text().contains("-16.5 dB");
                    node->setCurrentIndex(2);QTest::qWait(20);
                    monitored=monitored&&detail->text().contains("250 / 2500 Hz");
                    auto normal=builder.size();builder.resize(420,640);QTest::qWait(30);
                    if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".bands-narrow.png");
                    builder.resize(normal);
                }
                QTest::qWait(600);
                monitored=monitored&&node&&node->count()==0&&detail&&detail->text().isEmpty();
                tabs->setCurrentIndex(3);
            }
            std::printf("BUILDER_MONITOR_PASS=%d\n",monitored);ok=ok&&monitored;
            auto* preview=builder.findChild<QPushButton*>("runPreview");
            auto* previewStatus=builder.findChild<QLabel*>("previewStatus");
            auto* previewMode=builder.findChild<QComboBox*>("previewMode");
            bool previewed=preview&&previewStatus&&previewMode;
            if(previewed){
                auto* data=obs_source_get_settings(filter);auto accepted=std::string(obs_data_get_string(data,"graph_json"));obs_data_release(data);
                auto low=QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_native_filter:1","params":{"frequency":1000},"inputs":[{"source":-1,"weight":1}]}],"output":0})").object();
                builder.setDraft(low);tabs->setCurrentIndex(5);QTest::mouseClick(preview,Qt::LeftButton);
                auto complete=[&](){for(int i=0;i<100&&!preview->isEnabled();++i)QTest::qWait(20);return preview->isEnabled();};
                previewed=complete()&&previewStatus->text().contains("Response")&&previewMode->currentData().toInt()==0&&previewMode->count()==2;
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".preview.png");
                previewMode->setCurrentIndex(1);QTest::qWait(40);
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".tone.png");
                auto clip=QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_color_filter:3","params":{"drive":24},"inputs":[{"source":-1,"weight":1}]}],"output":0})").object();
                builder.setDraft(clip);QTest::qWait(180);
                previewed=previewed&&previewStatus->text().contains("changed");
                QTest::mouseClick(preview,Qt::LeftButton);
                previewed=complete()&&previewed&&previewMode->count()==1&&previewStatus->text().contains("Tone");
                auto invalid=clip;invalid["output"]=9;builder.setDraft(invalid);QTest::mouseClick(preview,Qt::LeftButton);
                previewed=complete()&&previewed&&previewStatus->text().startsWith("Error");
                data=obs_source_get_settings(filter);previewed=previewed&&accepted==obs_data_get_string(data,"graph_json");obs_data_release(data);
            }
            std::printf("BUILDER_PREVIEW_PASS=%d\n",previewed);ok=ok&&previewed;
            auto* renderAudition=builder.findChild<QPushButton*>("renderAudition");
            auto* dry=builder.findChild<QPushButton*>("playDry");
            auto* wet=builder.findChild<QPushButton*>("playWet");
            auto* stop=builder.findChild<QPushButton*>("stopAudition");
            auto* signal=builder.findChild<QComboBox*>("auditionSignal");
            auto* device=builder.findChild<QComboBox*>("auditionDevice");
            auto* auditionStatus=builder.findChild<QLabel*>("auditionStatus");
            bool auditioned=renderAudition&&dry&&wet&&stop&&signal&&device&&auditionStatus;
            if(auditioned){
                auto* data=obs_source_get_settings(filter);auto accepted=std::string(obs_data_get_string(data,"graph_json"));obs_data_release(data);
                auto draft=QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]}],"output":0})").object();
                builder.setDraft(draft);tabs->setCurrentIndex(6);signal->setCurrentIndex(1);
                QTest::mouseClick(renderAudition,Qt::LeftButton);
                for(int i=0;i<150&&!renderAudition->isEnabled();++i)QTest::qWait(20);
                auditioned=auditionStatus->text().startsWith("Ready")&&!dry->isEnabled()&&!wet->isEnabled();
                if(qEnvironmentVariableIsSet("HYPERDSP_AUDITION_TEST_SINK")){
                    auto sink=qEnvironmentVariable("HYPERDSP_AUDITION_TEST_SINK");
                    for(int i=0;i<100&&device->findData(sink)<0;++i)QTest::qWait(20);
                    int index=device->findData(sink);auditioned=auditioned&&index>=0;
                    if(index>=0){
                        device->setCurrentIndex(index);auditioned=auditioned&&dry->isEnabled()&&wet->isEnabled();
                        QTest::mouseClick(dry,Qt::LeftButton);
                        for(int i=0;i<300&&!auditionStatus->text().startsWith("Finished");++i)QTest::qWait(20);
                        std::printf("AUDITION_DRY %s\n",qPrintable(auditionStatus->text()));std::fflush(stdout);
                        auditioned=auditioned&&auditionStatus->text().startsWith("Finished");
                        QTest::mouseClick(wet,Qt::LeftButton);QTest::qWait(120);
                        tabs->setCurrentIndex(5);QTest::qWait(200);tabs->setCurrentIndex(6);
                        auditioned=auditioned&&!stop->isEnabled();
                    }
                }
                QTemporaryFile wave(QDir::tempPath()+"/hyperdsp-XXXXXX.wav");
                bool made=wave.open();
                if(made){
                    QDataStream wav(&wave);wav.setByteOrder(QDataStream::LittleEndian);
                    wav.writeRawData("RIFF",4);wav<<quint32(36+1600);wav.writeRawData("WAVEfmt ",8);
                    wav<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(8000)<<quint32(16000)<<quint16(2)<<quint16(16);
                    wav.writeRawData("data",4);wav<<quint32(1600);
                    for(int j=0;j<800;++j)wav<<qint16(8192*std::sin(2*std::numbers::pi*j/16));
                    wave.flush();wave.close();
                    signal->setCurrentIndex(3);
                    auto* choose=builder.findChild<QPushButton*>("chooseAuditionFile");
                    auditioned=auditioned&&choose;
                    if(choose){
                        QTimer select;select.setInterval(100);
                        int tries=0;
                        QObject::connect(&select,&QTimer::timeout,&builder,[&]{
                            ++tries;
                            for(auto* widget:QApplication::topLevelWidgets()){
                                auto* dialog=qobject_cast<QFileDialog*>(widget);
                                if(!dialog||!dialog->isVisible())continue;
                                if(tries>30){dialog->reject();return;}
                                dialog->setDirectory(QFileInfo(wave.fileName()).absolutePath());
                                dialog->selectFile(QFileInfo(wave.fileName()).fileName());
                                if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit"))edit->setText(wave.fileName());
                                QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);
                                return;
                            }
                        });
                        select.start();
                        QTest::mouseClick(choose,Qt::LeftButton);
                        select.stop();
                        QTest::mouseClick(renderAudition,Qt::LeftButton);
                        for(int i=0;i<150&&!renderAudition->isEnabled();++i)QTest::qWait(20);
                        std::printf("AUDITION_FILE %s\n",qPrintable(auditionStatus->text()));std::fflush(stdout);
                        auditioned=auditioned&&auditionStatus->text().startsWith("Ready")&&auditionStatus->text().contains("8 kHz")&&auditionStatus->text().contains("1 ch");
                        if(qEnvironmentVariableIsSet("HYPERDSP_AUDITION_TEST_SINK")&&wet->isEnabled()){
                            QTest::mouseClick(wet,Qt::LeftButton);
                            for(int i=0;i<150&&!auditionStatus->text().startsWith("Finished");++i)QTest::qWait(20);
                            auditioned=auditioned&&auditionStatus->text().startsWith("Finished");
                        }
                    }
                }else auditioned=false;
                if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))
                    builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".audition.png");
                draft["output"]=9;builder.setDraft(draft);QTest::qWait(150);
                auditioned=auditioned&&!dry->isEnabled()&&!wet->isEnabled()&&auditionStatus->text().contains("changed");
                data=obs_source_get_settings(filter);auditioned=auditioned&&accepted==obs_data_get_string(data,"graph_json");obs_data_release(data);
            }
            std::printf("BUILDER_AUDITION_PASS=%d\n",auditioned);ok=ok&&auditioned;
            QTest::qWait(80);
            if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT"));
        }
    }
    bool persisted=storage.isValid();
    QJsonObject saved=QJsonDocument::fromJson(R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-3},"inputs":[{"source":-1,"weight":1}]}],"output":0})").object();
    QPointF position;
    {
        hyperdsp::EffectBuilder editor(nullptr,storage.path(),QString::fromUtf8(argv[2])+"/examples/graphs");
        editor.resize(880,620);editor.show();editor.setDraft(saved);QTest::qWait(40);
        auto* graph=editor.findChild<QGraphicsView*>("graph");
        auto* tabs=editor.findChild<QTabWidget*>("builderTabs");
        auto* name=editor.findChild<QLineEdit*>("presetName");
        auto* list=editor.findChild<QListWidget*>("presetList");
        auto* save=editor.findChild<QPushButton*>("savePreset");
        auto* load=editor.findChild<QPushButton*>("loadPreset");
        auto* fresh=editor.findChild<QPushButton*>("newDraft");
        auto* previous=editor.findChild<QPushButton*>("previousDraft");
        persisted=persisted&&graph&&tabs&&name&&list&&save&&load&&fresh&&previous;
        if(persisted){
            QGraphicsItem* node=nullptr;
            for(auto* i:graph->scene()->items())if(i->data(1)=="node"&&i->data(0).toInt()==0)node=i;
            persisted=persisted&&node;
            if(node){
                auto start=graph->mapFromScene(node->sceneBoundingRect().center());
                auto pos=node->pos();
                QTest::mousePress(graph->viewport(),Qt::LeftButton,Qt::NoModifier,start);
                QTest::mouseMove(graph->viewport(),start+QPoint(0,48),20);
                QTest::mouseRelease(graph->viewport(),Qt::LeftButton,Qt::NoModifier,start+QPoint(0,48));
                position=node->pos();persisted=persisted&&position!=pos;
            }
            tabs->setCurrentIndex(7);name->setText("Local example");QTest::mouseClick(save,Qt::LeftButton);
            int row=-1;for(int i=0;i<list->count();++i)if(list->item(i)->text().contains("Local example"))row=i;
            persisted=persisted&&row>=0&&list->count()>=12;
            QTest::mouseClick(fresh,Qt::LeftButton);
            persisted=persisted&&editor.draft().value("nodes").toArray().isEmpty();
            QTest::mouseClick(previous,Qt::LeftButton);
            persisted=persisted&&editor.draft()==saved;
            QTest::mouseClick(fresh,Qt::LeftButton);
            if(row>=0){list->setCurrentRow(row);QTest::mouseClick(load,Qt::LeftButton);}
            persisted=persisted&&editor.draft()==saved;
            if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))editor.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT")+".presets.png");
            int bundled=-1;
            for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole+1).toBool()&&list->item(i)->text().startsWith("Envelope Wah"))bundled=i;
            persisted=persisted&&bundled>=0;
            if(bundled>=0){
                auto* before=obs_source_get_settings(filter);auto accepted=std::string(obs_data_get_string(before,"graph_json"));obs_data_release(before);
                list->setCurrentRow(bundled);QTest::mouseClick(load,Qt::LeftButton);
                persisted=persisted&&editor.draft().value("routes").toArray().size()==1&&editor.draft().value("nodes").toArray().size()==3;
                auto* after=obs_source_get_settings(filter);persisted=persisted&&accepted==obs_data_get_string(after,"graph_json");obs_data_release(after);
                QTest::mouseClick(previous,Qt::LeftButton);persisted=persisted&&editor.draft()==saved;
            }
            QTest::qWait(650); // autosave without Apply
            hyperdsp::BuilderStorage store(storage.path());hyperdsp::BuilderDocument doc;QString error;
            persisted=persisted&&store.readRecovery(doc,error)&&doc.graph==saved&&doc.positions.at(0)==position;
        }
    }
    {
        hyperdsp::EffectBuilder recovered(nullptr,storage.path(),QString::fromUtf8(argv[2])+"/examples/graphs");
        auto* graph=recovered.findChild<QGraphicsView*>("graph");auto* target=recovered.findChild<QComboBox*>("target");
        persisted=persisted&&recovered.draft()==saved&&target&&target->currentIndex()==-1&&graph;
        bool layout=false;
        if(graph)for(auto* i:graph->scene()->items())if(i->data(1)=="node"&&i->data(0).toInt()==0)layout=i->pos()==position;
        persisted=persisted&&layout;
        auto* refresh=recovered.findChild<QPushButton*>("refreshTargets");
        if(refresh)refresh->click();
        persisted=persisted&&refresh&&target->currentIndex()==-1;
    }
    const QByteArray corruptBytes="{not a recovery document";
    {
        QFile bad(storage.filePath("recovery.json"));
        persisted=persisted&&bad.open(QIODevice::WriteOnly)&&bad.write(corruptBytes)==corruptBytes.size();bad.close();
        hyperdsp::EffectBuilder editor(nullptr,storage.path(),QString::fromUtf8(argv[2])+"/examples/graphs");
        editor.setDraft(saved);QTest::qWait(550);
        persisted=persisted&&bad.open(QIODevice::ReadOnly)&&bad.readAll()==corruptBytes;bad.close();
        auto* fresh=editor.findChild<QPushButton*>("newDraft");
        if(fresh)fresh->click();
        auto backups=QDir(storage.path()).entryList({"recovery-unreadable-*.json"},QDir::Files);
        persisted=persisted&&fresh&&backups.size()==1;
        if(backups.size()==1){QFile backup(storage.filePath(backups.first()));persisted=persisted&&backup.open(QIODevice::ReadOnly)&&backup.readAll()==corruptBytes;}
        hyperdsp::BuilderStorage store(storage.path());hyperdsp::BuilderDocument doc;QString error;
        persisted=persisted&&store.readRecovery(doc,error)&&doc.graph.value("nodes").toArray().isEmpty();
    }
    std::printf("BUILDER_PERSISTENCE_PASS=%d\n",persisted);ok=ok&&persisted;
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_shutdown();
    std::printf("BUILDER_WIDGET_PASS=%d\n",ok);return ok?0:1;
}
