#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <cmath>
using namespace hyperdsp;
int main(){
    GraphSpec s{{{"gain",{},{{-1,1}}},{"native:hyperdsp_color_filter:3",{{"drive",0}},{{0,1}}}},1};
    s.controls={{"intensity","Intensity",.5,{{0,"gain_db",-12,0},{1,"drive",0,12}}}};
    std::string error;auto g=PreparedGraph::prepare(s,48000,2,error);
    if(!g){std::printf("%s\n",error.c_str());return 1;}
    float l=.2,r=-.08;float* p[]={&l,&r};
    g->process(p,2,1);
    bool ok=std::abs(l-.2)<1e-5&&std::abs(r+.08)<1e-5;
    ok=ok&&g->set_controls({{"intensity",1}},error);
    for(unsigned i=0;i<1024;++i){l=.2;r=-.08;g->process(p,2,1);}
    ok=ok&&std::abs(l-.796214)<1e-5&&std::abs(r+.318486)<1e-5;
    ok=ok&&!g->set_controls({{"intensity",1.1}},error);
    l=.2;r=-.08;g->process(p,2,1);ok=ok&&std::abs(l-.796214)<1e-5;
    auto bad=s;bad.controls[0].bindings[1].high=100;
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    bad=s;bad.controls[0].bindings[1].parameter="oversample";
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    bad=s;bad.controls.push_back({"other","Other",0,{{0,"gain_db",0,6}}});
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    bad=s;bad.controls[0].bindings.push_back({0,"unknown",0,1});
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    bad=s;bad.controls[0].id="bad id";
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    bad=s;bad.controls[0].bindings[0].node=99;
    ok=ok&&!PreparedGraph::prepare(bad,48000,2,error);
    auto restored=PreparedGraph::prepare(s,48000,2,error,{},{{"intensity",1}});
    if(!restored)return 3;
    l=.2;r=-.08;restored->process(p,2,1);ok=ok&&std::abs(l-.796214)<1e-5;
    // Live macro edits must not reset the delay line or change fixed latency.
    GraphSpec delay{{{"native:hyperdsp_time_filter:1",{{"delay_ms",100},{"feedback",0},{"mix",1}},{{-1,1}}}},0};
    delay.controls={{"level","Level",0,{{0,"output_db",0,-6}}}};
    auto echo=PreparedGraph::prepare(delay,48000,1,error);
    if(!echo)return 2;
    double impulse=0;
    for(unsigned i=0;i<5000;++i){
        if(i==1000)ok=ok&&echo->set_controls({{"level",1}},error);
        l=i==0?1:0;echo->process(p,1,1);if(i==4800)impulse=l;
    }
    ok=ok&&std::abs(impulse-.501187)<1e-5;
    std::printf("GRAPH_CONTROLS level=%g preserved_delay=%g pass=%d\n",.796214,impulse,ok);return ok?0:1;
}
