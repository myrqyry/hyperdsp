#include "framework/dsp/modulation_matrix.hpp"
#include <cmath>
#include <cstdio>
using namespace hyperdsp;
int main(){
    ModulationMatrix matrix;
    RouteConfig a{2,0,0,1,.5,10,100,0}, b=a,c=a,d=a;
    b.amount=.25;c.destination=3;c.amount=-.5;d.amount=-.25;
    matrix.configure(0,48000,a,true);matrix.configure(1,48000,b,true);
    matrix.configure(2,48000,c,true);
    float x[]={.8f,-.8f};
    ModulationFrame frame{};
    for(int i=0;i<48000;++i)frame=matrix.next(x,2);
    auto near=[](float a,float b){return std::abs(a-b)<.001f;};
    bool ok=near(frame.sum(0),.6)&&near(frame.sum(3),-.4)&&frame.sum(1)==0;
    matrix.configure(3,48000,d,false);
    for(int i=0;i<48000;++i)frame=matrix.next(x,2);
    ok=ok&&near(frame.sum(0),.4);
    b.source=0;matrix.configure(1,48000,b,false);
    d.source=0;matrix.configure(3,48000,d,false);
    frame=matrix.next(x,2);
    ok=ok&&near(frame.sum(0),.4)&&near(frame.sum(3),-.4);
    a.destination=2;matrix.configure(0,48000,a,false);
    frame=matrix.next(x,2);
    ok=ok&&frame.sum(0)==0&&std::abs(frame.sum(2))<.01;
    std::printf("MOD_MATRIX_PASS=%d\n",ok);return ok?0:1;
}
