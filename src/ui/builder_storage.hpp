#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QUuid>
#include <QPointF>
#include <QString>
#include <QRegularExpression>
#include <jansson.h>
#include <cmath>
#include <map>
#include <vector>
namespace hyperdsp {
struct BuilderDocument {
    QJsonObject graph;
    std::map<int,QPointF> positions;
    QString name;
};
inline QJsonObject builder_document_json(const BuilderDocument& d){
    QJsonArray layout;
    for(auto& [id,p]:d.positions)layout.append(QJsonObject{{"id",id},{"x",p.x()},{"y",p.y()}});
    return {{"version",1},{"graph",d.graph},{"positions",layout},{"name",d.name}};
}
class BuilderStorage {
    QString root_;
    static constexpr qsizetype limit=96*1024;
    static bool validId(const QString& id){return QRegularExpression("^[a-f0-9]{32}$").match(id).hasMatch();}
    static bool decode(const QJsonObject& obj,BuilderDocument& out,QString& error){
        auto fail=[&](const char* msg){error=QString::fromUtf8(msg);return false;};
        if(obj.value("version")!=1||!obj.value("graph").isObject()||!obj.value("positions").isArray()||!obj.value("name").isString())
            return fail("Unknown or malformed editor document");
        BuilderDocument d;d.graph=obj.value("graph").toObject();d.name=obj.value("name").toString();
        if(d.name.toUtf8().size()>160||QJsonDocument(d.graph).toJson(QJsonDocument::Compact).size()>65536)
            return fail("Editor document exceeds size limits");
        auto nodes=d.graph.value("nodes").toArray();auto positions=obj.value("positions").toArray();
        if(positions.size()>18)return fail("Too many layout positions");
        for(auto v:positions){
            auto p=v.toObject();auto id=p.value("id"),x=p.value("x"),y=p.value("y");
            if(!id.isDouble()||!x.isDouble()||!y.isDouble())return fail("Malformed layout position");
            double n=id.toDouble(),xx=x.toDouble(),yy=y.toDouble();
            if(!std::isfinite(n)||n!=std::floor(n)||n<-2||n>=16||n>=nodes.size()||
                !std::isfinite(xx)||!std::isfinite(yy)||std::abs(xx)>5000||std::abs(yy)>5000||
                !d.positions.emplace(int(n),QPointF(xx,yy)).second)return fail("Invalid layout position");
        }
        out=std::move(d);error.clear();return true;
    }
    bool read(const QString& path,BuilderDocument& d,QString& error)const{
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)||file.size()>limit){error="Cannot read editor document (96 KiB limit)";return false;}
        auto bytes=file.read(limit+1);if(bytes.size()>limit){error="Editor document too large";return false;}
        json_error_t strict{};auto* parsed=json_loadb(bytes.constData(),size_t(bytes.size()),JSON_REJECT_DUPLICATES,&strict);
        if(!parsed){error=QString::fromUtf8(strict.text);return false;}json_decref(parsed);
        auto doc=QJsonDocument::fromJson(bytes);
        return decode(doc.object(),d,error);
    }
    bool write(const QString& path,const BuilderDocument& d,QString& error)const{
        BuilderDocument checked;auto obj=builder_document_json(d);
        if(!decode(obj,checked,error))return false;
        auto bytes=QJsonDocument(obj).toJson(QJsonDocument::Compact);
        if(bytes.size()>limit||root_.isEmpty()||!QDir().mkpath(QFileInfo(path).absolutePath())){error="Cannot create editor storage";return false;}
        QSaveFile file(path);file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){error="Cannot atomically save editor document";return false;}
        error.clear();return true;
    }
    QString path(const QString& relative)const{return root_.isEmpty()?QString():QDir(root_).filePath(relative);}
public:
    explicit BuilderStorage(QString root={}):root_(std::move(root)){}
    bool hasRecovery()const{return !root_.isEmpty()&&QFileInfo::exists(path("recovery.json"));}
    bool archiveRecovery(QString& error)const{
        if(!hasRecovery()){error="Recovery file unavailable";return false;}
        auto backup=path("recovery-unreadable-"+QUuid::createUuid().toString(QUuid::Id128)+".json");
        if(!QFile::copy(path("recovery.json"),backup)){error="Cannot preserve unreadable recovery";return false;}
        return true;
    }
    bool hasPrevious()const{return !root_.isEmpty()&&QFileInfo::exists(path("previous.json"));}
    bool readRecovery(BuilderDocument& d,QString& e)const{return read(path("recovery.json"),d,e);}
    bool saveRecovery(const BuilderDocument& d,QString& e)const{return write(path("recovery.json"),d,e);}
    bool readPrevious(BuilderDocument& d,QString& e)const{return read(path("previous.json"),d,e);}
    bool savePrevious(const BuilderDocument& d,QString& e)const{return write(path("previous.json"),d,e);}
    QString savePreset(const QString& name,BuilderDocument d,QString& e)const{
        d.name=name.trimmed();if(d.name.isEmpty()){e="Enter a preset name";return {};}
        if(root_.isEmpty()){e="Editor storage unavailable";return {};}
        auto id=QUuid::createUuid().toString(QUuid::Id128);
        return write(path("presets/"+id+".json"),d,e)?id:QString();
    }
    bool readPreset(const QString& id,BuilderDocument& d,QString& e)const{
        if(!validId(id)){e="Invalid preset identity";return false;}
        return read(path("presets/"+id+".json"),d,e);
    }
    struct Preset {QString id,name;};
    std::vector<Preset> presets()const{
        std::vector<Preset> result;if(root_.isEmpty())return result;
        auto entries=QDir(path("presets")).entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Name);
        for(auto& f:entries){auto id=f.completeBaseName();BuilderDocument d;QString e;
            if(validId(id)&&readPreset(id,d,e))result.push_back({id,d.name});
        }
        return result;
    }
    static bool readGraph(const QString& path,BuilderDocument& d,QString& error){
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)||file.size()>65536){error="Cannot read graph (64 KiB limit)";return false;}
        auto bytes=file.read(65537);if(bytes.size()>65536){error="Graph too large";return false;}
        json_error_t strict{};auto* parsed=json_loadb(bytes.constData(),size_t(bytes.size()),JSON_REJECT_DUPLICATES,&strict);
        if(!parsed){error=QString::fromUtf8(strict.text);return false;}json_decref(parsed);
        auto doc=QJsonDocument::fromJson(bytes);
        if(!doc.isObject()){error="Expected a graph object";return false;}
        d={doc.object(),{}, {}};return true;
    }
};
}
