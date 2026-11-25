#pragma once
#include <algorithm>
#include <cmath>

namespace hyperdsp {
// Backend-independent linear blend. Trim before the effect and both blend
// branches; output trim after the blend. Invalid processed samples stay dry.
struct MixSettings {
    float wet=1.f,input_gain=1.f,output_gain=1.f;
    static float bounded(double v,double lo,double hi,double fallback) {
        return float(std::isfinite(v)?std::clamp(v,lo,hi):fallback);
    }
    static MixSettings from(double wet,double in_db,double out_db) {
        return {bounded(wet,0.,1.,1.),
                std::pow(10.f,bounded(in_db,-24.,24.,0.)/20.f),
                std::pow(10.f,bounded(out_db,-24.,24.,0.)/20.f)};
    }
    float input(float dry) const {return dry*input_gain;}
    float output(float dry,float trimmed,float processed) const {
        if(!std::isfinite(processed)&&wet>0.f)return dry;
        const float blend=wet==0.f?trimmed:wet==1.f?processed:trimmed*(1.f-wet)+processed*wet;
        const float result=blend*output_gain;
        return std::isfinite(result)?result:dry;
    }
};
}
