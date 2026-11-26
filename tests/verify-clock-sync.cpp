#include "framework/dsp/clock_sync.hpp"
#include <cstdio>
#include <cmath>
using namespace hyperdsp;
int main(){
    bool ok=true;
    for(double ppm:{-400.,400.,0.}){
        ClockSync clock;
        ok=clock.prepare(48000,2,1,1000)&&ok;
        std::array<float,480> l{},r{};
        const float* in[]={l.data(),r.data()};
        uint64_t base=1000000000000ULL,previous=0,total=0;
        double tailError=0,sum=0;unsigned n=0;
        for(unsigned block=0;block<15000;++block){
            for(unsigned i=0;i<480;++i){
                l[i]=.4f*std::sin((block*480+i)*.071);r[i]=-.4f*l[i];
            }
            auto ts=base+uint64_t(block*10000000.0*(1+ppm/1e6)+200000.0*std::sin(block*.23));
            auto out=clock.process(in,480,ts);
            ok=ok&&out.valid&&out.frames<=512;
            if(out.frames){
                if(previous)ok=ok&&out.timestamp==previous;
                total+=out.frames;previous=base+total*1000000000ULL/48000;
                for(unsigned i=0;i<out.frames;++i){
                    ok=ok&&std::isfinite(out.data[0][i])&&std::abs(out.data[1][i]+.4f*out.data[0][i])<1e-5;
                    if(block>14000){sum+=out.data[0][i]*out.data[0][i];++n;}
                }
            }
            if(block>14000)tailError=std::max(tailError,std::abs(double(total)-double((block+1)*480)*(1+ppm/1e6)));
        }
        auto d=clock.diagnostics();
        auto rms=std::sqrt(sum/n);
        ok=ok&&d.locked&&std::abs(d.correction_ppm-ppm)<35&&tailError<250&&std::abs(rms-.282843)<.003;
        std::printf("CLOCK ppm=%g correction=%g drift=%g error_ms=%g frame_error=%g rms=%g lock=%d\
",ppm,d.correction_ppm,d.drift_ppm,d.error_ms,tailError,rms,d.locked);
        auto reset=clock.process(in,480,base+200000000000ULL);
        ok=ok&&reset.valid&&!clock.diagnostics().locked&&clock.diagnostics().relocks==1&&reset.timestamp==base+200000000000ULL;
        ok=ok&&!clock.process(in,480,0).valid;
        ok=ok&&!clock.process(in,9000,base).valid;
    }
    // Variable packet sizes, a different rate and every quality setting.
    for(unsigned quality=0;quality<3;++quality){
        ClockSync clock;ok=clock.prepare(44100,1,quality,500)&&ok;
        std::array<float,2048> mono{};mono.fill(.2);const float* p[]={mono.data()};
        uint64_t input=0,output=0,base=2000000000000ULL;
        for(unsigned i=0;i<400;++i){
            unsigned frames=std::array<unsigned,4>{1,257,1024,2048}[i%4];
            auto out=clock.process(p,frames,base+input*1000000000ULL/44100);
            ok=ok&&out.valid&&out.timestamp==base+output*1000000000ULL/44100;
            output+=out.frames;input+=frames;
        }
        ok=ok&&input>=output&&input-output<256;
    }
    ClockSync limited;ok=limited.prepare(48000,1,0,100)&&ok;
    std::array<float,480> mono{};const float* p[]={mono.data()};
    for(unsigned i=0;i<3000;++i)ok=limited.process(p,480,1000000000000ULL+uint64_t(i)*10010000ULL).valid&&ok;
    auto cap=limited.diagnostics();ok=ok&&cap.limited&&!cap.locked&&std::abs(cap.correction_ppm-100)<1e-6;
    std::printf("CLOCK_LIMIT correction=%g limited=%d\n",cap.correction_ppm,cap.limited);
    std::printf("CLOCK_CORE_PASS=%d\
",ok);return ok?0:1;
}
