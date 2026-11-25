#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <cmath>
using namespace hyperdsp;
int main(){
    GraphSpec s{{{"gain",{},{{-1,1}}},{"gain",{},{{0,1}}}},1};
    s.controls={{"level","Level",.5,{{0,"gain_db",-12,0},{1,"gain_db",-12,0}}}};
    s.routes={{"level",{1,-1,0,2,.5,10,100,0},"",""}};
    std::string error;
    auto one=PreparedGraph::prepare(s,48000,2,error),block=PreparedGraph::prepare(s,48000,2,error);
    if(!one||!block){std::printf("%s\n",error.c_str());return 1;}
    double low=1,high=0,err=0;
    for(unsigned k=0;k<128;++k){
        std::array<float,777> x{},y{};x.fill(.4f);y.fill(-.16f);float* p[]={x.data(),y.data()};block->process(p,2,x.size());
        for(unsigned i=0;i<x.size();++i){
            float l=.4f,r=-.16f;float* q[]={&l,&r};one->process(q,2,1);
            err=std::max(err,double(std::abs(l-x[i])));err=std::max(err,double(std::abs(r-y[i])));
            if(k>20){low=std::min(low,double(l));high=std::max(high,double(l));}
        }
    }
    bool ok=err<1e-6&&low<.027&&high>.38&&high<=.40001;
    auto invalid=s;invalid.routes[0].control="missing";ok=ok&&!PreparedGraph::prepare(invalid,48000,2,error);
    invalid=s;invalid.routes[0].config.amount=2;ok=ok&&!PreparedGraph::prepare(invalid,48000,2,error);
    invalid=s;invalid.routes[0].config.source=11;ok=ok&&!PreparedGraph::prepare(invalid,48000,2,error);
    s.controls[0].initial=0;s.routes={{"level",{6,-1,0,1,1,10,100,0},"fixture","Speech"}};
    auto external=PreparedGraph::prepare(s,48000,2,error);
    if(!external)return 2;
    external->external_values({.5,0,0,0});
    float l=0,r=0;float* q[]={&l,&r};
    for(unsigned i=0;i<2400;++i){l=.4;r=-.16;external->process(q,2,1);}
    double active=l;
    external->external_values({0,0,0,0});
    for(unsigned i=0;i<2400;++i){l=.4;r=-.16;external->process(q,2,1);}
    ok=ok&&std::abs(active-.1004755)<1e-5&&std::abs(l-.0252383)<1e-5;
    s.routes={{"level",{2,-1,0,1,1,1,10,0},"",""}};
    auto env=PreparedGraph::prepare(s,48000,2,error);
    if(!env)return 3;
    for(unsigned i=0;i<2400;++i){l=.5;r=0;env->process(q,2,1);}
    ok=ok&&std::abs(l-.1255943)<1e-4;
    std::printf("GRAPH_MOD low=%g high=%g partition=%g external=%g fallback=%g envelope=%g pass=%d\n",low,high,err,active,.0252383,l,ok);return ok?0:1;
}
