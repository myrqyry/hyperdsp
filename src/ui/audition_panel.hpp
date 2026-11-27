#pragma once
#include "framework/dsp/graph_audition.hpp"
#include <obs-module.h>
#include <QWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QScrollArea>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QSignalBlocker>
#include <future>
#include <chrono>
#include <functional>
namespace hyperdsp {
class AuditionPanel final:public QWidget {
    std::function<QByteArray()> draft_;
    QComboBox *signal_,*device_;
    QDoubleSpinBox *frequency_,*level_,*volume_;
    QPushButton *open_,*render_,*dry_,*wet_,*stop_,*refresh_;
    QLabel *fileLabel_,*status_;
    QProcess playback_,discovery_;
    QTimer timer_;
    std::future<GraphAudition> future_;
    GraphAudition result_;
    QByteArray captured_,pending_;
    QString path_,capturedPath_;
    int kind_=-1;
    double hz_=0,db_=0;
    unsigned rate_=0,channels_=0;
    bool ready_=false,stopping_=false;
    bool matches()const{
        if(draft_()!=captured_||signal_->currentIndex()!=kind_||frequency_->value()!=hz_||level_->value()!=db_||path_!=capturedPath_)return false;
        if(kind_==int(AuditionSignal::File))return true;
        obs_audio_info ai{};
        return obs_get_audio_info(&ai)&&ai.samples_per_sec==rate_&&get_audio_channels(ai.speakers)==channels_;
    }
    void buttons(){
        bool playing=playback_.state()!=QProcess::NotRunning;
        bool enabled=ready_&&!playing&&device_->currentIndex()>0&&!QStandardPaths::findExecutable("paplay").isEmpty();
        dry_->setEnabled(enabled);wet_->setEnabled(enabled);stop_->setEnabled(playing&&!stopping_);
        render_->setEnabled(!future_.valid()&&!playing);
        device_->setEnabled(!playing);refresh_->setEnabled(!playing&&discovery_.state()==QProcess::NotRunning);
        volume_->setEnabled(!playing);
    }
    void stopPlayback(){
        if(playback_.state()!=QProcess::NotRunning){
            stopping_=true;playback_.kill();status_->setText("Stopping audition…");
        }
        buttons();
    }
    void invalidate(){
        if(!ready_)return;
        stopPlayback();ready_=false;result_={};status_->setText("Draft or signal changed — render again.");buttons();
    }
    void poll(){
        if(future_.valid()&&future_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            try{result_=future_.get();}catch(const std::exception& e){result_={};result_.error=e.what();}
            ready_=result_.ok&&matches();
            if(!matches())status_->setText("Draft or signal changed — render again.");
            else if(!result_.ok)status_->setText("Error: "+QString::fromStdString(result_.error));
            else{
                auto frames=result_.dry.size()/result_.channels;
                QString text=QString("Ready · %1 s · %2 kHz · %3 ch").arg(double(frames)/result_.rate,0,'f',2).arg(result_.rate/1000.,0,'g',5).arg(result_.channels);
                if(result_.latency)text+=" · latency aligned";
                if(result_.external_routes)text+=" · external signals zero";
                if(result_.truncated)text+=" · first 8 s";
                status_->setText(text);
            }
            if(!ready_)result_={};
            buttons();
        }
        if(ready_&&!matches())invalidate();
    }
    void render(){
        if(future_.valid()||playback_.state()!=QProcess::NotRunning)return;
        captured_=draft_();capturedPath_=path_;kind_=signal_->currentIndex();hz_=frequency_->value();db_=level_->value();
        obs_audio_info ai{};
        if(kind_!=int(AuditionSignal::File)&&!obs_get_audio_info(&ai)){status_->setText("Error: OBS audio format unavailable");return;}
        rate_=ai.samples_per_sec;channels_=get_audio_channels(ai.speakers);
        ready_=false;result_={};status_->setText("Rendering isolated A/B…");
        try{
            future_=std::async(std::launch::async,[raw=captured_.toStdString(),path=capturedPath_,kind=kind_,hz=hz_,db=db_,rate=rate_,channels=channels_]{
                std::vector<uint8_t> bytes;
                if(kind==int(AuditionSignal::File)){
                    QFile file(path);QFileInfo info(path);
                    if(!info.isFile()||!file.open(QIODevice::ReadOnly)||file.size()>qint64(audition_file_limit)){
                        GraphAudition bad;bad.error="Select a local PCM16 WAV no larger than 32 MiB";return bad;
                    }
                    auto data=file.read(qint64(audition_file_limit)+1);
                    bytes.assign(data.begin(),data.end());
                }
                return audition_graph(raw,rate,channels,AuditionSignal(kind),hz,db,bytes);
            });
        }catch(const std::exception& e){status_->setText("Error: "+QString::fromUtf8(e.what()));}
        buttons();
    }
    void refreshDevices(){
#if defined(_WIN32) || defined(__APPLE__)
        // Isolated rendering is portable; device playback still needs a
        // native output backend. Do not select the system default silently.
        status_->setText("Native playback is not yet available on this platform. Isolated A/B rendering is available.");
        buttons();return;
#endif
        if(discovery_.state()!=QProcess::NotRunning||playback_.state()!=QProcess::NotRunning)return;
        auto program=QStandardPaths::findExecutable("pactl");
        if(program.isEmpty()){status_->setText("Playback unavailable: pactl is not installed.");return;}
        discovery_.start(program,{"list","short","sinks"});buttons();
    }
    void play(bool wet){
        if(!ready_||!matches()){invalidate();return;}
        if(device_->currentIndex()<=0||playback_.state()!=QProcess::NotRunning)return;
        auto program=QStandardPaths::findExecutable("paplay");
        if(program.isEmpty()){status_->setText("Playback unavailable: paplay is not installed.");return;}
        auto bytes=audition_pcm(result_,wet,volume_->value());
        if(bytes.empty()){status_->setText("Error: no audition PCM");return;}
        pending_=QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size()));
        stopping_=false;
        playback_.start(program,{"--raw","--format=s16le","--rate="+QString::number(result_.rate),
            "--channels="+QString::number(result_.channels),"--device="+device_->currentData().toString(),
            "--client-name=HyperDSP","--stream-name=HyperDSP isolated audition","--volume=65536"});
        status_->setText(wet?"Requested playback: B · processed":"Requested playback: A · dry");
        buttons();
    }
public:
    explicit AuditionPanel(std::function<QByteArray()> draft,QWidget* parent=nullptr):QWidget(parent),draft_(std::move(draft)){
        auto* root=new QVBoxLayout(this);root->setContentsMargins(4,4,4,4);root->setSpacing(4);
        auto* form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        signal_=new QComboBox;signal_->setObjectName("auditionSignal");signal_->addItems({"Sine","Pink noise","Log sweep","Local PCM16 WAV"});
        form->addRow("Signal",signal_);
        auto spin=[](const char* name,double min,double max,double value,const char* suffix){
            auto* w=new QDoubleSpinBox;w->setObjectName(name);w->setRange(min,max);w->setValue(value);w->setDecimals(1);w->setSuffix(suffix);return w;
        };
        frequency_=spin("auditionFrequency",20,20000,1000," Hz");
        level_=spin("auditionLevel",-60,0,-12," dBFS");
        auto* signalRow=new QWidget;auto* signalLayout=new QHBoxLayout(signalRow);signalLayout->setContentsMargins(0,0,0,0);
        signalLayout->addWidget(frequency_,1);signalLayout->addWidget(level_,1);form->addRow("Frequency / peak",signalRow);
        open_=new QPushButton("Choose WAV…");open_->setObjectName("chooseAuditionFile");fileLabel_=new QLabel("No file selected");fileLabel_->setTextFormat(Qt::PlainText);fileLabel_->setWordWrap(true);
        auto* fileRow=new QWidget;auto* fileLayout=new QHBoxLayout(fileRow);fileLayout->setContentsMargins(0,0,0,0);
        fileLayout->addWidget(open_);fileLayout->addWidget(fileLabel_,1);form->addRow("File",fileRow);
        device_=new QComboBox;device_->setObjectName("auditionDevice");device_->setMinimumContentsLength(12);device_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        device_->addItem("Select output device…");
        refresh_=new QPushButton("Refresh");auto* devices=new QWidget;auto* deviceLayout=new QHBoxLayout(devices);deviceLayout->setContentsMargins(0,0,0,0);deviceLayout->addWidget(device_,1);deviceLayout->addWidget(refresh_);
        form->addRow("Playback device",devices);
        volume_=spin("auditionVolume",-60,0,-18," dB");form->addRow("Listening gain",volume_);
        auto* settings=new QWidget;settings->setLayout(form);
        auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);scroll->setWidget(settings);
        root->addWidget(scroll,1);
        auto* transport=new QHBoxLayout;
        auto make=[&](const char* label,const char* name){auto* b=new QPushButton(label);b->setObjectName(name);transport->addWidget(b);return b;};
        render_=make("Render","renderAudition");dry_=make("A · Dry","playDry");wet_=make("B · Wet","playWet");stop_=make("Stop","stopAudition");root->addLayout(transport);
        status_=new QLabel("Render the draft, then select a playback device.");status_->setObjectName("auditionStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);root->addWidget(status_);
        auto* warning=new QLabel("Preview bypasses the OBS mixer. Desktop audio capture can still record the selected device. Use an uncaptured output. Both versions share a −12 dBFS digital peak ceiling; start quiet.");
        warning->setWordWrap(true);root->addWidget(warning);
        auto updateSignal=[=,this]{
            bool file=signal_->currentIndex()==int(AuditionSignal::File);
            fileRow->setVisible(file);if(auto* label=form->labelForField(fileRow))label->setVisible(file);
            signalRow->setVisible(!file);if(auto* label=form->labelForField(signalRow))label->setVisible(!file);
            frequency_->setEnabled(signal_->currentIndex()==0);invalidate();
        };
        connect(signal_,&QComboBox::currentIndexChanged,this,updateSignal);updateSignal();
        connect(frequency_,&QDoubleSpinBox::valueChanged,this,[this]{invalidate();});
        connect(level_,&QDoubleSpinBox::valueChanged,this,[this]{invalidate();});
        connect(open_,&QPushButton::clicked,this,[this]{
            auto path=QFileDialog::getOpenFileName(this,"Choose local PCM16 WAV",{},"WAV (*.wav)");
            if(!path.isEmpty()){path_=path;fileLabel_->setText(QFileInfo(path).fileName());invalidate();}
        });
        connect(refresh_,&QPushButton::clicked,this,[this]{refreshDevices();});
        connect(device_,&QComboBox::currentIndexChanged,this,[this]{buttons();});
        connect(render_,&QPushButton::clicked,this,[this]{render();});
        connect(dry_,&QPushButton::clicked,this,[this]{play(false);});
        connect(wet_,&QPushButton::clicked,this,[this]{play(true);});
        connect(stop_,&QPushButton::clicked,this,[this]{stopPlayback();});
        connect(&discovery_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus exit){
            auto old=device_->currentData();QSignalBlocker b(device_);device_->clear();device_->addItem("Select output device…");
            if(code==0&&exit==QProcess::NormalExit){
                for(auto line:discovery_.readAllStandardOutput().split('\n')){
                    auto parts=line.split('\t');
                    if(parts.size()>1&&!parts[1].isEmpty())device_->addItem(QString::fromUtf8(parts[1]),QString::fromUtf8(parts[1]));
                }
                auto i=device_->findData(old);device_->setCurrentIndex(i>0?i:0);
            }else status_->setText("Output device discovery failed.");
            buttons();
        });
        connect(&discovery_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){status_->setText("Output device discovery unavailable.");buttons();});
        connect(&playback_,&QProcess::started,this,[this]{
            if(stopping_){playback_.kill();return;}
            if(playback_.write(pending_)!=pending_.size()){stopPlayback();return;}
            playback_.closeWriteChannel();pending_.clear();
        });
        connect(&playback_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus exit){
            pending_.clear();bool stopped=stopping_;stopping_=false;
            if(ready_)status_->setText(stopped?"Stopped":code==0&&exit==QProcess::NormalExit?"Finished playback":"Error: output playback failed");
            buttons();
        });
        connect(&playback_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart){pending_.clear();stopping_=false;status_->setText("Error: playback process unavailable");buttons();}
        });
        timer_.setInterval(50);connect(&timer_,&QTimer::timeout,this,[this]{poll();});buttons();
    }
    ~AuditionPanel()override{
        timer_.stop();playback_.disconnect(this);discovery_.disconnect(this);
        if(playback_.state()!=QProcess::NotRunning){playback_.kill();playback_.waitForFinished(250);}
        if(discovery_.state()!=QProcess::NotRunning){discovery_.kill();discovery_.waitForFinished(250);}
    }
protected:
    void showEvent(QShowEvent* e)override{QWidget::showEvent(e);poll();refreshDevices();timer_.start();}
    void hideEvent(QHideEvent* e)override{timer_.stop();stopPlayback();QWidget::hideEvent(e);}
};
}
