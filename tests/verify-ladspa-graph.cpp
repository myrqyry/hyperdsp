#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <cmath>
using namespace hyperdsp;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string mono=std::string("ladspa:")+argv[1]+"\tamp_mono",stereo=std::string("ladspa:")+argv[1]+"\tamp_stereo";
    GraphSpec s{{{mono,{{"port_0",.5}},{{-1,1}}},{"gain",{{"gain_db",-6}},{{0,1}}}},1};
    GraphRuntime g;std::string error;bool ok=!g.replace(s,48000,2,error);
    std::vector<std::string> allowed{mono,stereo};
    ok=ok&&g.replace(s,48000,2,error,allowed)&&g.latency()==0;
    float l=.4f,r=-.16f;float* p[]={&l,&r};g.process(p,2,1);
    ok=ok&&std::abs(l-.1002374)<1e-5&&std::abs(r+.04009497)<1e-5;
    auto bad=s;bad.nodes[0].params["port_999"]=1;ok=ok&&!g.replace(bad,48000,2,error,allowed);
    bad=s;bad.nodes[0].params["port_0"]=-1;ok=ok&&!g.replace(bad,48000,2,error,allowed);
    bad=s;bad.nodes[0].effect=stereo;ok=ok&&!g.replace(bad,48000,1,error,allowed);
    l=.4;r=-.16;g.process(p,2,1);ok=ok&&std::abs(l-.1002374)<1e-5;
    s.nodes[0].effect=stereo;s.nodes[0].params={{"port_0",.5},{"mix",.5}};
    s.nodes[1]={"native:hyperdsp_spectral_filter:1",{{"range",0}},{{0,1}}};
    s.nodes.push_back({"gain",{},{{1,1},{-1,-.75}}});s.output=2;
    ok=ok&&g.replace(s,48000,2,error,allowed)&&g.latency()==1024;
    double peak=0;
    for(unsigned i=0;i<6000;++i){l=.4f*std::sin(.071*i);r=-.4f*l;g.process(p,2,1);peak=std::max(peak,double(std::max(std::abs(l),std::abs(r))));}
    ok=ok&&peak<1e-6;
    std::printf("LADSPA_GRAPH cancellation=%g pass=%d error=%s\n",peak,ok,error.c_str());return ok?0:1;
}
