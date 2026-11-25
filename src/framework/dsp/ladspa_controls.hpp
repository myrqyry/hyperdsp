#pragma once
#include <ladspa.h>
#include <algorithm>
#include <cmath>
#include <string>
namespace hyperdsp {
struct Control {
    unsigned port;
    std::string name;
    float min,max,initial;
    bool integer,toggle;
};
inline std::string key(unsigned port) { return "port_"+std::to_string(port); }
inline std::string target_id(const std::string& path,const std::string& label,unsigned port){
    return "port:"+path+"\t"+label+"\t"+std::to_string(port);
}
inline Control control_from_hint(unsigned p,const std::string& name,LADSPA_PortRangeHint h,unsigned rate) {
    auto f=h.HintDescriptor;
    const float scale=LADSPA_IS_HINT_SAMPLE_RATE(f)?float(rate):1.f;
    float lo=LADSPA_IS_HINT_BOUNDED_BELOW(f)?h.LowerBound*scale:-10000.f;
    float hi=LADSPA_IS_HINT_BOUNDED_ABOVE(f)?h.UpperBound*scale:10000.f;
    if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi) {lo=0;hi=1;}
    float initial=std::clamp(0.f,lo,hi);
    auto interp=[&](float t) {
        return LADSPA_IS_HINT_LOGARITHMIC(f)&&lo>0&&hi>0
            ? std::exp(std::log(lo)*(1-t)+std::log(hi)*t):lo*(1-t)+hi*t;
    };
    switch(f & LADSPA_HINT_DEFAULT_MASK) {
        case LADSPA_HINT_DEFAULT_MINIMUM: initial=lo;break;
        case LADSPA_HINT_DEFAULT_LOW: initial=interp(.25f);break;
        case LADSPA_HINT_DEFAULT_MIDDLE: initial=interp(.5f);break;
        case LADSPA_HINT_DEFAULT_HIGH: initial=interp(.75f);break;
        case LADSPA_HINT_DEFAULT_MAXIMUM: initial=hi;break;
        case LADSPA_HINT_DEFAULT_0: initial=0;break;
        case LADSPA_HINT_DEFAULT_1: initial=1;break;
        case LADSPA_HINT_DEFAULT_100: initial=100;break;
        case LADSPA_HINT_DEFAULT_440: initial=440;break;
    }
    bool toggle=LADSPA_IS_HINT_TOGGLED(f), integer=LADSPA_IS_HINT_INTEGER(f);
    if(toggle){lo=0;hi=1;initial=initial>0?1:0;}
    return {p,name,lo,hi,std::clamp(initial,lo,hi),integer,toggle};
}
inline Control control(const LADSPA_Descriptor* d,unsigned p,unsigned rate){
    return control_from_hint(p,d->PortNames[p]?d->PortNames[p]:"Control",d->PortRangeHints[p],rate);
}
}
