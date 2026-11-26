#include "effect_builder.hpp"
#include "audition_panel.hpp"
#include "effect-metadata.hpp"
#include "framework/dsp/monitor_spectrum.hpp"
#include "framework/dsp/graph_preview.hpp"
#include <future>
#include <chrono>
#include <QStackedWidget>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include "interop/external_inputs.hpp"
#include <QStandardItemModel>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
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
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainterPathStroker>
#include <QMenu>
#include <functional>
#include <set>
#include <QTabWidget>
#include <QUuid>
#include <cmath>
#include <cstring>
#include <jansson.h>
namespace hyperdsp {
namespace {
QString qs(const std::string& s){return QString::fromUtf8(s.data(),qsizetype(s.size()));}
QJsonObject json(obs_data_t* data){return QJsonDocument::fromJson(obs_data_get_json(data)).object();}
QString effectName(const QJsonObject& item){return item.value("name").toString(item.value("id").toString());}
QPushButton* button(const QString& text,const char* name,QWidget* parent){
    auto* p=new QPushButton(text,parent);p->setObjectName(name);return p;
}
// The host palette is authoritative. Blend semantic roles, never fixed hues.
QColor blend(QColor a,QColor b,double t){
    return QColor::fromRgbF(a.redF()*(1-t)+b.redF()*t,a.greenF()*(1-t)+b.greenF()*t,a.blueF()*(1-t)+b.blueF()*t);
}
double luminance(QColor c){
    auto linear=[](double x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);};
    return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
}
double contrast(QColor a,QColor b){
    double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
QColor readable(QColor preferred,QColor bg,QColor alternative,double minimum){
    if(contrast(preferred,bg)>=minimum)return preferred;
    for(int i=1;i<=20;++i){auto candidate=blend(preferred,alternative,i/20.);if(contrast(candidate,bg)>=minimum)return candidate;}
    // If the host roles themselves have insufficient contrast, use the
    // achromatic endpoint that is readable against the actual surface.
    QColor neutral=luminance(bg)>.179?Qt::black:Qt::white;
    for(int i=1;i<=20;++i){auto candidate=blend(preferred,neutral,i/20.);if(contrast(candidate,bg)>=minimum)return candidate;}
    return neutral;
}
struct GraphColors {
    QColor base,text,accent,edge,outline,terminal,native,external;
    explicit GraphColors(const QPalette& p){
        base=p.color(QPalette::Base);text=p.color(QPalette::Text);
        accent=readable(p.color(QPalette::Highlight),base,text,3);
        edge=readable(p.color(QPalette::Link),base,text,3);
        outline=readable(p.color(QPalette::Mid),base,text,3);
        terminal=blend(base,accent,.16);native=blend(base,p.color(QPalette::Button),.65);
        external=blend(base,p.color(QPalette::Link),.12);
    }
};
class MonitorPlot final:public QWidget {
public:
    GraphMonitorFrame frame{};
    MonitorSpectrum analyzer;
    MonitorSpectrum::Bins input{},output{};
    bool valid=false;
    explicit MonitorPlot(QWidget* parent):QWidget(parent){setMinimumHeight(200);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    void setFrame(const GraphMonitorFrame& next){
        if(!valid||frame.sequence!=next.sequence||frame.epoch!=next.epoch){
            frame=next;input=analyzer.analyze(frame.input);output=analyzer.analyze(frame.output);
        }
        valid=true;update();
    }
    void clear(){valid=false;update();}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        GraphColors colors(palette());
        QColor in=readable(palette().color(QPalette::Link),colors.base,colors.text,3);
        QColor out=colors.accent;
        p.fillRect(rect(),colors.base);p.setPen(colors.text);
        const int left=43,right=9,top=23,bottom=19;
        const double w=width()-left-right,h=(height()-2*(top+bottom))/2.;
        if(w<20||h<20)return;
        QRectF wave(left,top,w,h),spectrum(left,top+bottom+h+top,w,h);
        auto line=[&](QRectF area,double y){p.setPen(QPen(blend(colors.base,colors.text,.19),1));p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));};
        auto label=[&](QRectF area,QString text){p.setPen(colors.text);p.drawText(QRectF(4,area.top()-20,width()-8,18),Qt::AlignLeft|Qt::AlignVCenter,text);};
        label(wave,valid?QString("Waveform · channel 1 · %1 ms").arg(1000.*monitor_samples/frame.rate,0,'f',1):QString("Waveform · channel 1"));label(spectrum,"Spectrum · dBFS peak amplitude");
        for(double value:{1.,0.,-1.}){
            double y=wave.center().y()-value*wave.height()/2;line(wave,y);
            p.setPen(colors.text);p.drawText(QRectF(1,y-8,left-5,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(value));
        }
        for(int db:{0,-30,-60,-90}){
            double y=spectrum.top()+(-db/90.)*h;line(spectrum,y);
            p.setPen(colors.text);p.drawText(QRectF(1,y-8,left-5,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(db));
        }
        if(!valid){
            p.setPen(colors.text);p.drawText(wave,Qt::AlignCenter,"Waiting for audio");return;
        }
        auto wavePath=[&](const auto& samples,QColor color,Qt::PenStyle style){
            QPainterPath path;
            for(unsigned i=0;i<samples.size();++i){
                QPointF point(wave.left()+w*i/(samples.size()-1),wave.center().y()-std::clamp(samples[i],-1.f,1.f)*h/2);
                if(!i)path.moveTo(point);else path.lineTo(point);
            }
            p.save();p.setClipRect(wave);p.setPen(QPen(color,1.4,style));p.drawPath(path);p.restore();
        };
        wavePath(frame.input,in,Qt::DashLine);wavePath(frame.output,out,Qt::SolidLine);
        const double low=std::max(20.,double(frame.rate)/monitor_samples),high=frame.rate/2.;
        auto x=[&](double hz){return spectrum.left()+w*std::log(hz/low)/std::log(high/low);};
        for(double hz:{100.,1000.,10000.})if(hz>low&&hz<high){
            double xx=x(hz);p.setPen(QPen(blend(colors.base,colors.text,.19),1));p.drawLine(QPointF(xx,spectrum.top()),QPointF(xx,spectrum.bottom()));
            p.setPen(colors.text);p.drawText(QRectF(xx-20,spectrum.bottom()+1,40,16),Qt::AlignHCenter,hz>=1000?QString::number(hz/1000)+"k":QString::number(hz));
        }
        auto spectrumPath=[&](const auto& bins,QColor color,Qt::PenStyle style){
            QPainterPath path;bool first=true;
            for(unsigned i=1;i<bins.size();++i){
                double hz=double(i)*frame.rate/monitor_samples;if(hz<low)continue;
                QPointF point(x(hz),spectrum.top()+h*(-std::clamp(bins[i],-90.f,0.f)/90.));
                if(first){path.moveTo(point);first=false;}else path.lineTo(point);
            }
            p.save();p.setClipRect(spectrum);p.setPen(QPen(color,1.5,style));p.drawPath(path);p.restore();
        };
        spectrumPath(input,in,Qt::DashLine);spectrumPath(output,out,Qt::SolidLine);
    }
};
class MonitorPanel final:public QWidget {
    MonitorPlot* plot_;
    QLabel *status_,*controls_;
    QTimer timer_;
    std::function<bool(GraphMonitorFrame&)> read_;
public:
    MonitorPanel(std::function<bool(GraphMonitorFrame&)> read,QWidget* parent=nullptr):QWidget(parent),read_(std::move(read)){
        setObjectName("graphMonitor");setProperty("hasFrame",false);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(3,3,3,3);layout->setSpacing(3);
        status_=new QLabel("Waiting for applied graph audio");status_->setObjectName("monitorStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);
        layout->addWidget(status_);plot_=new MonitorPlot(this);layout->addWidget(plot_,1);
        auto* legend=new QLabel("Input — dashed   Output — solid · applied graph, not draft");legend->setWordWrap(true);layout->addWidget(legend);
        controls_=new QLabel;controls_->setTextFormat(Qt::PlainText);controls_->setWordWrap(true);layout->addWidget(controls_);
        timer_.setInterval(50);connect(&timer_,&QTimer::timeout,this,[this]{poll();});
    }
    void reset(){
        plot_->clear();setProperty("hasFrame",false);status_->setText("Waiting for applied graph audio");controls_->clear();
    }
    void poll(){
        GraphMonitorFrame frame{};
        if(!read_(frame)||frame.rate<8000||frame.rate>192000||frame.control_count>8){reset();return;}
        if(plot_->valid&&plot_->frame.sequence==frame.sequence&&plot_->frame.epoch==frame.epoch)return;
        plot_->setFrame(frame);setProperty("hasFrame",true);
        auto peak=[](const auto& samples){float max=0;for(float v:samples)max=std::max(max,std::abs(v));return 20*std::log10(std::max(max,1e-6f));};
        status_->setText(QString("Live · %1 kHz · Ch 1 peak: in %2 / out %3 dBFS").arg(frame.rate/1000.,0,'g',4).arg(peak(frame.input),0,'f',1).arg(peak(frame.output),0,'f',1));
        QStringList values;
        for(unsigned i=0;i<frame.control_count;++i){auto& c=frame.controls[i];values<<QString("%1: %2 → %3").arg(QString::fromUtf8(c.name.data(),strnlen(c.name.data(),c.name.size()))).arg(c.base,0,'f',2).arg(c.effective,0,'f',2);}
        controls_->setText(values.isEmpty()?QString():QString("Controls · base → effective\n")+values.join("   |   "));
    }
protected:
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);poll();timer_.start();}
    void hideEvent(QHideEvent* event)override{timer_.stop();reset();QWidget::hideEvent(event);}
};
class ResponsePlot final:public QWidget {
    std::vector<PreviewPoint> points_;
public:
    explicit ResponsePlot(QWidget* parent):QWidget(parent){setMinimumHeight(170);}
    void setResponse(const std::vector<PreviewPoint>& points){points_=points;update();}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);GraphColors c(palette());
        p.fillRect(rect(),c.base);p.setPen(c.text);
        QRectF area(45,23,width()-54,height()-46);
        p.drawText(QRectF(4,2,width()-8,18),Qt::AlignLeft,"Measured magnitude · dB");
        if(points_.empty()||area.width()<20||area.height()<20)return;
        double lo=-60,hi=12;
        for(auto v:points_){lo=std::min(lo,std::max(-120.,v.db));hi=std::max(hi,v.db);}
        lo=std::floor(lo/12)*12;hi=std::ceil(hi/12)*12;
        double step=(hi-lo)>108?24:12;
        auto x=[&](double hz){return area.left()+area.width()*std::log(hz/points_.front().hz)/std::log(points_.back().hz/points_.front().hz);};
        auto y=[&](double db){return area.top()+area.height()*(hi-std::clamp(db,lo,hi))/(hi-lo);};
        for(double db=std::ceil(lo/step)*step;db<=hi;db+=step){
            p.setPen(QPen(blend(c.base,c.text,.20),db==0?1.5:1));p.drawLine(QPointF(area.left(),y(db)),QPointF(area.right(),y(db)));
            p.setPen(c.text);p.drawText(QRectF(0,y(db)-8,40,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(db));
        }
        for(double hz:{20.,100.,1000.,10000.})if(hz>=points_.front().hz&&hz<=points_.back().hz){
            double xx=x(hz);p.setPen(QPen(blend(c.base,c.text,.20),1));p.drawLine(QPointF(xx,area.top()),QPointF(xx,area.bottom()));
            p.setPen(c.text);p.drawText(QRectF(xx-20,area.bottom()+1,40,16),Qt::AlignHCenter,hz>=1000?QString::number(hz/1000)+"k":QString::number(hz));
        }
        QPainterPath path;bool first=true;
        for(auto v:points_){QPointF pt(x(v.hz),y(v.db));if(first){path.moveTo(pt);first=false;}else path.lineTo(pt);}
        p.save();p.setClipRect(area.adjusted(-1,-1,1,1));p.setPen(QPen(c.accent,2));p.drawPath(path);p.restore();
    }
};
class PreviewPanel final:public QWidget {
    std::function<QByteArray()> draft_;
    QByteArray captured_;
    std::future<GraphPreview> future_;
    GraphPreview result_;
    QTimer timer_;
    QPushButton* run_;
    QComboBox* mode_;
    QDoubleSpinBox *hz_,*level_;
    QLabel *status_,*detail_;
    QStackedWidget* plots_;
    ResponsePlot* response_;
    MonitorPlot* tone_;
    bool completed_=false;
    unsigned rate_=0,channels_=0;
    double frequency_=0,level_db_=0;
    bool matches()const{
        obs_audio_info ai{};
        return draft_()==captured_&&hz_->value()==frequency_&&level_->value()==level_db_&&
            obs_get_audio_info(&ai)&&ai.samples_per_sec==rate_&&get_audio_channels(ai.speakers)==channels_;
    }
    void clearPlots(){response_->setResponse({});tone_->clear();detail_->clear();}
    void poll(){
        if(future_.valid()){
            if(future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            try{result_=future_.get();}catch(const std::exception& e){result_={};result_.error=e.what();}
            run_->setEnabled(true);completed_=true;
            if(!matches()){clearPlots();status_->setText("Draft or signal changed — run Preview again.");return;}
            if(!result_.ok){clearPlots();status_->setText("Error: "+qs(result_.error));return;}
            response_->setResponse(result_.response);tone_->clear();tone_->setFrame(result_.frame);
            mode_->clear();if(result_.linear)mode_->addItem("Response",0);mode_->addItem("Test tone",1);
            mode_->setCurrentIndex(0);
            status_->setText(result_.linear?"Response · isolated draft":"Tone · nonlinear or time-varying graph");
            QString detail=QString("Ch 1 RMS: %1 → %2 dBFS · peak %3 dBFS · %4 samples known latency")
                .arg(result_.input_rms_db,0,'f',1).arg(result_.output_rms_db,0,'f',1).arg(result_.output_peak_db,0,'f',1).arg(result_.latency);
            if(result_.tail_warning)detail+=" · IR tail may be truncated";
            if(result_.external_routes)detail+=" · external modulation held at zero";
            detail_->setText(detail);return;
        }
        if(completed_&&!matches()){clearPlots();completed_=false;status_->setText("Draft or signal changed — run Preview again.");}
    }
    void run(){
        if(future_.valid())return;
        obs_audio_info ai{};
        if(!obs_get_audio_info(&ai)){clearPlots();completed_=false;status_->setText("Error: OBS audio format unavailable");return;}
        rate_=ai.samples_per_sec;channels_=get_audio_channels(ai.speakers);
        captured_=draft_();frequency_=hz_->value();level_db_=level_->value();completed_=false;
        clearPlots();run_->setEnabled(false);status_->setText("Rendering isolated draft…");
        try{
            future_=std::async(std::launch::async,[raw=captured_.toStdString(),rate=rate_,channels=channels_,hz=frequency_,level=level_db_]{
                return preview_graph(raw,rate,channels,hz,level);
            });
        }catch(const std::exception& e){run_->setEnabled(true);status_->setText("Error: "+QString::fromUtf8(e.what()));}
    }
public:
    explicit PreviewPanel(std::function<QByteArray()> draft,QWidget* parent=nullptr):QWidget(parent),draft_(std::move(draft)){
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(3,3,3,3);layout->setSpacing(4);
        auto* row=new QHBoxLayout;
        hz_=new QDoubleSpinBox;hz_->setObjectName("previewFrequency");hz_->setRange(20,20000);hz_->setValue(1000);hz_->setSuffix(" Hz");hz_->setDecimals(1);hz_->setToolTip("Synthetic test-tone frequency");
        level_=new QDoubleSpinBox;level_->setObjectName("previewLevel");level_->setRange(-60,0);level_->setValue(-12);level_->setSuffix(" dBFS");level_->setDecimals(1);level_->setToolTip("Synthetic sine peak amplitude");
        run_=button("Preview","runPreview",this);row->addWidget(hz_,1);row->addWidget(level_,1);row->addWidget(run_);layout->addLayout(row);
        auto* view=new QHBoxLayout;mode_=new QComboBox;mode_->setObjectName("previewMode");mode_->addItem("Test tone",1);
        status_=new QLabel("Preview uses draft defaults; no live audio changes.");status_->setObjectName("previewStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);
        view->addWidget(mode_);view->addWidget(status_,1);layout->addLayout(view);
        plots_=new QStackedWidget;response_=new ResponsePlot(this);tone_=new MonitorPlot(this);
        plots_->addWidget(response_);plots_->addWidget(tone_);plots_->setCurrentIndex(1);layout->addWidget(plots_,1);
        detail_=new QLabel;detail_->setTextFormat(Qt::PlainText);detail_->setWordWrap(true);layout->addWidget(detail_);
        auto* hint=new QLabel("Synthetic 0.5 s tone · draft defaults · native only · silent preview (no playback)");hint->setWordWrap(true);layout->addWidget(hint);
        connect(mode_,&QComboBox::currentIndexChanged,this,[this]{if(mode_->currentIndex()>=0)plots_->setCurrentIndex(mode_->currentData().toInt());});
        connect(run_,&QPushButton::clicked,this,[this]{run();});
        timer_.setInterval(50);connect(&timer_,&QTimer::timeout,this,[this]{poll();});
    }
protected:
    void showEvent(QShowEvent* e)override{QWidget::showEvent(e);poll();timer_.start();}
    void hideEvent(QHideEvent* e)override{timer_.stop();QWidget::hideEvent(e);}
};
// Scene item identities are UI-only; the DSP graph still uses schema-1 indices.
constexpr int input_id=-1,output_id=-2;
QPainterPath cablePath(QPointF a,QPointF b,double laneHint=-10000){
    QPainterPath path(a);const double gap=b.x()-a.x();
    if(gap>65||gap<0){
        const double lane=std::max(std::max(a.y(),b.y())+48,laneHint);
        path.cubicTo(a+QPointF(23,0),QPointF(a.x()+18,lane),QPointF(a.x()+40,lane));
        path.lineTo(QPointF(b.x()-40,lane));
        path.cubicTo(QPointF(b.x()-18,lane),b-QPointF(23,0),b);
    }else{
        double dx=std::clamp(gap*.45,8.,40.);
        path.cubicTo(a+QPointF(dx,0),b-QPointF(dx,0),b);
    }
    return path;
}
class CableItem final:public QGraphicsPathItem {
public:
    explicit CableItem(QPainterPath path):QGraphicsPathItem(path){}
    QPainterPath shape()const override{
        QPainterPathStroker stroke;stroke.setWidth(12);return stroke.createStroke(path());
    }
    QRectF boundingRect()const override{return QGraphicsPathItem::boundingRect().adjusted(-6,-6,6,6);}
    void paint(QPainter* painter,const QStyleOptionGraphicsItem*,QWidget*)override{
        auto stroke=pen();if(isSelected()){stroke.setColor(data(4).value<QColor>());stroke.setWidthF(3);}
        painter->setPen(stroke);painter->setBrush(Qt::NoBrush);painter->drawPath(path());
    }
};
class Canvas final:public QGraphicsView {
public:
    explicit Canvas(QGraphicsScene* scene):QGraphicsView(scene){
        setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }
    std::function<void(int,int)> connectPorts,disconnectCable;
    std::function<void(int,QPointF)> moved;
    std::function<void()> finishedMove;
protected:
    QGraphicsItem* hit(QPoint point,const QString& role)const{
        for(auto* i:items(point))if(i->data(1).toString()==role)return i;
        return nullptr;
    }
    void mousePressEvent(QMouseEvent* e)override{
        if(e->button()==Qt::LeftButton){
            setFocus();auto p=e->position().toPoint();
            if(auto* port=hit(p,"out")){
                origin_=port->data(0).toInt();start_=port->sceneBoundingRect().center();
                preview_=scene()->addPath(cablePath(start_,start_),QPen(GraphColors(palette()).accent,2,Qt::DashLine));
                preview_->setData(1,"preview");preview_->setZValue(5);viewport()->setCursor(Qt::CrossCursor);e->accept();return;
            }
            if(auto* node=hit(p,"node")){
                if(node->flags()&QGraphicsItem::ItemIsSelectable){
                    scene()->clearSelection();node->setSelected(true);
                }
                moving_=node;offset_=mapToScene(p)-node->pos();viewport()->setCursor(Qt::ClosedHandCursor);e->accept();return;
            }
        }
        if(e->button()==Qt::LeftButton)if(auto* cable=hit(e->position().toPoint(),"cable")){
            scene()->clearSelection();cable->setSelected(true);e->accept();return;
        }
        QGraphicsView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e)override{
        auto p=e->position().toPoint();
        if(preview_){
            auto* target=hit(p,"in");
            preview_->setPath(cablePath(start_,target?target->sceneBoundingRect().center():mapToScene(p)));
            e->accept();return;
        }
        if(moving_){
            auto pos=mapToScene(p)-offset_;
            pos.setX(std::clamp(pos.x(),-5000.,5000.));pos.setY(std::clamp(pos.y(),-5000.,5000.));
            moving_->setPos(pos);if(moved)moved(moving_->data(0).toInt(),pos);
            e->accept();return;
        }
        QGraphicsView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e)override{
        if(e->button()==Qt::LeftButton&&preview_){
            auto* target=hit(e->position().toPoint(),"in");
            const int to=target?target->data(0).toInt():-3;
            cancel();if(to!=-3&&connectPorts)connectPorts(origin_,to);
            e->accept();return;
        }
        if(e->button()==Qt::LeftButton&&moving_){
            moving_=nullptr;viewport()->unsetCursor();if(finishedMove)finishedMove();e->accept();return;
        }
        QGraphicsView::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent* e)override{
        if(e->key()==Qt::Key_Escape&&preview_){cancel();e->accept();return;}
        if(e->key()==Qt::Key_Delete||e->key()==Qt::Key_Backspace){
            for(auto* i:scene()->selectedItems())if(i->data(1).toString()=="cable"){
                int to=i->data(0).toInt(),row=i->data(2).toInt();
                if(disconnectCable)disconnectCable(to,row);e->accept();return;
            }
        }
        QGraphicsView::keyPressEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent* e)override{
        auto* cable=hit(e->pos(),"cable");
        if(!cable)return QGraphicsView::contextMenuEvent(e);
        const int to=cable->data(0).toInt(),row=cable->data(2).toInt();
        QMenu menu(this);auto* remove=menu.addAction("Disconnect cable");remove->setEnabled(to>=0);
        if(menu.exec(e->globalPos())==remove&&disconnectCable)disconnectCable(to,row);
    }
    void focusOutEvent(QFocusEvent* e)override{
        if(preview_||moving_)cancel();
        QGraphicsView::focusOutEvent(e);
    }
    void wheelEvent(QWheelEvent* e)override{
        if(e->modifiers()&Qt::ControlModifier){
            const double current=transform().m11();
            const double factor=std::clamp(current*std::pow(1.0015,e->angleDelta().y()),.35,2.5)/current;
            scale(factor,factor);e->accept();return;
        }
        QGraphicsView::wheelEvent(e);
    }
public:
    void cancel(){
        if(preview_){delete preview_;preview_=nullptr;}
        moving_=nullptr;viewport()->unsetCursor();
    }
private:
    QGraphicsPathItem* preview_=nullptr;
    QGraphicsItem* moving_=nullptr;
    int origin_=-3;
    QPointF start_,offset_;
};
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
namespace {
QString storage_path(const QString& supplied,bool bundled){
    if(!supplied.isEmpty())return supplied;
    char* path=bundled?obs_module_file("graphs"):obs_module_config_path("effect-builder");
    if(!path)return {};
    auto result=QString::fromUtf8(path);bfree(path);return result;
}
}
EffectBuilder::EffectBuilder(QWidget* parent,QString storagePath,QString bundledPath):QWidget(parent),draft_(empty()),
    storage_(storage_path(storagePath,false)),bundledPath_(storage_path(bundledPath,true)){
    setObjectName("hyperdspEffectBuilder");
    // Let OBS provide all widget styling, including custom theme QSS.
    auto* root=new QVBoxLayout(this);root->setContentsMargins(6,6,6,6);root->setSpacing(5);
    auto* targetRow=new QHBoxLayout;
    target_=new QComboBox;target_->setObjectName("target");target_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);target_->setMinimumContentsLength(12);
    auto* refresh=button("Refresh","refreshTargets",this),*load=button("Load","loadTarget",this);
    targetRow->addWidget(target_,1);targetRow->addWidget(refresh);targetRow->addWidget(load);root->addLayout(targetRow);
    auto* tools=new QHBoxLayout;
    auto* open=button("Import","importGraph",this),*save=button("Export","exportGraph",this),*validate=button("Validate","validateGraph",this),*applyBtn=button("Apply","applyGraph",this);
    tools->addWidget(open);tools->addWidget(save);tools->addStretch();tools->addWidget(validate);tools->addWidget(applyBtn);root->addLayout(tools);
    applyBtn->setDefault(true);
    auto* split=new QSplitter(Qt::Vertical);
    auto* viewPanel=new QWidget;auto* viewLayout=new QVBoxLayout(viewPanel);viewLayout->setContentsMargins(0,0,0,0);
    scene_=new QGraphicsScene(this);auto* canvas=new Canvas(scene_);graph_=canvas;
    canvas->connectPorts=[this](int from,int to){connectPorts(from,to);};
    canvas->disconnectCable=[this](int to,int row){disconnectCable(to,row);};
    canvas->moved=[this](int id,QPointF pos){updateNodePosition(id,pos);};
    canvas->finishedMove=[this]{scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-30,-30,30,30));};graph_->setObjectName("graph");graph_->setRenderHint(QPainter::Antialiasing);
    graph_->setBackgroundBrush(palette().color(QPalette::Base));graph_->setDragMode(QGraphicsView::ScrollHandDrag);graph_->setMinimumHeight(160);
    viewLayout->addWidget(graph_,1);
    auto* outputRow=new QHBoxLayout;outputRow->addWidget(new QLabel("Output"));
    output_=new QComboBox;output_->setObjectName("outputNode");outputRow->addWidget(output_,1);
    auto* fit=button("Fit","fitCanvas",this),*layout=button("Layout","autoLayout",this);
    fit->setToolTip("Fit graph · Ctrl+wheel to zoom");layout->setToolTip("Reset node positions");
    outputRow->addWidget(fit);outputRow->addWidget(layout);
    connect(fit,&QPushButton::clicked,this,[this]{fitCanvas();});
    connect(layout,&QPushButton::clicked,this,[this]{positions_.clear();redraw();fitCanvas();});
    auto* remove=button("Remove node","removeNode",this);outputRow->addWidget(remove);viewLayout->addLayout(outputRow);
    split->addWidget(viewPanel);
    auto* tabs=new QTabWidget;tabs->setObjectName("builderTabs");auto* libraryPanel=new QWidget;auto* libLayout=new QVBoxLayout(libraryPanel);libLayout->setContentsMargins(3,3,3,3);
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
    tabs->addTab(inspector,"Inspector");
    auto* macroPanel=new QWidget;auto* macroLayout=new QVBoxLayout(macroPanel);macroLayout->setContentsMargins(3,3,3,3);
    auto* macroRow=new QHBoxLayout;macros_=new QComboBox;macros_->setObjectName("macros");
    addMacro_=button("+ Control","addMacro",this);removeMacro_=button("Remove","removeMacro",this);
    macroRow->addWidget(macros_,1);macroRow->addWidget(addMacro_);macroRow->addWidget(removeMacro_);macroLayout->addLayout(macroRow);
    auto* macroForm=new QFormLayout;macroName_=new QLineEdit;macroName_->setObjectName("macroName");macroName_->setMaxLength(80);
    macroDefault_=new QDoubleSpinBox;macroDefault_->setObjectName("macroDefault");macroDefault_->setRange(0,1);macroDefault_->setDecimals(3);macroDefault_->setSingleStep(.05);
    macroForm->addRow("Name",macroName_);macroForm->addRow("Default (0–1)",macroDefault_);macroLayout->addLayout(macroForm);
    macroBindings_=new QTableWidget(0,3);macroBindings_->setObjectName("macroBindings");macroBindings_->setHorizontalHeaderLabels({"Target","At 0","At 1"});
    macroBindings_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    macroBindings_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    macroBindings_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    macroBindings_->verticalHeader()->hide();macroBindings_->setSelectionBehavior(QAbstractItemView::SelectRows);macroLayout->addWidget(macroBindings_,1);
    auto* bindingRow=new QHBoxLayout;macroTarget_=new QComboBox;macroTarget_->setObjectName("macroTarget");
    macroTarget_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);macroTarget_->setMinimumContentsLength(12);
    addBinding_=button("+ Bind","addBinding",this);removeBinding_=button("− Bind","removeBinding",this);
    bindingRow->addWidget(macroTarget_,1);bindingRow->addWidget(addBinding_);bindingRow->addWidget(removeBinding_);macroLayout->addLayout(bindingRow);
    tabs->addTab(macroPanel,"Controls");
    connect(macros_,&QComboBox::currentIndexChanged,this,[this]{inspectMacro();});
    connect(addMacro_,&QPushButton::clicked,this,[this]{addMacro();});
    connect(removeMacro_,&QPushButton::clicked,this,[this]{removeMacro();});
    connect(macroName_,&QLineEdit::editingFinished,this,[this]{
        auto name=macroName_->text().trimmed();
        if(name.isEmpty()||name.toUtf8().size()>80){message("Control name must be 1–80 UTF-8 bytes.",true);inspectMacro();return;}
        changeMacro("name",name);if(macros_->currentIndex()>=0)macros_->setItemText(macros_->currentIndex(),name);refreshRoutes();
    });
    connect(macroDefault_,&QDoubleSpinBox::valueChanged,this,[this](double v){changeMacro("default",v);});
    connect(addBinding_,&QPushButton::clicked,this,[this]{addBinding();});
    connect(removeBinding_,&QPushButton::clicked,this,[this]{removeBinding();});
    tabs->addTab(makeRoutePanel(),"Modulation");
    auto* monitor=new MonitorPanel([this](GraphMonitorFrame& frame){
        auto* source=find_target(target_);if(!source)return false;
        calldata_t d;calldata_init(&d);calldata_set_ptr(&d,"frame",&frame);
        bool ready=proc_handler_call(obs_source_get_proc_handler(source),"hyperdsp_graph_monitor_v1",&d)&&calldata_bool(&d,"available");
        calldata_free(&d);obs_source_release(source);return ready;
    });
    tabs->addTab(monitor,"Monitor");
    tabs->addTab(new PreviewPanel([this]{return QJsonDocument(draft_).toJson(QJsonDocument::Compact);}),"Preview");
    tabs->addTab(new AuditionPanel([this]{return QJsonDocument(draft_).toJson(QJsonDocument::Compact);}),"Audition");
    tabs->addTab(makePresetPanel(),"Presets");
    connect(target_,&QComboBox::currentIndexChanged,monitor,[monitor]{monitor->reset();if(monitor->isVisible())monitor->poll();});

    connect(tabs,&QTabWidget::currentChanged,this,[this](int index){if(index==3)refreshRoutes();});
    split->addWidget(tabs);split->setStretchFactor(0,1);split->setStretchFactor(1,1);root->addWidget(split,1);
    status_=new QLabel;status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);root->addWidget(status_);
    connect(refresh,&QPushButton::clicked,this,[this]{refreshTargets();});
    connect(load,&QPushButton::clicked,this,[this]{loadTarget();});
    connect(search_,&QLineEdit::textChanged,this,[this]{refreshLibrary();});
    connect(add,&QPushButton::clicked,this,[this,tabs]{addNode();if(selected_>=0)tabs->setCurrentIndex(1);});
    connect(library_,&QListWidget::itemDoubleClicked,this,[this,tabs]{addNode();if(selected_>=0)tabs->setCurrentIndex(1);});
    connect(scan,&QPushButton::clicked,this,[this]{message(qs(CatalogScan::start()));});
    connect(reload,&QPushButton::clicked,this,[this]{inventory_=inventory();refreshLibrary();inspect();refreshMacros();message(qs(CatalogScan::status()));});
    connect(scene_,&QGraphicsScene::selectionChanged,this,[this,tabs]{
        auto items=scene_->selectedItems();if(items.isEmpty())return;
        auto* item=items.first();
        if(item->data(1).toString()=="node")selected_=item->data(0).toInt();
        else if(item->data(1).toString()=="cable"&&item->data(0).toInt()>=0)selected_=item->data(0).toInt();
        else return;
        inspect();tabs->setCurrentIndex(1);
    });
    connect(output_,&QComboBox::currentIndexChanged,this,[this]{draft_["output"]=output_->currentData().toInt();redraw();});
    connect(remove,&QPushButton::clicked,this,[this]{removeNode();});
    connect(addInput,&QPushButton::clicked,this,[this]{addEdge();});
    connect(removeInput,&QPushButton::clicked,this,[this]{removeEdge();});
    connect(validate,&QPushButton::clicked,this,[this]{apply(true);});
    connect(applyBtn,&QPushButton::clicked,this,[this]{apply(false);});
    connect(open,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getOpenFileName(this,"Import graph",{},"HyperDSP graph (*.json)");
        if(path.isEmpty())return;
        BuilderDocument d;QString error;
        if(!BuilderStorage::readGraph(path,d,error)){message(error,true);return;}
        if(replaceDocument(d))message("Imported draft. Validate or apply explicitly.");
    });
    connect(save,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getSaveFileName(this,"Export graph","effect.json","HyperDSP graph (*.json)");
        if(path.isEmpty())return;QSaveFile file(path);
        auto bytes=QJsonDocument(draft_).toJson();if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit())message("Draft exported.");else message("Export failed.",true);
    });
    refreshTargets();inventory_=inventory();refreshLibrary();redraw();inspect();
    message("Add a HyperDSP Graph filter to an audio source, then select it here. Edits are drafts until Apply.");
    recoverAtStartup();refreshPresets();
    autosave_.setInterval(500);connect(&autosave_,&QTimer::timeout,this,[this]{saveRecovery();});autosave_.start();
}
EffectBuilder::~EffectBuilder(){autosave_.stop();saveRecovery();}
BuilderDocument EffectBuilder::document()const{return {draft_,positions_,{}};}
void EffectBuilder::restoreDocument(const BuilderDocument& d){
    draft_=d.graph;positions_=d.positions;
    {QSignalBlocker b(macros_);macros_->clear();}
    selected_=draft_.value("output").toInt(-1);redraw();inspect();fitCanvas();
}
void EffectBuilder::recoverAtStartup(){
    if(storage_.hasRecovery()){
        requireExplicitTarget_=true;
        BuilderDocument d;QString error;
        if(storage_.readRecovery(d,error)){
            restoreDocument(d);target_->setCurrentIndex(-1);
            message("Recovered local draft and layout. Select a target explicitly before Apply.");
            recoveryStatus_->setText("Recovered previous session.");
        }else{
            recoveryWritable_=false;target_->setCurrentIndex(-1);
            recoveryStatus_->setText("Recovery unreadable; preserved on disk. New draft archives it and re-enables autosave.");
            message("Recovery unreadable: "+error,true);
        }
    }
    lastSaved_=QJsonDocument(builder_document_json(document())).toJson(QJsonDocument::Compact);
    previousDraft_->setEnabled(storage_.hasPrevious());
}
bool EffectBuilder::saveRecovery(){
    if(!recoveryWritable_)return false;
    auto d=document();auto bytes=QJsonDocument(builder_document_json(d)).toJson(QJsonDocument::Compact);
    if(bytes==lastSaved_)return true;
    QString error;
    if(!storage_.saveRecovery(d,error)){
        recoveryStatus_->setText("Draft not saved: "+error);return false;
    }
    lastSaved_=bytes;recoveryStatus_->setText("Draft and layout saved locally.");return true;
}
bool EffectBuilder::replaceDocument(const BuilderDocument& d){
    QString error;
    if(!recoveryWritable_){message("Recovery is unreadable and preserved; choose New draft to archive it before replacing the draft.",true);return false;}
    auto old=document();
    if(old.graph!=d.graph||old.positions!=d.positions){
        if(!storage_.savePrevious(old,error)){message("Cannot preserve current draft: "+error,true);return false;}
        // Commit the new recovery before changing editor state, so an interrupted
        // transition never loses both copies.
        if(!storage_.saveRecovery(d,error)){message("Cannot save replacement draft: "+error,true);return false;}
        restoreDocument(d);lastSaved_=QJsonDocument(builder_document_json(d)).toJson(QJsonDocument::Compact);
        saveRecovery();previousDraft_->setEnabled(true);
    }
    return true;
}
QWidget* EffectBuilder::makePresetPanel(){
    auto* panel=new QWidget;auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(3,3,3,3);layout->setSpacing(4);
    auto* recoveryRow=new QHBoxLayout;
    auto* fresh=button("New draft","newDraft",panel);previousDraft_=button("Restore previous","previousDraft",panel);
    recoveryRow->addWidget(fresh);recoveryRow->addWidget(previousDraft_);layout->addLayout(recoveryRow);
    recoveryStatus_=new QLabel("Draft autosave · local only");recoveryStatus_->setWordWrap(true);recoveryStatus_->setTextFormat(Qt::PlainText);layout->addWidget(recoveryStatus_);
    presets_=new QListWidget;presets_->setObjectName("presetList");presets_->setAlternatingRowColors(true);layout->addWidget(presets_,1);
    auto* row=new QHBoxLayout;
    presetName_=new QLineEdit;presetName_->setObjectName("presetName");presetName_->setPlaceholderText("Name a new preset");presetName_->setMaxLength(80);
    auto* save=button("Save as preset","savePreset",panel);row->addWidget(presetName_,1);row->addWidget(save);layout->addLayout(row);
    auto* buttons=new QHBoxLayout;auto* load=button("Load selected","loadPreset",panel),*refresh=button("Refresh","refreshPresets",panel);
    buttons->addWidget(load,1);buttons->addWidget(refresh);layout->addLayout(buttons);
    auto* hint=new QLabel("Presets include graph and node positions. Loading replaces the draft, not the live effect. Previous draft is retained.");hint->setWordWrap(true);layout->addWidget(hint);
    connect(fresh,&QPushButton::clicked,this,[this]{
        if(!recoveryWritable_){
            QString error;if(!storage_.archiveRecovery(error)){message(error,true);return;}
            recoveryWritable_=true;lastSaved_.clear();
        }
        if(replaceDocument({empty(),{}, {}})&&saveRecovery())message("New draft. Previous draft retained.");
    });
    connect(previousDraft_,&QPushButton::clicked,this,[this]{
        BuilderDocument d;QString error;if(!storage_.readPrevious(d,error)){message(error,true);return;}
        if(replaceDocument(d))message("Previous draft restored. Live filter unchanged.");
    });
    connect(refresh,&QPushButton::clicked,this,[this]{refreshPresets();});
    connect(save,&QPushButton::clicked,this,[this]{
        QString error;auto id=storage_.savePreset(presetName_->text(),document(),error);
        if(id.isEmpty()){message(error,true);return;}
        refreshPresets();for(int i=0;i<presets_->count();++i)if(presets_->item(i)->data(Qt::UserRole).toString()==id){presets_->setCurrentRow(i);break;}
        message("Saved a new local preset.");
    });
    auto loadSelected=[this]{
        auto* item=presets_->currentItem();if(!item){message("Select a preset first.",true);return;}
        BuilderDocument d;QString error;
        bool ok=item->data(Qt::UserRole+1).toBool()?BuilderStorage::readGraph(item->data(Qt::UserRole).toString(),d,error):
            storage_.readPreset(item->data(Qt::UserRole).toString(),d,error);
        if(!ok){message(error,true);return;}
        if(replaceDocument(d))message("Preset loaded into draft. Live filter unchanged.");
    };
    connect(load,&QPushButton::clicked,this,loadSelected);connect(presets_,&QListWidget::itemDoubleClicked,this,[loadSelected]{loadSelected();});
    return panel;
}
void EffectBuilder::refreshPresets(){
    presets_->clear();
    if(!bundledPath_.isEmpty())for(auto& f:QDir(bundledPath_).entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Name)){
        BuilderDocument d;QString error;if(!BuilderStorage::readGraph(f.absoluteFilePath(),d,error))continue;
        auto words=f.completeBaseName().split('-');
        for(auto& word:words)if(!word.isEmpty())word[0]=word[0].toUpper();
        auto* item=new QListWidgetItem(words.join(" ")+" · Bundled",presets_);
        auto controls=d.graph.value("controls").toArray();
        QStringList names;for(auto c:controls)names<<c.toObject().value("name").toString();
        item->setToolTip(QString("%1 effects · %2 modulation routes\n%3").arg(d.graph.value("nodes").toArray().size()).arg(d.graph.value("routes").toArray().size()).arg(names.join(" · ")));
        item->setData(Qt::UserRole,f.absoluteFilePath());item->setData(Qt::UserRole+1,true);
    }
    for(auto& p:storage_.presets()){
        auto* item=new QListWidgetItem(p.name+" · Local",presets_);item->setData(Qt::UserRole,p.id);item->setData(Qt::UserRole+1,false);
    }
}
void EffectBuilder::message(const QString& s,bool error){
    statusError_=error;status_->setText(error?"Error: "+s:s);refreshTheme();
}
void EffectBuilder::changeEvent(QEvent* event){
    QWidget::changeEvent(event);
    if((event->type()==QEvent::PaletteChange||event->type()==QEvent::ApplicationPaletteChange||event->type()==QEvent::StyleChange)&&status_)
        refreshTheme();
}
void EffectBuilder::refreshTheme(){
    if(!status_)return;
    const GraphColors c(palette());
    graph_->setBackgroundBrush(c.base);
    for(auto* item:scene_->items()){
        auto role=item->data(1).toString();
        if(role=="node"){
            auto* node=static_cast<QGraphicsPathItem*>(item);
            auto kind=item->data(3).toString();
            QColor fill=kind=="terminal"?c.terminal:kind=="ladspa"?c.external:c.native;
            node->setBrush(fill);node->setPen(QPen(c.outline));
            for(auto* child:node->childItems())if(child->data(1).toString()=="label")
                static_cast<QGraphicsSimpleTextItem*>(child)->setBrush(readable(c.text,fill,c.text,4.5));
        }else if(role=="in"||role=="out"){
            auto* port=static_cast<QGraphicsEllipseItem*>(item);port->setPen(QPen(c.text,1.2));port->setBrush(c.accent);
        }else if(role=="cable"){
            auto* wire=static_cast<QGraphicsPathItem*>(item);
            bool inverted=item->data(3).toBool();wire->setPen(QPen(inverted?c.edge:c.accent,2,inverted?Qt::DashLine:Qt::SolidLine));
            wire->setData(4,c.text);
        }else if(role=="preview"){
            static_cast<QGraphicsPathItem*>(item)->setPen(QPen(c.accent,2,Qt::DashLine));
        }else if(role=="weight"){
            static_cast<QGraphicsSimpleTextItem*>(item)->setBrush(c.text);
        }
    }
    // Textual error status remains distinguishable even in monochrome themes.
    auto font=status_->font();font.setBold(statusError_);status_->setFont(font);
    graph_->viewport()->update();
}
void EffectBuilder::refreshTargets(){
    auto old=target_->currentData();std::vector<Target> targets;obs_enum_sources(collect_source,&targets);
    target_->clear();for(auto& t:targets)target_->addItem(t.label,QStringList{t.parent,t.filter});
    int index=target_->findData(old);if(index>=0)target_->setCurrentIndex(index);else if(requireExplicitTarget_||old.isValid()||targets.size()!=1)target_->setCurrentIndex(-1);
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
void EffectBuilder::setDraft(const QJsonObject& d){draft_=d;positions_.clear();{QSignalBlocker b(macros_);macros_->clear();}selected_=d.value("output").toInt(-1);redraw();inspect();fitCanvas();}
void EffectBuilder::fitCanvas(){
    graph_->resetTransform();
    const auto bounds=scene_->sceneRect();
    const double factor=std::clamp(std::min(graph_->viewport()->width()/bounds.width(),graph_->viewport()->height()/bounds.height()),.35,1.2);
    graph_->scale(factor,factor);graph_->centerOn(bounds.center());
}
void EffectBuilder::redraw(){
    static_cast<Canvas*>(graph_)->cancel();
    QSignalBlocker block(scene_),outBlock(output_);
    scene_->clear();output_->clear();
    auto nodes=draft_.value("nodes").toArray();auto out=draft_.value("output").toInt(-1);
    int count=std::min<qsizetype>(nodes.size(),16);
    if(nodes.isEmpty())output_->addItem("Direct input",-1);
    positions_.try_emplace(input_id,10,28);
    positions_.try_emplace(output_id,142+std::min(count,3)*188,28);
    for(int i=0;i<count;++i)positions_.try_emplace(i,142+(i%3)*188,28+(i/3)*110);
    auto card=[this](QString text,int id,int width,QString kind){
        const GraphColors c(palette());
        QPainterPath shape;shape.addRoundedRect(QRectF(0,0,width,52),8,8);
        auto* box=scene_->addPath(shape,QPen(c.outline),QBrush(kind=="terminal"?c.terminal:kind=="ladspa"?c.external:c.native));
        box->setData(0,id);box->setData(1,"node");box->setData(3,kind);box->setPos(positions_.at(id));box->setZValue(2);
        box->setToolTip(text+" · Drag to move");
        if(id>=0){box->setFlag(QGraphicsItem::ItemIsSelectable);box->setSelected(id==selected_);}
        auto* label=new QGraphicsSimpleTextItem(QFontMetrics(QApplication::font()).elidedText(text,Qt::ElideRight,width-20),box);
        label->setData(1,"label");label->setBrush(c.text);label->setPos(10,16);
        for(bool outgoing:{false,true}){
            if((id==input_id&&!outgoing)||(id==output_id&&outgoing))continue;
            auto* port=new QGraphicsEllipseItem((outgoing?width:0)-5,21,10,10,box);
            port->setData(0,id);port->setData(1,outgoing?"out":"in");
            port->setZValue(3);port->setPen(QPen(c.text,1.2));port->setBrush(c.accent);
            port->setToolTip(outgoing?"Drag to an input port":"Connect an output here");
            port->setAcceptHoverEvents(true);
        }
    };
    card("INPUT",input_id,100,"terminal");
    for(int i=0;i<count;++i){
        auto e=metadata(nodes[i].toObject().value("effect").toString());
        QString title=QString::number(i+1)+" · "+effectName(e);
        output_->addItem(title,i);card(title,i,156,e.value("format").toString()=="ladspa"?"ladspa":"native");
    }
    card("OUTPUT",output_id,100,"terminal");
    output_->setCurrentIndex(output_->findData(out));
    redrawWires();refreshTheme();refreshMacros();scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-30,-30,30,30));
    fitCanvas();
}
void EffectBuilder::redrawWires(){
    QSignalBlocker block(scene_);
    for(auto* i:scene_->items())if(i->data(1).toString()=="cable"||i->data(1).toString()=="weight")delete i;
    auto nodes=draft_.value("nodes").toArray();int count=std::min<qsizetype>(nodes.size(),16);
    auto draw=[this](int from,int to,int row,double weight){
        auto a=positions_.at(from)+QPointF(from==input_id?100:156,26),b=positions_.at(to)+QPointF(0,26);
        double lane=std::max(a.y(),b.y())+48;
        for(auto& [id,pos]:positions_){
            if(id!=from&&id!=to&&pos.x()<std::max(a.x(),b.x())&&pos.x()+156>std::min(a.x(),b.x()))
                lane=std::max(lane,pos.y()+74);
        }
        auto path=cablePath(a,b,lane);const GraphColors c(palette());const QColor color=weight<0?c.edge:c.accent;
        auto* item=new CableItem(path);item->setData(0,to);item->setData(1,"cable");item->setData(2,row);
        item->setData(3,weight<0);item->setData(4,c.text);item->setPen(QPen(color,2,weight<0?Qt::DashLine:Qt::SolidLine));item->setZValue(0);item->setFlag(QGraphicsItem::ItemIsSelectable);
        item->setToolTip(to>=0?"Select and Delete, or right-click to disconnect":"Drag another output to the Output port to reroute");
        scene_->addItem(item);
        if(std::abs(weight-1)>1e-6){
            auto* text=scene_->addSimpleText(QString::number(weight,'g',3)+"×");text->setData(1,"weight");text->setBrush(c.text);
            auto midpoint=path.pointAtPercent(.5);text->setPos(midpoint.x()-text->boundingRect().width()/2,midpoint.y()+2);
            text->setAcceptedMouseButtons(Qt::NoButton);
        }
    };
    for(int i=0;i<count;++i){
        auto inputs=nodes[i].toObject().value("inputs").toArray();
        for(int row=0;row<std::min<qsizetype>(inputs.size(),8);++row){
            auto e=inputs[row].toObject();int from=e.value("source").toInt(-3);
            if(from==input_id||(from>=0&&from<count))draw(from,i,row,e.value("weight").toDouble());
        }
    }
    int out=draft_.value("output").toInt(-3);
    if(out==input_id||(out>=0&&out<count))draw(out,output_id,-1,1);
}
void EffectBuilder::updateNodePosition(int id,QPointF pos){positions_[id]=pos;redrawWires();}
bool EffectBuilder::acceptsEdge(int from,int to,int ignoreRow)const{
    auto nodes=draft_.value("nodes").toArray();int count=nodes.size();
    if(count>16||to<0||to>=count||from<-1||from>=count||from==to)return false;
    auto inputs=nodes[to].toObject().value("inputs").toArray();
    for(int i=0;i<inputs.size();++i)if(i!=ignoreRow&&inputs[i].toObject().value("source").toInt(-3)==from)return false;
    std::set<int> visited;
    std::function<bool(int)> reaches=[&](int index){
        if(index==to)return true;
        if(index<0||index>=count||!visited.insert(index).second)return false;
        for(auto e:nodes[index].toObject().value("inputs").toArray())
            if(reaches(e.toObject().value("source").toInt(-3)))return true;
        return false;
    };
    return !reaches(from);
}
void EffectBuilder::connectPorts(int from,int to){
    auto nodes=draft_.value("nodes").toArray();
    if(to==output_id){
        if((from<0&&!nodes.isEmpty())||from<-1||from>=nodes.size()){message("Select an effect as output.",true);return;}
        draft_["output"]=from;redraw();message("Output rerouted in draft.");return;
    }
    if(!acceptsEdge(from,to)){message("Connection rejected: duplicate, cycle or invalid port.",true);return;}
    auto node=nodes[to].toObject();auto inputs=node.value("inputs").toArray();
    int total=0;for(auto n:nodes)total+=n.toObject().value("inputs").toArray().size();
    if(inputs.size()>=8||total>=32){message("Connection limit reached.",true);return;}
    inputs.append(QJsonObject{{"source",from},{"weight",1}});node["inputs"]=inputs;nodes[to]=node;draft_["nodes"]=nodes;
    selected_=to;redraw();inspect();message("Cable connected in draft. Live filter unchanged.");
}
void EffectBuilder::disconnectCable(int to,int row){
    auto nodes=draft_.value("nodes").toArray();
    if(to<0||to>=nodes.size()){message("Reroute the output by dragging a new cable to it.",true);return;}
    auto node=nodes[to].toObject();auto inputs=node.value("inputs").toArray();
    if(row<0||row>=inputs.size())return;
    if(inputs.size()<=1){message("Connect another input before removing the last cable.",true);return;}
    inputs.removeAt(row);node["inputs"]=inputs;nodes[to]=node;draft_["nodes"]=nodes;
    selected_=to;redraw();inspect();message("Cable disconnected in draft.");
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
        auto e=inputs[row].toObject();auto* from=new QComboBox;if(e.value("source").toInt(-3)==-1||acceptsEdge(-1,selected_,row))from->addItem("Input",-1);
        for(int i=0;i<std::min<qsizetype>(nodes.size(),16);++i)if(i==e.value("source").toInt(-3)||acceptsEdge(i,selected_,row))from->addItem(QString::number(i+1)+" · "+effectName(metadata(nodes[i].toObject().value("effect").toString())),i);
        from->setCurrentIndex(from->findData(e.value("source").toInt(-2)));
        auto* weight=new QDoubleSpinBox;weight->setRange(-4,4);weight->setSingleStep(.1);weight->setValue(e.value("weight").toDouble());
        auto change=[this,row,from,weight]{
            auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size()||from->currentIndex()<0)return;
            auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();if(row>=inputs.size())return;
            const int previous=inputs[row].toObject().value("source").toInt(-3),next=from->currentData().toInt();
            if(next!=previous&&!acceptsEdge(next,selected_,row)){
                QSignalBlocker block(from);from->setCurrentIndex(from->findData(previous));
                message("Connection rejected: duplicate or cycle.",true);return;
            }
            inputs[row]=QJsonObject{{"source",next},{"weight",weight->value()}};
            n["inputs"]=inputs;replaceNode(n);redraw();
        };
        connect(from,&QComboBox::currentIndexChanged,this,change);connect(weight,&QDoubleSpinBox::valueChanged,this,change);
        edges_->setCellWidget(row,0,from);edges_->setCellWidget(row,1,weight);
    }
}
QJsonObject EffectBuilder::bindingMetadata(int node,const QString& key)const{
    auto nodes=draft_.value("nodes").toArray();
    if(node<0||node>=nodes.size())return {};
    for(auto value:metadata(nodes[node].toObject().value("effect").toString()).value("parameters").toArray()){
        auto p=value.toObject();
        if(p.value("key").toString()==key&&p.value("type")=="number"&&p.value("bindable").toBool(false))return p;
    }
    return {};
}
void EffectBuilder::refreshMacros(){
    if(!macros_)return;
    auto old=macros_->currentData().toString();
    auto controls=draft_.value("controls").toArray();
    {QSignalBlocker block(macros_);macros_->clear();
        for(int i=0;i<std::min<qsizetype>(controls.size(),8);++i){
            auto c=controls[i].toObject();macros_->addItem(c.value("name").toString(),c.value("id").toString());
        }
        int index=macros_->findData(old);if(index>=0)macros_->setCurrentIndex(index);
    }
    inspectMacro();refreshRoutes();
}
void EffectBuilder::changeMacro(const QString& key,const QJsonValue& value){
    auto controls=draft_.value("controls").toArray();int index=macros_->currentIndex();
    if(index<0||index>=controls.size())return;
    auto c=controls[index].toObject();c[key]=value;controls[index]=c;draft_["controls"]=controls;
}
void EffectBuilder::inspectMacro(){
    const QSignalBlocker blockName(macroName_),blockDefault(macroDefault_),blockTargets(macroTarget_);
    auto controls=draft_.value("controls").toArray();int index=macros_->currentIndex();
    bool active=index>=0&&index<controls.size();
    macroName_->setEnabled(active);macroDefault_->setEnabled(active);removeMacro_->setEnabled(active);
    addMacro_->setEnabled(controls.size()<8);
    macroBindings_->clearContents();macroBindings_->setRowCount(0);macroTarget_->clear();
    addBinding_->setEnabled(false);removeBinding_->setEnabled(false);
    macroName_->clear();macroDefault_->setValue(0);
    if(!active)return;
    auto c=controls[index].toObject();macroName_->setText(c.value("name").toString());macroDefault_->setValue(c.value("default").toDouble());
    auto bindings=c.value("bindings").toArray();auto nodes=draft_.value("nodes").toArray();
    macroBindings_->setRowCount(std::min<qsizetype>(bindings.size(),16));
    for(int row=0;row<macroBindings_->rowCount();++row){
        auto b=bindings[row].toObject();int node=b.value("node").toInt(-1);auto key=b.value("parameter").toString();
        auto p=bindingMetadata(node,key);
        auto* label=new QTableWidgetItem(QString::number(node+1)+" · "+p.value("name").toString(key));
        label->setFlags(label->flags()&~Qt::ItemIsEditable);macroBindings_->setItem(row,0,label);
        for(int col=1;col<3;++col){
            auto field=col==1?QString("low"):QString("high");auto* spin=new QDoubleSpinBox;
            const auto step=p.value("step").toDouble(.01);
            spin->setDecimals(std::clamp(int(std::ceil(-std::log10(std::max(.000001,step)))),2,6));
            spin->setRange(p.value("min").toDouble(-1e9),p.value("max").toDouble(1e9));
            spin->setSingleStep(std::max(.000001,p.value("step").toDouble(.01)));spin->setSuffix(p.value("unit").toString());
            spin->setValue(b.value(field).toDouble());spin->setEnabled(!p.isEmpty());
            connect(spin,&QDoubleSpinBox::valueChanged,this,[this,row,field](double v){
                auto controls=draft_.value("controls").toArray();int i=macros_->currentIndex();
                if(i<0||i>=controls.size())return;auto bindings=controls[i].toObject().value("bindings").toArray();
                if(row>=bindings.size())return;auto b=bindings[row].toObject();b[field]=v;bindings[row]=b;
                changeMacro("bindings",bindings);
            });
            macroBindings_->setCellWidget(row,col,spin);
        }
    }
    std::set<std::pair<int,QString>> used;int total=0;
    for(auto control:controls)for(auto binding:control.toObject().value("bindings").toArray()){
        auto b=binding.toObject();used.emplace(b.value("node").toInt(-1),b.value("parameter").toString());++total;
    }
    for(int node=0;node<std::min<qsizetype>(nodes.size(),16);++node){
        auto meta=metadata(nodes[node].toObject().value("effect").toString());
        for(auto value:meta.value("parameters").toArray()){
            auto p=value.toObject();auto key=p.value("key").toString();
            if(p.value("type")!="number"||!p.value("bindable").toBool()||used.contains({node,key}))continue;
            macroTarget_->addItem(QString::number(node+1)+" · "+effectName(meta)+" / "+p.value("name").toString(),QStringList{QString::number(node),key});
        }
    }
    addBinding_->setEnabled(macroTarget_->count()>0&&bindings.size()<16&&total<32);
    removeBinding_->setEnabled(!bindings.isEmpty());
}
void EffectBuilder::addMacro(){
    auto controls=draft_.value("controls").toArray();if(controls.size()>=8)return;
    // New IDs are never recycled into unrelated saved scene control values.
    auto id=QString("c_")+QUuid::createUuid().toString(QUuid::Id128).left(30);
    controls.append(QJsonObject{{"id",id},{"name",QString("Control %1").arg(controls.size()+1)},{"default",.5},{"bindings",QJsonArray{}}});
    draft_["controls"]=controls;refreshMacros();macros_->setCurrentIndex(macros_->findData(id));
    message("Control added to draft. Bind at least one parameter before Apply.");
}
void EffectBuilder::removeMacro(){
    auto controls=draft_.value("controls").toArray();int i=macros_->currentIndex();if(i<0||i>=controls.size())return;
    auto id=controls[i].toObject().value("id");
    for(auto route:draft_.value("routes").toArray())if(route.toObject().value("control")==id){
        message("This control has a modulation route. Remove the route in Modulation first.",true);return;
    }
    controls.removeAt(i);draft_["controls"]=controls;refreshMacros();message("Control removed from draft.");
}
void EffectBuilder::addBinding(){
    if(!addBinding_->isEnabled())return;
    auto target=macroTarget_->currentData().toStringList();if(target.size()!=2)return;
    int node=target[0].toInt();auto key=target[1];auto p=bindingMetadata(node,key);if(p.isEmpty())return;
    auto controls=draft_.value("controls").toArray();int i=macros_->currentIndex();if(i<0||i>=controls.size())return;
    auto bindings=controls[i].toObject().value("bindings").toArray();
    bindings.append(QJsonObject{{"node",node},{"parameter",key},{"low",p.value("min")},{"high",p.value("max")}});
    changeMacro("bindings",bindings);inspectMacro();message("Binding added. Edit its endpoints to set the control response.");
}
void EffectBuilder::removeBinding(){
    auto controls=draft_.value("controls").toArray();int i=macros_->currentIndex();if(i<0||i>=controls.size())return;
    auto bindings=controls[i].toObject().value("bindings").toArray();int row=macroBindings_->currentRow();
    if(row<0||row>=bindings.size())return;bindings.removeAt(row);changeMacro("bindings",bindings);inspectMacro();
}
namespace {
constexpr const char* routeSources[]={"","LFO","Input envelope","Bass energy","Mid energy","Treble energy",
    "Sound category confidence","Sight motion energy","Sight hand X","Sight hand Y","Sight pinch closeness"};
QString routeSourceName(int id){return id>=1&&id<=10?QString::fromUtf8(routeSources[id]):QString("Unknown source (%1)").arg(id);}
int channelDomain(int source){return source==6?6:source==7?7:source>=8&&source<=10?8:0;}
}
QWidget* EffectBuilder::makeRoutePanel(){
    auto* panel=new QWidget;auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(3,3,3,3);layout->setSpacing(4);
    auto* row=new QHBoxLayout;routeList_=new QComboBox;routeList_->setObjectName("routeList");
    routeList_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);routeList_->setMinimumContentsLength(12);
    addRoute_=button("+ Route","addRoute",panel);removeRoute_=button("Remove","removeRoute",panel);
    row->addWidget(routeList_,1);row->addWidget(addRoute_);row->addWidget(removeRoute_);layout->addLayout(row);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto* content=new QWidget;routeForm_=new QVBoxLayout(content);routeForm_->setContentsMargins(4,3,4,3);routeForm_->setSpacing(4);
    auto field=[&](const char* label,QWidget* widget){
        auto* line=new QWidget;auto* row=new QHBoxLayout(line);row->setContentsMargins(0,0,0,0);
        auto* text=new QLabel(label);text->setMinimumWidth(100);text->setBuddy(widget);
        row->addWidget(text);row->addWidget(widget,1);routeForm_->addWidget(line);routeRows_[widget]=line;
    };
    auto combo=[&](const char* id,const char* label){
        auto* c=new QComboBox;c->setObjectName(id);c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);c->setMinimumContentsLength(12);
        field(label,c);return c;
    };
    auto spin=[&](const char* id,const char* label,double lo,double hi,double step,int decimals,const char* suffix=""){
        auto* s=new QDoubleSpinBox;s->setObjectName(id);s->setDecimals(decimals);s->setRange(lo,hi);s->setSingleStep(step);s->setSuffix(suffix);
        field(label,s);return s;
    };
    routeSource_=combo("routeSource","Source");
    for(int i=1;i<=10;++i)routeSource_->addItem(routeSourceName(i),i);
    routeTarget_=combo("routeTarget","Control");
    routeAmount_=spin("routeAmount","Amount",-1,1,.05,3);
    routeAmount_->setToolTip("Signed contribution to the normalized macro. Added to its manual base, then clamped to 0–1.");
    routeShape_=combo("routeShape","Waveform");
    for(const auto* shape:{"Sine","Triangle","Square","Saw"})routeShape_->addItem(shape,routeShape_->count());
    routeRate_=spin("routeRate","Rate",.05,20,.05,2," Hz");
    routeAttack_=spin("routeAttack","Attack",1,200,1,1," ms");
    routeRelease_=spin("routeRelease","Release",10,2000,10,1," ms");
    routeSensitivity_=spin("routeSensitivity","Band sensitivity",-24,24,.1,1," dB");
    routeChannel_=combo("routeChannel","Input channel");
    refreshChannels_=button("Refresh inputs","refreshRouteChannels",panel);routeForm_->addWidget(refreshChannels_);routeRows_[refreshChannels_]=refreshChannels_;
    routeCategory_=new QLineEdit;routeCategory_->setObjectName("routeCategory");routeCategory_->setPlaceholderText("Exact label, e.g. Speech");routeCategory_->setMaxLength(256);
    field("Sound category",routeCategory_);routeForm_->addStretch(1);
    scroll->setWidget(content);layout->addWidget(scroll,1);
    auto* hint=new QLabel("Routes add to exposed controls. Missing external signals fall back to the manual value. Apply to activate.");
    hint->setWordWrap(true);layout->addWidget(hint);
    connect(routeList_,&QComboBox::currentIndexChanged,this,[this]{inspectRoute();});
    connect(addRoute_,&QPushButton::clicked,this,[this]{addRoute();});
    connect(removeRoute_,&QPushButton::clicked,this,[this]{removeRoute();});
    connect(refreshChannels_,&QPushButton::clicked,this,[this]{refreshRouteChannels();});
    connect(routeSource_,&QComboBox::currentIndexChanged,this,[this]{
        int i=routeList_->currentIndex();auto routes=draft_.value("routes").toArray();
        if(i<0||i>=routes.size()||routeSource_->currentIndex()<0)return;
        auto r=routes[i].toObject();int old=r.value("source").toInt(),next=routeSource_->currentData().toInt();
        if(channelDomain(old)!=channelDomain(next)){r.remove("channel");r.remove("category");}
        r["source"]=next;routes[i]=r;draft_["routes"]=routes;refreshRoutes();
    });
    connect(routeTarget_,&QComboBox::currentIndexChanged,this,[this]{
        if(routeTarget_->currentIndex()>=0){changeRoute("control",routeTarget_->currentData().toString());refreshRoutes();}
    });
    connect(routeShape_,&QComboBox::currentIndexChanged,this,[this]{if(routeShape_->currentIndex()>=0)changeRoute("shape",routeShape_->currentData().toInt());});
    connect(routeChannel_,&QComboBox::currentIndexChanged,this,[this]{if(routeChannel_->currentIndex()>=0)changeRoute("channel",routeChannel_->currentData().toString());});
    connect(routeCategory_,&QLineEdit::textChanged,this,[this](const QString& value){changeRoute("category",value);});
    for(auto [widget,key]:std::initializer_list<std::pair<QDoubleSpinBox*,const char*>>{
        {routeAmount_,"amount"},{routeRate_,"rate"},{routeAttack_,"attack"},{routeRelease_,"release"},{routeSensitivity_,"sensitivity"}})
        connect(widget,&QDoubleSpinBox::valueChanged,this,[this,key](double v){changeRoute(key,v);});
    return panel;
}
void EffectBuilder::refreshRoutes(){
    if(!routeList_)return;
    auto routes=draft_.value("routes").toArray(),controls=draft_.value("controls").toArray();
    int old=routeList_->currentIndex();
    {QSignalBlocker block(routeList_);routeList_->clear();
        for(int i=0;i<routes.size();++i){
            auto r=routes[i].toObject();auto id=r.value("control");QString name="Missing control";
            for(auto c:controls)if(c.toObject().value("id")==id){name=c.toObject().value("name").toString();break;}
            routeList_->addItem(QString::number(i+1)+" · "+routeSourceName(r.value("source").toInt())+" → "+name);
        }
        if(!routes.isEmpty())routeList_->setCurrentIndex(std::clamp(old,0,int(routes.size())-1));
    }
    addRoute_->setEnabled(routes.size()<modulation_slots&&!controls.isEmpty());
    addRoute_->setToolTip(controls.isEmpty()?"Create an exposed control first.":routes.size()>=modulation_slots?"All four modulation slots are in use.":"Add a modulation route to an exposed control.");
    inspectRoute();
}
void EffectBuilder::inspectRoute(){
    int i=routeList_->currentIndex();auto routes=draft_.value("routes").toArray();bool active=i>=0&&i<routes.size();
    removeRoute_->setEnabled(active);
    const QSignalBlocker a(routeSource_),b(routeTarget_),c(routeAmount_),d(routeShape_),e(routeRate_),f(routeAttack_),g(routeRelease_),h(routeSensitivity_),j(routeCategory_);
    auto r=active?routes[i].toObject():QJsonObject{};
    int source=r.value("source").toInt();
    routeSource_->setCurrentIndex(routeSource_->findData(source));routeTarget_->clear();
    auto id=r.value("control").toString();
    for(auto v:draft_.value("controls").toArray()){auto c=v.toObject();routeTarget_->addItem(c.value("name").toString(),c.value("id").toString());}
    if(!id.isEmpty()&&routeTarget_->findData(id)<0)routeTarget_->addItem("Missing saved control",id);
    routeTarget_->setCurrentIndex(routeTarget_->findData(id));
    routeAmount_->setValue(r.value("amount").toDouble());routeShape_->setCurrentIndex(routeShape_->findData(r.value("shape").toInt()));
    routeRate_->setValue(r.value("rate").toDouble(1));routeAttack_->setValue(r.value("attack").toDouble(10));
    routeRelease_->setValue(r.value("release").toDouble(100));routeSensitivity_->setValue(r.value("sensitivity").toDouble());
    routeCategory_->setText(r.value("category").toString());
    for(QWidget* w:std::initializer_list<QWidget*>{routeSource_,routeTarget_,routeAmount_})w->setEnabled(active);
    auto show=[this](QWidget* field,bool visible){
        routeRows_.at(field)->setVisible(visible);
    };
    show(routeShape_,active&&source==1);show(routeRate_,active&&source==1);
    show(routeAttack_,active&&source>=2&&source<=5);show(routeRelease_,active&&source>=2&&source<=5);
    show(routeSensitivity_,active&&source>=3&&source<=5);
    show(routeChannel_,active&&channelDomain(source)!=0);show(refreshChannels_,active&&channelDomain(source)!=0);
    show(routeCategory_,active&&source==6);
    routeForm_->invalidate();routeForm_->activate();
    if(routeList_->isVisible())refreshRouteChannels();
}
void EffectBuilder::refreshRouteChannels(){
    const QSignalBlocker block(routeChannel_);routeChannel_->clear();
    auto routes=draft_.value("routes").toArray();int i=routeList_->currentIndex();if(i<0||i>=routes.size())return;
    auto r=routes[i].toObject();int source=r.value("source").toInt();
    if(!channelDomain(source))return;
    auto saved=r.value("channel").toString();auto bytes=saved.toUtf8();
    auto* props=obs_properties_create();auto* prop=obs_properties_add_list(props,"channel","Channel",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);
    if(source==6)sound_channel_choices(prop,bytes.constData());else sight_channel_choices(prop,bytes.constData(),source);
    for(size_t n=0;n<obs_property_list_item_count(prop);++n){
        routeChannel_->addItem(QString::fromUtf8(obs_property_list_item_name(prop,n)),QString::fromUtf8(obs_property_list_item_string(prop,n)));
        if(obs_property_list_item_disabled(prop,n))if(auto* m=qobject_cast<QStandardItemModel*>(routeChannel_->model()))m->item(int(n))->setEnabled(false);
    }
    obs_properties_destroy(props);routeChannel_->setCurrentIndex(routeChannel_->findData(saved));
}
void EffectBuilder::changeRoute(const QString& key,const QJsonValue& value){
    int i=routeList_->currentIndex();auto routes=draft_.value("routes").toArray();if(i<0||i>=routes.size())return;
    auto r=routes[i].toObject();r[key]=value;routes[i]=r;draft_["routes"]=routes;
}
void EffectBuilder::addRoute(){
    auto routes=draft_.value("routes").toArray(),controls=draft_.value("controls").toArray();
    if(routes.size()>=modulation_slots||controls.isEmpty())return;
    auto id=macros_->currentData().toString();if(id.isEmpty())id=controls[0].toObject().value("id").toString();
    routes.append(QJsonObject{{"control",id},{"source",1},{"amount",.25},{"rate",1},{"shape",0}});
    draft_["routes"]=routes;refreshRoutes();routeList_->setCurrentIndex(routes.size()-1);message("Modulation route added to draft.");
}
void EffectBuilder::removeRoute(){
    int i=routeList_->currentIndex();auto routes=draft_.value("routes").toArray();if(i<0||i>=routes.size())return;
    routes.removeAt(i);draft_["routes"]=routes;refreshRoutes();message("Modulation route removed from draft.");
}
void EffectBuilder::addNode(){
    auto* item=library_->currentItem();if(!item||!(item->flags()&Qt::ItemIsEnabled))return;
    auto nodes=draft_.value("nodes").toArray();if(nodes.size()>=16){message("16-node limit.",true);return;}
    int previous=draft_.value("output").toInt(-1);
    nodes.append(QJsonObject{{"effect",item->data(Qt::UserRole).toString()},{"params",QJsonObject{}},{"inputs",QJsonArray{QJsonObject{{"source",previous},{"weight",1}}}}});
    selected_=nodes.size()-1;
    const QPointF defaultNew(142+(selected_%3)*188,28+(selected_/3)*110);
    if(positions_.contains(output_id)&&positions_[output_id]==defaultNew)
        positions_[output_id]=QPointF(142+std::min<int>(nodes.size(),3)*188,28);
    draft_["nodes"]=nodes;draft_["output"]=selected_;redraw();inspect();message("Draft updated. Live filter unchanged.");
}
void EffectBuilder::addEdge(){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();if(inputs.size()>=8){message("Eight inputs per node.",true);return;}
    // Choose the first valid source instead of inserting a duplicate.
    for(int from=-1;from<nodes.size();++from)if(acceptsEdge(from,selected_)){
        int total=0;for(auto node:nodes)total+=node.toObject().value("inputs").toArray().size();
        if(total>=32){message("Connection limit reached.",true);return;}
        inputs.append(QJsonObject{{"source",from},{"weight",.5}});n["inputs"]=inputs;replaceNode(n);redraw();inspect();return;
    }
    message("No additional acyclic input available.",true);
}
void EffectBuilder::removeEdge(){
    auto nodes=draft_.value("nodes").toArray();if(selected_<0||selected_>=nodes.size())return;
    auto n=nodes[selected_].toObject();auto inputs=n.value("inputs").toArray();int row=edges_->currentRow();
    if(row>=0)disconnectCable(selected_,row);
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
    positions_.erase(selected_);selected_=-1;redraw();inspect();
}
void EffectBuilder::loadTarget(){
    auto* target=find_target(target_);if(!target){message("Select an existing HyperDSP Graph filter.",true);return;}
    auto* settings=obs_source_get_settings(target);auto doc=QJsonDocument::fromJson(obs_data_get_string(settings,"graph_json"));obs_data_release(settings);obs_source_release(target);
    if(!doc.isObject()){message("Target contains invalid JSON. Restore it from filter properties first.",true);return;}
    if(replaceDocument({doc.object(),{}, {}}))message("Loaded target draft. Live filter unchanged.");
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
    }else if(event==OBS_FRONTEND_EVENT_THEME_CHANGED&&dock){
        QTimer::singleShot(0,dock.data(),[]{if(dock)dock->refreshTheme();});
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
