#include "effect_builder.hpp"
#include "effect-metadata.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QFormLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QPainter>
#include <QPointer>
#include <QMessageBox>
#include <QGraphicsSceneMouseEvent>
#include <QTabWidget>
#include <cmath>
#include <jansson.h>
namespace hyperdsp {
namespace {
QString qs(const std::string& s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
QJsonObject json(obs_data_t* data){return QJsonDocument::fromJson(obs_data_get_json(data)).object();}
QString effectName(const QJsonObject& item){return item.value("name").toString(item.value("id").toString());}
QPushButton* button(const QString& text,const char* name,QWidget* parent){
    auto* p=new QPushButton(text,parent);p->setObjectName(name);return p;
}
struct Target {QString parent,filter,label;};
void collect_filter(obs_source_t* parent,obs_source_t* filter,void* data){
    if(std::string_view(obs_source_get_id(filter))!="hyperdsp_graph_filter"||obs_source_removed(filter))return;
    static_cast<std::vector<Target>*>(data)->push_back({QString::fromUtf8(obs_source_get_uuid(parent)),QString::fromUtf8(obs_source_get_uuid(filter)),
        QString::fromUtf8(obs_source_get_name(parent))+" / "+QString::fromUtf8(obs_source_get_name(filter))});
}
bool collect_source(void* data,obs_source_t* source){
    if(!obs_source_removed(source))obs_source_enum_filters(source,collect_filter,data);
    return true;
}
obs_source_t* find_target(QComboBox* combo){
    auto parent=combo->currentData().toStringList();
    if(parent.size()!=2)return nullptr;
    auto* source=obs_get_source_by_uuid(parent[0].toUtf8().constData());if(!source)return nullptr;
    struct Find {QString id;obs_source_t* found=nullptr;};Find find{parent[1]};
    obs_source_enum_filters(source,[](obs_source_t*,obs_source_t* f,void* p){
        auto& target=*static_cast<Find*>(p);
        if(!target.found&&!obs_source_removed(f)&&target.id==QString::fromUtf8(obs_source_get_uuid(f))&&
            std::string_view(obs_source_get_id(f))=="hyperdsp_graph_filter")target.found=obs_source_get_ref(f);
    },&find);
    obs_source_release(source);return find.found;
}
QJsonObject empty(){return {{"schema",1},{"nodes",QJsonArray{}},{"output",-1}};}
}
EffectBuilder::EffectBuilder(QWidget* parent):QWidget(parent),draft_(empty()){
    setObjectName("hyperdspEffectBuilder");
    // Deliberately compact dark surface; native controls retain keyboard accessibility.
    setStyleSheet(R"(
        QWidget#hyperdspEffectBuilder { background:#21262e; color:#e5e9ef; }
        QWidget { color:#e5e9ef; background:#21262e; }
        QLineEdit,QComboBox,QDoubleSpinBox,QListWidget,QTableWidget,QScrollArea { background:#171c23; border:1px solid #39444f; border-radius:6px; }
        QComboBox,QDoubleSpinBox,QLineEdit { padding:3px 6px; min-height:20px; }
        QPushButton { background:#303943; border:1px solid #47535f; border-radius:9px; padding:4px 9px; }
        QPushButton:hover { background:#3c4854; } QPushButton:pressed { background:#242c35; }
        QListWidget::item { padding:4px 6px; } QListWidget::item:selected { background:#3a5d58; }
        QListWidget::item:alternate { background:#20262e; }
        QTabWidget::pane { border:1px solid #39444f; border-radius:5px; }
        QTabBar::tab { background:#2c3540; padding:5px 16px; border-top-left-radius:6px; border-top-right-radius:6px; }
        QTabBar::tab:selected { background:#405c58; }
        QHeaderView::section { background:#29333d; border:0; padding:4px; }
        QSplitter::handle { background:#39444f; height:4px; }
        QToolTip { background:#2b3540; color:#e5e9ef; border:1px solid #586674; }
    )");
    auto* root=new QVBoxLayout(this);root->setContentsMargins(6,6,6,6);root->setSpacing(5);
    auto* targetRow=new QHBoxLayout;
    target_=new QComboBox;target_->setObjectName("target");target_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);target_->setMinimumContentsLength(12);
    auto* refresh=button("Refresh","refreshTargets",this),*load=button("Load","loadTarget",this);
    targetRow->addWidget(target_,1);targetRow->addWidget(refresh);targetRow->addWidget(load);root->addLayout(targetRow);
    auto* tools=new QHBoxLayout;
    auto* open=button("Import","importGraph",this),*save=button("Export","exportGraph",this),*validate=button("Validate","validateGraph",this),*applyBtn=button("Apply","applyGraph",this);
    tools->addWidget(open);tools->addWidget(save);tools->addStretch();tools->addWidget(validate);tools->addWidget(applyBtn);root->addLayout(tools);
    applyBtn->setStyleSheet("QPushButton { background:#32655e; color:#effff9; border:1px solid #50857b; border-radius:9px; padding:4px 14px; } QPushButton:hover {background:#41796f;}");
    auto* split=new QSplitter(Qt::Vertical);
    auto* viewPanel=new QWidget;auto* viewLayout=new QVBoxLayout(viewPanel);viewLayout->setContentsMargins(0,0,0,0);
    scene_=new QGraphicsScene(this);graph_=new QGraphicsView(scene_);graph_->setObjectName("graph");graph_->setRenderHint(QPainter::Antialiasing);
    graph_->setBackgroundBrush(QColor("#171c23"));graph_->setDragMode(QGraphicsView::ScrollHandDrag);graph_->setMinimumHeight(160);
    viewLayout->addWidget(graph_,1);
    auto* outputRow=new QHBoxLayout;outputRow->addWidget(new QLabel("Output"));
    output_=new QComboBox;output_->setObjectName("outputNode");outputRow->addWidget(output_,1);
    auto* remove=button("Remove node","removeNode",this);outputRow->addWidget(remove);viewLayout->addLayout(outputRow);
    split->addWidget(viewPanel);
    auto* tabs=new QTabWidget;auto* libraryPanel=new QWidget;auto* libLayout=new QVBoxLayout(libraryPanel);libLayout->setContentsMargins(3,3,3,3);
    search_=new QLineEdit;search_->setObjectName("search");search_->setPlaceholderText("Search effects, makers, categories…");
    library_=new QListWidget;library_->setObjectName("library");library_->setAlternatingRowColors(true);
    auto* add=button("Add selected effect","addNode",this);auto* scan=button("Scan installed","scanEffects",this);auto* reload=button("Reload","reloadEffects",this);
    auto* libButtons=new QHBoxLayout;libButtons->addWidget(add,1);libButtons->addWidget(scan);libButtons->addWidget(reload);
    libLayout->addWidget(search_);libLayout->addWidget(library_,1);libLayout->addLayout(libButtons);
    tabs->addTab(libraryPanel,"Effects");
    auto* inspector=new QWidget;auto* inspectorLayout=new QVBoxLayout(inspector);inspectorLayout->setContentsMargins(3,3,3,3);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);auto* paramPanel=new QWidget;
    parameters_=new QFormLayout(paramPanel);parameters_->setContentsMargins(4,4,4,4);parameters_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(paramPanel);inspectorLayout->addWidget(scroll,1);
    edges_=new QTableWidget(0,2);edges_->setObjectName("inputs");edges_->setHorizontalHeaderLabels({"Input","Weight"});
    edges_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);edges_->verticalHeader()->hide();edges_->setSelectionBehavior(QAbstractItemView::SelectRows);edges_->setMaximumHeight(120);
    inspectorLayout->addWidget(edges_);
    auto* edgeButtons=new QHBoxLayout;auto* addInput=button("+ Input","addInput",this),*removeInput=button("− Input","removeInput",this);
    edgeButtons->addWidget(addInput);edgeButtons->addWidget(removeInput);inspectorLayout->addLayout(edgeButtons);
    tabs->addTab(inspector,"Inspector");split->addWidget(tabs);split->setStretchFactor(0,1);split->setStretchFactor(1,1);root->addWidget(split,1);
    status_=new QLabel;status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);root->addWidget(status_);
    connect(refresh,&QPushButton::clicked,this,[this]{refreshTargets();});
    connect(load,&QPushButton::clicked,this,[this]{loadTarget();});
    connect(search_,&QLineEdit::textChanged,this,[this]{refreshLibrary();});
    connect(add,&QPushButton::clicked,this,[this,tabs]{addNode();if(selected_>=0)tabs->setCurrentIndex(1);});
    connect(library_,&QListWidget::itemDoubleClicked,this,[this,tabs]{addNode();if(selected_>=0)tabs->setCurrentIndex(1);});
    connect(scan,&QPushButton::clicked,this,[this]{message(qs(CatalogScan::start()));});
    connect(reload,&QPushButton::clicked,this,[this]{inventory_=inventory();refreshLibrary();inspect();message(qs(CatalogScan::status()));});
    connect(scene_,&QGraphicsScene::selectionChanged,this,[this,tabs]{
        auto items=scene_->selectedItems();if(items.isEmpty())return;
        selected_=items.first()->data(0).toInt();inspect();tabs->setCurrentIndex(1);
    });
    connect(output_,&QComboBox::currentIndexChanged,this,[this]{draft_["output"]=output_->currentData().toInt();redraw();});
    connect(remove,&QPushButton::clicked,this,[this]{removeNode();});
    connect(addInput,&QPushButton::clicked,this,[this]{addEdge();});
    connect(removeInput,&QPushButton::clicked,this,[this]{removeEdge();});
    connect(validate,&QPushButton::clicked,this,[this]{apply(true);});
    connect(applyBtn,&QPushButton::clicked,this,[this]{apply(false);});
    connect(open,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getOpenFileName(this,"Import graph",{},"HyperDSP graph (*.json)");
        if(path.isEmpty())return;QFile file(path);
        if(!file.open(QIODevice::ReadOnly)||file.size()>65536){message("Cannot read graph (64 KiB limit).",true);return;}
        const auto bytes=file.readAll();json_error_t strict_error{};
        auto* strict=json_loadb(bytes.constData(),size_t(bytes.size()),JSON_REJECT_DUPLICATES,&strict_error);
        if(!strict){message(QString::fromUtf8(strict_error.text),true);return;}json_decref(strict);
        QJsonParseError error;auto doc=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()){message(error.errorString(),true);return;}
        setDraft(doc.object());message("Imported draft. Validate or apply explicitly.");
    });
    connect(save,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getSaveFileName(this,"Export graph","effect.json","HyperDSP graph (*.json)");
        if(path.isEmpty())return;QSaveFile file(path);
        auto bytes=QJsonDocument(draft_).toJson();if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit())message("Draft exported.");else message("Export failed.",true);
    });
    refreshTargets();inventory_=inventory();refreshLibrary();redraw();inspect();message("Add a HyperDSP Graph filter to an audio source, then select it here. Edits are drafts until Apply.");
}
void EffectBuilder::message(const QString& s,bool error){status_->setText(s);status_->setStyleSheet(error?"color:#ed9e86;":"");}
void EffectBuilder::refreshTargets(){
    auto old=target_->currentData();std::vector<Target> targets;obs_enum_sources(collect_source,&targets);
    target_->clear();for(auto& t:targets)target_->addItem(t.label,QStringList{t.parent,t.filter});
    int index=target_->findData(old);if(index>=0)target_->setCurrentIndex(index);else if(old.isValid()||targets.size()!=1)target_->setCurrentIndex(-1);
}
QJsonArray EffectBuilder::inventory()const{
    obs_audio_info ai{};unsigned rate=48000,channels=2;
    if(obs_get_audio_info(&ai)){rate=ai.samples_per_sec;channels=get_audio_channels(ai.speakers);}
    QJsonArray out;
    for(auto& e:effect_library(read_catalog(),channels)){
        auto* item=obs_data_create();obs_data_set_string(item,"id",e.id.c_str());obs_data_set_string(item,"name",e.name.c_str());
        obs_data_set_string(item,"category",e.category.c_str());obs_data_set_string(item,"format",e.format.c_str());
        obs_data_set_string(item,"maker",e.maker.c_str());obs_data_set_bool(item,"compatible",e.compatible);
        graph_metadata(item,e,rate);out.append(json(item));obs_data_release(item);
    }
    auto* gain=obs_data_create();auto* params=obs_data_array_create();
    obs_data_set_string(gain,"id","gain");obs_data_set_string(gain,"name","Gain / weighted sum");obs_data_set_string(gain,"category","Utility");obs_data_set_string(gain,"format","native");obs_data_set_bool(gain,"compatible",true);
    number_parameter(params,gain_key,"Gain",gain_min,gain_max,gain_step,0," dB");
    obs_data_set_array(gain,"parameters",params);obs_data_array_release(params);out.append(json(gain));obs_data_release(gain);
    return out;
}
void EffectBuilder::refreshLibrary(){
    library_->clear();
    for(auto value:inventory_){
        auto e=value.toObject();auto name=effectName(e),category=e.value("category").toString(),format=e.value("format").toString();
        if(!match_words((name+" "+category+" "+format+" "+e.value("maker").toString()+" "+e.value("id").toString()).toStdString(),search_->text().toStdString()))continue;
        auto* item=new QListWidgetItem(name+"  ·  "+category+" / "+format,library_);
        item->setData(Qt::UserRole,e.value("id").toString());item->setToolTip(e.value("id").toString());
        if(!e.value("compatible").toBool())item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
}
QJsonObject EffectBuilder::metadata(const QString& id)const{
    for(auto e:inventory_)if(e.toObject().value("id").toString()==id)return e.toObject();
    return {{"id",id},{"name",id}};
}
void EffectBuilder::setDraft(const QJsonObject& d){draft_=d;selected_=d.value("output").toInt(-1);redraw();inspect();}
void EffectBuilder::redraw(){
    QSignalBlocker block(scene_),outBlock(output_);scene_->clear();output_->clear();
    auto nodes=draft_.value("nodes").toArray();auto out=draft_.value("output").toInt(-1);
    if(nodes.isEmpty())output_->addItem("Direct input",-1);
    int count=std::min<qsizetype>(nodes.size(),16);
    // Stable index positions; edges are visualized even for non-topological imports.
    std::vector<QPointF> positions;
    for(int i=0;i<count;++i)positions.emplace_back(142+(i%3)*188,28+(i/3)*102);
    QPointF input(10,28),dest(142+std::min(count,3)*188,28);
    auto drawEdge=[this](QPointF a,QPointF b,double weight){
        QPainterPath path(a);const double gap=b.x()-a.x();
        if(gap>65||gap<0||std::abs(a.y()-b.y())>1){
            const double lane=std::max(a.y(),b.y())+48;
            path.cubicTo(a+QPointF(23,0),QPointF(a.x()+18,lane),QPointF(a.x()+40,lane));
            path.lineTo(QPointF(b.x()-40,lane));
            path.cubicTo(QPointF(b.x()-18,lane),b-QPointF(23,0),b);
        }else{
            double dx=std::max(15.,gap*.45);
            path.cubicTo(a+QPointF(dx,0),b-QPointF(dx,0),b);
        }
        const QColor color(weight<0?"#db9b6b":"#66b8aa");
        scene_->addPath(path,QPen(color,1.7));
        if(std::abs(weight-1)>1e-6){
            auto* text=scene_->addSimpleText(QString::number(weight,'g',3)+"×");
            text->setBrush(color);
            auto midpoint=path.pointAtPercent(.5);text->setPos(midpoint.x()-text->boundingRect().width()/2,midpoint.y()+2);
        }
    };
    for(int i=0;i<count;++i){
        for(auto v:nodes[i].toObject().value("inputs").toArray()){
            auto e=v.toObject();int from=e.value("source").toInt(-2);
            if(from==-1|| (from>=0&&from<count))drawEdge((from==-1?input:positions[from])+QPointF(from==-1?100:156,26),positions[i]+QPointF(0,26),e.value("weight").toDouble());
        }
    }
    if(out==-1|| (out>=0&&out<count))drawEdge((out==-1?input:positions[out])+QPointF(out==-1?100:156,26),dest+QPointF(0,26),1);
    auto card=[this](QPointF p,QString text,int id,int width,QColor fill){
        QPainterPath shape;shape.addRoundedRect(QRectF(p,QSizeF(width,52)),8,8);
        auto* box=scene_->addPath(shape,QPen(id==selected_&&id>=0?QColor("#a9dfd3"):QColor("#61737e"),id==selected_&&id>=0?2:1),QBrush(fill));
        if(id>=0){box->setFlag(QGraphicsItem::ItemIsSelectable);box->setData(0,id);box->setSelected(id==selected_);}
        auto* label=new QGraphicsSimpleTextItem(QFontMetrics(QApplication::font()).elidedText(text,Qt::ElideRight,width-16),box);
        for(double x:{p.x(),p.x()+width}){
            if((id==-1&&text=="INPUT"&&x==p.x())||(id==-1&&text=="OUTPUT"&&x!=p.x()))continue;
            auto* port=new QGraphicsEllipseItem(x-3,p.y()+23,6,6,box);
            port->setPen(QPen(QColor("#98c8ba")));port->setBrush(QColor("#66b8aa"));
        }label->setBrush(QColor("#e9eef1"));label->setPos(p+QPointF(8,16));
    };
    card(input,"INPUT",-1,100,QColor("#29463f"));
    for(int i=0;i<count;++i){
        auto id=nodes[i].toObject().value("effect").toString();auto e=metadata(id);
        QString title=QString::number(i+1)+" · "+effectName(e);
        output_->addItem(title,i);card(positions[i],title.left(24),i,156,e.value("format").toString()=="ladspa"?QColor("#30415c"):QColor("#3b3651"));
    }
    card(dest,"OUTPUT",-1,100,QColor("#29463f"));
    output_->setCurrentIndex(output_->findData(out));scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-8,-8,8,8));
    graph_->resetTransform();
    const auto bounds=scene_->sceneRect();
    const double factor=std::clamp(std::min(graph_->viewport()->width()/bounds.width(),graph_->viewport()->height()/bounds.height()),.85,1.2);
    graph_->scale(factor,factor);graph_->centerOn(bounds.center());
}
void EffectBuilder::replaceNode(const QJsonObject& n){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    nodes[selected_]=n;draft_["nodes"]=nodes;
}
void EffectBuilder::inspect(){
    while(parameters_->rowCount())parameters_->removeRow(0);
    edges_->clearContents();edges_->setRowCount(0);
    auto nodes=draft_.value("nodes").toArray();
    if(selected_<0||selected_>=nodes.size()){parameters_->addRow(new QLabel("Select a node to edit its parameters."));return;}
    auto n=nodes[selected_].toObject();auto meta=metadata(n.value("effect").toString());
    auto* heading=new QLabel(effectName(meta));heading->setTextFormat(Qt::PlainText);parameters_->addRow(heading);
    auto params=n.value("params").toObject();
    for(auto value:meta.value("parameters").toArray()){
        auto p=value.toObject();auto key=p.value("key").toString(),label=p.value("name").toString();
        auto set=[this,key](double v){auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;auto n=nodes[selected_].toObject();auto params=n.value("params").toObject();params[key]=v;n["params"]=params;replaceNode(n);};
        auto initial=params.value(key).toDouble(p.value("default").toDouble());
        if(p.value("type")=="choice"){
            auto* choice=new QComboBox;choice->setObjectName("param_"+key);
            for(auto c:p.value("choices").toArray()){double v=c.toObject().value("value").toDouble();choice->addItem(QString::number(v),v);}
            choice->setCurrentIndex(choice->findData(initial));
            connect(choice,&QComboBox::currentIndexChanged,this,[choice,set]{if(choice->currentIndex()>=0)set(choice->currentData().toDouble());});parameters_->addRow(label,choice);
        }else{
            auto* spin=new QDoubleSpinBox;spin->setObjectName("param_"+key);spin->setDecimals(4);
            const double step=p.value("step").toDouble();spin->setDecimals(std::clamp(int(std::ceil(-std::log10(std::max(.0001,step)))),0,4));
            spin->setRange(p.value("min").toDouble(),p.value("max").toDouble());spin->setSingleStep(std::max(.0001,p.value("step").toDouble()));spin->setSuffix(p.value("unit").toString());spin->setValue(initial);
            connect(spin,&QDoubleSpinBox::valueChanged,this,set);parameters_->addRow(label,spin);
        }
    }
    auto inputs=n.value("inputs").toArray();edges_->setRowCount(std::min<qsizetype>(inputs.size(),8));
    for(int row=0;row<edges_->rowCount();++row){
        auto e=inputs[row].toObject();auto* from=new QComboBox;from->addItem("Input",-1);
        for(int i=0;i<std::min<qsizetype>(nodes.size(),16);++i)if(i!=selected_)from->addItem(QString::number(i+1)+" · "+effectName(metadata(nodes[i].toObject().value("effect").toString())),i);
        from->setCurrentIndex(from->findData(e.value("source").toInt(-2)));
        auto* weight=new QDoubleSpinBox;weight->setRange(-4,4);weight->setSingleStep(.1);weight->setValue(e.value("weight").toDouble());
        auto change=[this,row,from,weight]{
            auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size()||from->currentIndex()<0)return;
            auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();if(row>=inputs.size())return;
            inputs[row]=QJsonObject{{"source",from->currentData().toInt()},{"weight",weight->value()}};
            n["inputs"]=inputs;replaceNode(n);redraw();
        };
        connect(from,&QComboBox::currentIndexChanged,this,change);connect(weight,&QDoubleSpinBox::valueChanged,this,change);
        edges_->setCellWidget(row,0,from);edges_->setCellWidget(row,1,weight);
    }
}
void EffectBuilder::addNode(){
    auto* item=library_->currentItem();if(!item||!(item->flags()&Qt::ItemIsEnabled))return;
    auto nodes=draft_.value("nodes").toArray();if(nodes.size()>=16){message("16-node limit.",true);return;}
    int previous=draft_.value("output").toInt(-1);
    nodes.append(QJsonObject{{"effect",item->data(Qt::UserRole).toString()},{"params",QJsonObject{}},{"inputs",QJsonArray{QJsonObject{{"source",previous},{"weight",1}}}}});
    selected_=nodes.size()-1;draft_["nodes"]=nodes;draft_["output"]=selected_;redraw();inspect();message("Draft updated. Live filter unchanged.");
}
void EffectBuilder::addEdge(){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();if(inputs.size()>=8){message("Eight inputs per node.",true);return;}
    inputs.append(QJsonObject{{"source",-1},{"weight",.5}});n["inputs"]=inputs;replaceNode(n);redraw();inspect();
}
void EffectBuilder::removeEdge(){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();int row=edges_->currentRow();
    if(row<0||inputs.size()<=1)return;inputs.removeAt(row);n["inputs"]=inputs;replaceNode(n);redraw();inspect();
}
void EffectBuilder::removeNode(){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    // Only remove the final output leaf with one input; never silently rewrite macros or parallel branches.
    if(selected_!=nodes.size()-1||draft_.value("output").toInt()!=selected_){message("Remove the last output node first.",true);return;}
    auto inputs=nodes.last().toObject().value("inputs").toArray();
    if(inputs.size()!=1){message("Disconnect extra inputs before removing the output.",true);return;}
    for(auto c:draft_.value("controls").toArray())for(auto b:c.toObject().value("bindings").toArray())
        if(b.toObject().value("node").toInt()==selected_){message("This node has exposed bindings; remove them in the graph JSON first.",true);return;}
    for(int i=0;i<selected_;++i)for(auto v:nodes[i].toObject().value("inputs").toArray())
        if(v.toObject().value("source").toInt()==selected_){message("Node is still referenced.",true);return;}
    draft_["output"]=inputs[0].toObject().value("source").toInt(-1);nodes.removeLast();draft_["nodes"]=nodes;
    selected_=-1;redraw();inspect();
}
void EffectBuilder::loadTarget(){
    auto* target=find_target(target_);if(!target){message("Select an existing HyperDSP Graph filter.",true);return;}
    auto* settings=obs_source_get_settings(target);auto doc=QJsonDocument::fromJson(obs_data_get_string(settings,"graph_json"));obs_data_release(settings);obs_source_release(target);
    if(!doc.isObject()){message("Target contains invalid JSON. Restore it from filter properties first.",true);return;}
    setDraft(doc.object());message("Loaded target draft. Live filter unchanged.");
}
void EffectBuilder::apply(bool validateOnly){
    auto* target=find_target(target_);if(!target){message("Select an existing HyperDSP Graph filter.",true);return;}
    auto bytes=QJsonDocument(draft_).toJson(QJsonDocument::Compact);
    calldata_t data;calldata_init(&data);calldata_set_string(&data,"graph",bytes.constData());calldata_set_bool(&data,"validate_only",validateOnly);
    bool called=proc_handler_call(obs_source_get_proc_handler(target),"hyperdsp_graph_apply_v1",&data);
    bool ok=called&&calldata_bool(&data,"ok");
    const char* status=nullptr;calldata_get_string(&data,"status",&status);
    message(status?QString::fromUtf8(status):"Graph transaction unavailable.",!ok);
    calldata_free(&data);obs_source_release(target);
}
namespace {
QPointer<EffectBuilder> dock;
bool registered=false;
void frontend_event(enum obs_frontend_event event,void*){
    if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING&&!dock&&qobject_cast<QApplication*>(QCoreApplication::instance())){
        auto* widget=new EffectBuilder;
        if(obs_frontend_add_dock_by_id("hyperdsp.effect_builder","HyperDSP Effect Builder",widget)){
            dock=widget;blog(LOG_INFO,"[HyperDSP] Effect Builder dock registered");
        }else {blog(LOG_WARNING,"[HyperDSP] Effect Builder dock registration rejected");delete widget;}
    }else if(event==OBS_FRONTEND_EVENT_EXIT&&dock){
        obs_frontend_remove_dock("hyperdsp.effect_builder");if(dock)delete dock.data();dock=nullptr;
    }
}
}
void register_effect_builder(){
    if(registered||!qobject_cast<QApplication*>(QCoreApplication::instance())||!obs_frontend_get_main_window())return;
    obs_frontend_add_event_callback(frontend_event,nullptr);registered=true;blog(LOG_INFO,"[HyperDSP] Effect Builder dock deferred to frontend startup");
}
void unregister_effect_builder(){
    if(registered){obs_frontend_remove_event_callback(frontend_event,nullptr);registered=false;}
    if(dock){obs_frontend_remove_dock("hyperdsp.effect_builder");if(dock)delete dock.data();dock=nullptr;}
}
}
