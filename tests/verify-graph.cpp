#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace hyperdsp;
int main(){
    GraphSpec s;
    s.nodes={{"gain",{{"gain_db",6}},{{-1,1}}},
             {"native:hyperdsp_color_filter:3",{{"drive",20}},{{0,1}}}};
    s.output=1;
    GraphRuntime g;std::string error;bool ok=g.replace(s,48000,2,error)&&g.latency()==0;
    float l=.2f,r=.01f;float* p[]={&l,&r};g.process(p,2,1);
    ok=ok&&std::abs(l-1)<1e-5&&std::abs(r-.1995262)<1e-5;
    // Parallel identity and spectral branch must cancel sample-exactly.
    s.nodes={{"native:hyperdsp_spectral_filter:1",{{"range",0}},{{-1,1}}},
             {"gain",{},{{-1,1},{0,-1}}}};
    ok=ok&&g.replace(s,48000,2,error)&&g.latency()==1024;
    double max_error=0;
    for(unsigned i=0;i<6000;++i){
        l=.3f*std::sin(.071*i);r=-.4f*l;g.process(p,2,1);
        max_error=std::max(max_error,double(std::max(std::abs(l),std::abs(r))));
    }
    ok=ok&&max_error<1e-6;
    // Invalid graph must not replace the active one.
    s.nodes[0].inputs={{1,1}};
    ok=ok&&!g.replace(s,48000,2,error)&&!error.empty()&&g.latency()==1024;
    l=.1;r=.2;g.process(p,2,1);ok=ok&&std::abs(l)<1e-6&&std::abs(r)<1e-6;
    s.nodes[0].inputs={{-1,1}};s.nodes[0].params["range"]=std::numeric_limits<double>::quiet_NaN();
    ok=ok&&!g.replace(s,48000,2,error);
    s.nodes[0].params={{"unknown",1}};ok=ok&&!g.replace(s,48000,2,error);
    // Non-topological node order, output impulse and cumulative delay.
    s.nodes={{"gain",{},{{2,1},{-1,1}}},
             {"native:hyperdsp_color_filter:1",{{"mix",0},{"oversample",4}},{{-1,1}}},
             {"native:hyperdsp_spectral_filter:1",{{"range",0}},{{1,1}}}};
    s.output=0;
    ok=ok&&g.replace(s,48000,2,error)&&g.latency()==1048;
    for(unsigned i=0;i<2100;++i){
        l=i==0?.25f:0;r=i==0?.1f:0;g.process(p,2,1);
        ok=ok&&std::abs(l-(i==1048?.5f:0))<1e-6&&std::abs(r-(i==1048?.2f:0))<1e-6;
    }
    // Partition invariance even when host blocks exceed the prepared scratch size.
    GraphRuntime block,sample;
    ok=ok&&block.replace(s,44100,2,error)&&sample.replace(s,44100,2,error);
    for(unsigned k=0;k<9;++k){
        std::array<float,777> x{},y{};
        for(unsigned j=0;j<x.size();++j){x[j]=.3f*std::sin(.08*(k*x.size()+j));y[j]=-.4f*x[j];}
        auto left=x,right=y;float* buf[]={x.data(),y.data()};block.process(buf,2,x.size());
        for(unsigned j=0;j<x.size();++j){
            float a=left[j],b=right[j];float* one[]={&a,&b};sample.process(one,2,1);
            ok=ok&&std::abs(a-x[j])<1e-6&&std::abs(b-y[j])<1e-6;
        }
    }
    auto invalid=s;invalid.output=2;ok=ok&&!g.replace(invalid,48000,2,error);
    invalid=s;invalid.nodes[0].inputs[0].weight=5;ok=ok&&!g.replace(invalid,48000,2,error);
    invalid=s;invalid.nodes[0].inputs[0].source=99;ok=ok&&!g.replace(invalid,48000,2,error);
    invalid=s;invalid.nodes.resize(17);ok=ok&&!g.replace(invalid,48000,2,error);
    invalid=s;invalid.nodes[1].effect="ladspa:unqualified";ok=ok&&!g.replace(invalid,48000,2,error);
    s.nodes[1].params["oversample"]=3;ok=ok&&!g.replace(s,48000,2,error);
    // Each remaining native mode must compile and actually process audio.
    const std::pair<const char*,int> families[]={{"mic_modeler_filter",6},{"hyperdsp_native_filter",9},
        {"hyperdsp_dynamics_filter",4},{"hyperdsp_multiband_filter",1},{"hyperdsp_time_filter",5},{"hyperdsp_space_filter",2},{"hyperdsp_formant_filter",2}};
    for(auto [family,count]:families)for(int mode=1;mode<=count;++mode){
        GraphSpec native{{{std::string("native:")+family+":"+std::to_string(mode),{},{{-1,1}}}},0};
        GraphRuntime a,b;
        if(!a.replace(native,48000,2,error)||!b.replace(native,48000,2,error)){
            std::printf("GRAPH_NATIVE rejected %s: %s\n",native.nodes[0].effect.c_str(),error.c_str());ok=false;continue;
        }
        double energy=0,delta=0,right_energy=0,partition=0;bool finite=true;
        for(unsigned k=0;k<64;++k){
            std::array<float,777> x{},y{};
            for(unsigned j=0;j<x.size();++j)x[j]=.3f*std::sin(.071*(k*x.size()+j));
            auto dry=x;float* ptr[]={x.data(),y.data()};a.process(ptr,2,x.size());
            for(unsigned j=0;j<x.size();++j){
                float l=dry[j],r=0;float* single[]={&l,&r};b.process(single,2,1);
                finite=finite&&std::isfinite(x[j])&&std::isfinite(y[j]);partition=std::max(partition,double(std::max(std::abs(l-x[j]),std::abs(r-y[j]))));right_energy+=y[j]*y[j];
                energy+=x[j]*x[j];delta+=std::abs(x[j]-dry[j]);
            }
        }
        bool cross=(std::string_view(family)=="hyperdsp_time_filter"&&mode==2)||(std::string_view(family)=="hyperdsp_space_filter"&&mode==2);
        bool identity=std::string_view(family)=="hyperdsp_native_filter"&&mode>=5&&mode<=7;
        bool pass=finite&&partition<2e-6&&energy>1e-8&&(identity||delta>1e-3)&&(cross||right_energy<1e-10);
        std::printf("GRAPH_NATIVE %s:%d energy=%g delta=%g partition=%g right=%g pass=%d\n",family,mode,energy,delta,partition,right_energy,pass);
        ok=ok&&pass;
    }
    GraphSpec wrong{{{"native:hyperdsp_space_filter:1",{{"stages",3}},{{-1,1}}}},0};
    ok=ok&&!g.replace(wrong,48000,2,error);
    wrong.nodes[0]={"native:hyperdsp_time_filter:4",{{"delay_ms",20}},{{-1,1}}};
    ok=ok&&!g.replace(wrong,48000,2,error);
    wrong.nodes[0]={"native:hyperdsp_formant_filter:2",{{"f3",17000}},{{-1,1}}};
    ok=ok&&!g.replace(wrong,48000,2,error);
    wrong.nodes[0]={"native:hyperdsp_formant_filter:2",{{"from",5}},{{-1,1}}};
    ok=ok&&!g.replace(wrong,48000,2,error);
    wrong.nodes[0]={"native:hyperdsp_native_filter:1",{{"alignment",2}},{{-1,1}}};
    ok=ok&&!g.replace(wrong,48000,2,error);
    std::printf("GRAPH_CORE cancellation=%g latency=%u pass=%d\n",max_error,g.latency(),ok);
    return ok?0:1;
}
