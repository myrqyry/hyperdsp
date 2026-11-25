#include "framework/dsp/modulation.hpp"
#include <cmath>
#include <cstdio>
int main(){
    using namespace hyperdsp;
    auto near=[](float a,float b){return std::abs(a-b)<.0001f;};
    MixModulator mod;
    mod.reset(48000,MixSettings::from(0,0,0));
    mod.set(MixSettings::from(1,0,0),{});
    bool ok=mod.next().wet>0 && mod.next().wet<.01f;
    for(int i=2;i<480;i++)mod.next();
    ok=ok&&near(mod.next().wet,1.f);
    // A single phase feeds all channels; a 1 Hz sine reaches its peak at 1/4 s.
    mod.reset(48000,MixSettings::from(.5,0,0),{1,1.,.5});
    ok=ok&&near(mod.next().wet,.5f);
    for(int i=1;i<12000;i++)mod.next();
    ok=ok&&near(mod.next().wet,1.f);
    for(int i=12001;i<36000;i++)mod.next();
    ok=ok&&near(mod.next().wet,0.f);
    mod.reset(48000,MixSettings::from(1,0,0),{2,1.,.5});
    for(int i=0;i<12000;i++)mod.next();
    ok=ok&&near(mod.next().output_gain,1.5f);
    // Changing the target must fade out the previous destination.
    mod.set(MixSettings::from(1,0,0),{});
    ok=ok&&mod.next().output_gain>1.f;
    for(int i=0;i<480;i++)mod.next();
    ok=ok&&near(mod.next().output_gain,1.f);
    std::printf("MODULATION_CORE_PASS=%d\n",ok);
    return ok?0:1;
}
