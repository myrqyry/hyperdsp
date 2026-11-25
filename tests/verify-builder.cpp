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
#include <QTableWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QTest>
#include <obs-module.h>
#include <cstdio>
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
    obs_register_source(&fixture);
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
            std::printf("BUILDER_CANVAS_PASS=%d\n",canvas);ok=ok&&canvas;
            auto* gain=builder.findChild<QDoubleSpinBox*>("param_gain_db");ok=ok&&gain;
            if(gain)gain->setValue(-6);
            QTest::mouseClick(apply,Qt::LeftButton);
            after=obs_source_get_settings(filter);
            ok=ok&&std::string(obs_data_get_string(after,"graph_json")).find("gain")!=std::string::npos;obs_data_release(after);
            QTest::qWait(80);
            if(qEnvironmentVariableIsSet("HYPERDSP_BUILDER_SCREENSHOT"))builder.grab().save(qEnvironmentVariable("HYPERDSP_BUILDER_SCREENSHOT"));
        }
    }
    obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_shutdown();
    std::printf("BUILDER_WIDGET_PASS=%d\n",ok);return ok?0:1;
}
