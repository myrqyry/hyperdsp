#include "framework/dsp/effect_mix.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
int main(){
    using hyperdsp::MixSettings;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const auto near=[](float a,float b){return std::abs(a-b)<1e-6f;};
    const auto identity=MixSettings::from(1.,0.,0.);
    const auto parallel=MixSettings::from(.5,0.,0.);
    const auto trim=MixSettings::from(.5,-6.020599913,6.020599913);
    const auto dry=MixSettings::from(0.,0.,0.);
    const auto invalid=MixSettings::from(nan,nan,nan);
    const auto bounded=MixSettings::from(10.,100.,-100.);
    const bool pass=near(identity.output(.4f,.4f,.2f),.2f)
        && near(parallel.output(.4f,.4f,.2f),.3f)
        && near(trim.input(.4f),.2f)
        && near(trim.output(.4f,.2f,.1f),.3f)
        && near(dry.output(.4f,.4f,nan),.4f)
        && near(parallel.output(.4f,.4f,nan),.4f)
        && near(invalid.wet,1.f)&&near(invalid.input_gain,1.f)&&near(invalid.output_gain,1.f)
        && near(bounded.wet,1.f)&&near(bounded.input_gain,std::pow(10.f,1.2f))
        && near(bounded.output_gain,std::pow(10.f,-1.2f));
    std::printf("EFFECT_MIX_PASS=%d\n",pass);
    return pass?0:1;
}
