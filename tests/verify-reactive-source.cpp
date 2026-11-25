#include "framework/dsp/reactive_source.hpp"
#include <cstdio>
#include <limits>
using namespace hyperdsp;
int main(){
    ReactiveSource s;
    float x[]={0,0};
    bool ok=true;
    const float expected[4][4]={{0,1,0,-1},{-1,0,1,0},{1,1,-1,-1},{-1,-.5,0,.5}};
    for(int shape=0;shape<4;++shape){
        s.configure(48000,0,shape,1,10,100,0);
        s.configure(48000,1,shape,1,10,100,0);
        for(int quarter=0;quarter<4;++quarter){
            // Allow rounding around the square-wave discontinuity.
            float v=s.next(x,2);
            if(shape==2&&quarter==2){v=s.next(x,2);for(int i=2;i<12000;++i)s.next(x,2);}
            else for(int i=1;i<12000;++i)s.next(x,2);
            ok=ok&&std::abs(v-expected[shape][quarter])<.001f;
        }
    }
    s.configure(48000,2,0,1,10,100,0);
    x[0]=.5;x[1]=-.5;
    for(int i=0;i<48000;++i)s.next(x,2);
    ok=ok&&std::abs(s.next(x,2)-.5)<.001;
    x[0]=std::numeric_limits<float>::quiet_NaN();x[1]=0;
    for(int i=0;i<48000;++i)s.next(x,2);
    ok=ok&&s.next(x,2)<.001;
    s.configure(48000,0,0,1,10,100,0);
    ok=ok&&s.next(x,2)==0;
    std::printf("REACTIVE_SOURCE_PASS=%d\n",ok);
    return ok?0:1;
}
