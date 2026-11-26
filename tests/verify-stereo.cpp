#include "framework/dsp/stereo.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <numbers>
using namespace hyperdsp;
int main(){
    bool ok=true;
    auto sample=[](StereoSettings s,float l,float r){
        Stereo engine;engine.reset(48000,s);float* p[]={&l,&r};engine.process(p,2,1);return std::array<float,2>{l,r};
    };
    StereoSettings s;
    auto identity=sample(s,.4f,-.2f);ok=ok&&std::abs(identity[0]-.4)<1e-6&&std::abs(identity[1]+.2)<1e-6;
    s.width=0;auto mono=sample(s,.4f,-.2f);ok=ok&&std::abs(mono[0]-.1)<1e-6&&mono[0]==mono[1];
    s.width=2;auto wide=sample(s,.4f,-.2f);ok=ok&&std::abs(wide[0]-.7)<1e-6&&std::abs(wide[1]+.5)<1e-6;
    s={};s.balance=1;auto right=sample(s,.4f,-.2f);ok=ok&&right[0]==0&&std::abs(right[1]+.2)<1e-6;
    s={};s.mid_db=-6;s.side_db=6;auto ms=sample(s,.4f,-.2f);
    ok=ok&&std::abs((ms[0]+ms[1])/2-.1*std::pow(10.,-.3))<1e-6&&std::abs((ms[0]-ms[1])/2-.3*std::pow(10.,.3))<1e-6;
    s.wet=0;auto dry=sample(s,.4f,-.2f);ok=ok&&std::abs(dry[0]-.4)<1e-6&&std::abs(dry[1]+.2)<1e-6;
    Stereo e;e.reset(48000,{});float l=.4,r=-.2,center=.1;float* surround[]={&l,&r,&center};
    e.process(surround,3,1);ok=ok&&l==.4f&&r==-.2f&&center==.1f;
    StereoAnalysis meter;meter.reset(48000);
    for(int i=0;i<48000;++i)meter.next(.4,-.4);
    auto a=meter.snapshot();ok=ok&&a.valid&&std::abs(a.correlation+1)<1e-6&&a.mono_loss_db<-100;
    meter.reset(48000);for(int i=0;i<48000;++i)meter.next(.4,.4);
    a=meter.snapshot();ok=ok&&a.valid&&std::abs(a.correlation-1)<1e-6&&std::abs(a.mono_loss_db)<1e-6;
    meter.reset(48000);for(int i=0;i<48000;++i)meter.next(.4,0);
    a=meter.snapshot();ok=ok&&a.valid&&!a.correlation_valid&&std::abs(a.mono_loss_db+3.0103)<.001;
    meter.reset(48000);ok=ok&&!meter.snapshot().valid;
    GraphSpec spec{{{"native:hyperdsp_stereo_filter:1",{{"width",2}},{{-1,1}}}},0};
    std::string error;auto g=PreparedGraph::prepare(spec,48000,2,error);
    ok=ok&&g&&g->latency()==0&&!PreparedGraph::prepare(spec,48000,1,error)&&!PreparedGraph::prepare(spec,48000,6,error);
    if(g){float l=.4,r=-.2;float* p[]={&l,&r};g->process(p,2,1);ok=ok&&std::abs(l-.7)<1e-6&&std::abs(r+.5)<1e-6;}
    // Ramped updates must produce identical samples across packet partitions.
    Stereo block,single;block.reset(48000,{});single.reset(48000,{});
    s={};s.width=1.8;s.balance=-.3;s.mid_db=-3;
    block.set(s);single.set(s);
    std::array<float,777> left{},rightbuf{};
    for(unsigned i=0;i<left.size();++i){left[i]=.3*std::sin(i*.13);rightbuf[i]=.2*std::cos(i*.09);}
    auto aleft=left,aright=rightbuf;float* p[]={left.data(),rightbuf.data()};block.process(p,2,left.size());
    double diff=0;
    for(unsigned i=0;i<left.size();++i){float* q[]={&aleft[i],&aright[i]};single.process(q,2,1);diff=std::max({diff,std::abs(double(left[i]-aleft[i])),std::abs(double(rightbuf[i]-aright[i]))});}
    ok=ok&&diff<1e-7;
    std::printf("STEREO_PASS=%d width2=%g/%g mono=%g partition=%g single_channel_fold=%g\n",ok,wide[0],wide[1],mono[0],diff,a.mono_loss_db);
    return ok?0:1;
}
