#include "framework/dsp/multiband.hpp"
#include "framework/dsp/effect_graph.hpp"
#include <cstdio>
#include <numbers>
using namespace hyperdsp;
int main(){
    bool ok=true;
    for(unsigned rate:{8000u,44100u,48000u,96000u,192000u}){
        for(double hz:{80.,250.,1000.,2500.,3500.}){
            if(hz>=rate*.45)continue;
            MultibandSettings s;s.ratio={1,1,1};s.wet=.5;
            Multiband engine;engine.reset(rate,s);
            std::array<float,256> l{},r{};float* p[]={l.data(),r.data()};
            double sum=0,reference=0,stereo=0;
            for(unsigned b=0;b<rate/256;++b){
                for(unsigned j=0;j<256;++j)l[j]=.3*std::sin(2*std::numbers::pi*hz*(b*256+j)/rate),r[j]=-.5*l[j];
                if(b>rate/512)for(auto x:l)reference+=x*x;
                engine.process(p,2,256);
                if(b>rate/512)for(unsigned j=0;j<256;++j){sum+=l[j]*l[j];stereo=std::max(stereo,std::abs(double(r[j]+.5f*l[j])));}
            }
            auto db=10*std::log10(sum/reference);
            ok=ok&&std::abs(db)<.12&&stereo<1e-5;
            std::printf("RECOMBINE rate=%u hz=%g db=%g stereo=%g\n",rate,hz,db,stereo);
        }
    }
    auto measure=[](double hz,bool compress){
        MultibandSettings s;s.ratio={compress?8.f:1.f,1,1};s.threshold={-30,-30,-30};s.attack=1;s.release=80;
        Multiband e;e.reset(48000,s);std::array<float,256> l{};float* p[]={l.data()};
        double sum=0;unsigned n=0;
        for(unsigned b=0;b<190;++b){for(unsigned j=0;j<256;++j)l[j]=.5*std::sin(2*std::numbers::pi*hz*(b*256+j)/48000);e.process(p,1,256);
            if(b>100)for(auto x:l){sum+=x*x;++n;}}
        return std::sqrt(sum/n);
    };
    double low=measure(80,true)/measure(80,false),high=measure(6000,true)/measure(6000,false);
    ok=ok&&low<.25&&high>.98;
    GraphSpec spec;spec.nodes.push_back({"native:hyperdsp_multiband_filter:1",{{"low_ratio",1},{"mid_ratio",1},{"high_ratio",1}},{{-1,1}}});spec.output=0;
    spec.controls.push_back({"low","Low compression",.5,{{0,"low_ratio",1,8}}});
    std::string error;auto graph=PreparedGraph::prepare(spec,48000,2,error);ok=ok&&graph&&graph->latency()==0;
    spec.controls[0].bindings[0].parameter="low_hz";spec.controls[0].bindings[0].low=100;spec.controls[0].bindings[0].high=300;
    ok=ok&&!PreparedGraph::prepare(spec,48000,2,error);
    std::printf("MULTIBAND_PASS=%d low_ratio=%g high_ratio=%g\n",ok,low,high);return ok?0:1;
}
