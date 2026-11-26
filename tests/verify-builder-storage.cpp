#include "ui/builder_storage.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>
using namespace hyperdsp;
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp;
    BuilderStorage store(temp.path());QString error;
    BuilderDocument d;
    d.graph={{"schema",1},{"nodes",QJsonArray{QJsonObject{{"effect","gain"},{"inputs",QJsonArray{QJsonObject{{"source",-1},{"weight",1}}}}}}},{"output",0}};
    d.positions={{-1,{10,28}},{-2,{400,40}},{0,{183,112}}};
    bool ok=temp.isValid()&&store.saveRecovery(d,error);
    BuilderDocument restored;
    ok=ok&&store.readRecovery(restored,error)&&restored.graph==d.graph&&restored.positions==d.positions;
    d.graph["output"]=99; // invalid DSP draft must still be recoverable
    ok=ok&&store.saveRecovery(d,error)&&store.readRecovery(restored,error)&&restored.graph==d.graph;
    auto id=store.savePreset("Warm color",d,error);auto second=store.savePreset("Warm color",d,error);
    ok=ok&&!id.isEmpty()&&!second.isEmpty()&&id!=second&&store.presets().size()==2;
    ok=ok&&store.readPreset(id,restored,error)&&restored.name=="Warm color"&&restored.graph==d.graph&&restored.positions==d.positions;
    ok=ok&&!store.readPreset("../recovery",restored,error);
    auto bad=d;bad.positions[99]={0,0};ok=ok&&!store.saveRecovery(bad,error);
    ok=ok&&store.readRecovery(restored,error)&&restored.positions==d.positions;
    QFile corrupt(temp.filePath("recovery.json"));ok=ok&&corrupt.open(QIODevice::WriteOnly);corrupt.write("{\"version\":1,\"version\":1}");corrupt.close();
    ok=ok&&!store.readRecovery(restored,error);
    std::printf("BUILDER_STORAGE_PASS=%d\n",ok);return ok?0:1;
}
