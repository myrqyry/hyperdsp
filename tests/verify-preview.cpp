#include "framework/dsp/graph_preview.hpp"
#include <cstdio>
using namespace hyperdsp;
int main(){
    bool ok=true;
    auto gain=R"({"schema":1,"nodes":[{"effect":"gain","params":{"gain_db":-6},"inputs":[{"source":-1,"weight":1}]}],"output":0})";
    auto low=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_native_filter:1","params":{"frequency":1000},"inputs":[{"source":-1,"weight":1}]}],"output":0})";
    auto clip=R"({"schema":1,"nodes":[{"effect":"native:hyperdsp_color_filter:3","params":{"drive":24},"inputs":[{"source":-1,"weight":1}]}],"output":0})";
    auto reactive=R"({"schema":1,"nodes":[{"effect":"gain","inputs":[{"source":-1,"weight":1}]}],"output":0,"controls":[{"id":"level","name":"Level","default":0.5,"bindings":[{"node":0,"parameter":"gain_db","low":-12,"high":0}]}],"routes":[{"control":"level","source":1,"amount":0.25}]})";
    auto external=R"({"schema":1,"nodes":[{"effect":"ladspa:/nonexistent.so\tbad","inputs":[{"source":-1,"weight":1}]}],"output":0})";
    auto a=preview_graph(gain,48000,2,1000,-12);
    ok=ok&&a.ok&&a.linear&&a.response.size()>100&&std::abs(a.output_rms_db-a.input_rms_db+6)<.01;
    for(auto p:a.response)ok=ok&&std::abs(p.db+6)<.01;
    auto b=preview_graph(low,48000,2,1000,-12);
    auto at=[&](double hz){auto it=std::min_element(b.response.begin(),b.response.end(),[&](auto x,auto y){return std::abs(x.hz-hz)<std::abs(y.hz-hz);});return it==b.response.end()?999.:it->db;};
    ok=ok&&b.ok&&b.linear&&std::abs(at(1000)+3.01)<.35&&at(10000)<-35&&at(100)>-.1;
    auto c=preview_graph(clip,48000,2,1000,-12);
    ok=ok&&c.ok&&!c.linear&&c.response.empty()&&c.output_rms_db>c.input_rms_db;
    auto d=preview_graph(reactive,48000,2,1000,-12);
    ok=ok&&d.ok&&!d.linear&&d.response.empty();
    auto e=preview_graph(external,48000,2,1000,-12);
    ok=ok&&!e.ok&&e.error.find("LADSPA")!=std::string::npos;
    ok=ok&&!preview_graph("{}",48000,2,1000,-12).ok&&!preview_graph(gain,48000,2,30000,-12).ok;
    auto repeat=preview_graph(gain,48000,2,1000,-12);ok=ok&&repeat.frame.output==a.frame.output;
    std::printf("PREVIEW gain=%g low100=%g low1k=%g low10k=%g clip=%g reactive=%d error=%s pass=%d\n",a.output_rms_db-a.input_rms_db,at(100),at(1000),at(10000),c.output_rms_db-c.input_rms_db,d.ok,b.error.c_str(),ok);
    return ok?0:1;
}
