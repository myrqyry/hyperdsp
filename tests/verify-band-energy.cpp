#include "framework/dsp/band_energy.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace hyperdsp;
double measure(Band band,double hz,unsigned rate,bool opposite=false,bool symmetric=false){
    BandEnergy d;d.configure(rate,band,10,symmetric?10:100,0);
    double sum=0;
    for(unsigned i=0;i<rate;++i){
        float x=.5f*std::sin(2*3.141592653589793*hz*i/rate);
        float a[]={x,opposite?-x:0.f};
        auto v=d.next(a,2);
        if(i>=rate/2)sum+=v;
    }
    return sum/(rate/2);
}
int main(){
    bool ok=true;
    for(unsigned rate:{44100u,48000u,96000u}){
        for(unsigned b=0;b<3;++b){
            double levels[3];
            const double hz[]={60,800,8000};
            for(unsigned j=0;j<3;++j)levels[j]=measure(Band(b),hz[j],rate);
            ok=ok&&levels[b]>.42&&levels[b]<.47;
            for(unsigned j=0;j<3;++j)if(j!=b)ok=ok&&levels[j]<.09;
            ok=ok&&std::abs(measure(Band(b),hz[b],rate,true)-levels[b])<1e-6;
            auto rms=measure(Band(b),hz[b],rate,false,true);
            ok=ok&&rms>.33&&rms<.36;
            std::printf("BAND_ENERGY rate=%u band=%u low=%g mid=%g high=%g\n",rate,b,levels[0],levels[1],levels[2]);
        }
    }
    BandEnergy d;d.configure(48000,Band::Bass,10,100,6.0205999);
    float a[]={.5f};
    for(int i=0;i<48000;++i)d.next(a,1);
    ok=ok&&d.next(a,1)>.99;
    a[0]=std::numeric_limits<float>::quiet_NaN();
    ok=ok&&std::isfinite(d.next(a,1));
    a[0]=0;
    for(int i=0;i<96000;++i)d.next(a,1);
    ok=ok&&d.next(a,1)<.001;
    std::printf("BAND_ENERGY_PASS=%d\n",ok);return ok?0:1;
}
