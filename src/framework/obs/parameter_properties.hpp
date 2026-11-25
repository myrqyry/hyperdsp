#pragma once
#include <obs-module.h>
#include "framework/dsp/parameter_schema.hpp"
namespace hyperdsp::obs_parameters {
template<class S,size_t N> void read(obs_data_t* data,S& s,const FloatParameter<S>(&spec)[N]){
    for(auto& p:spec)s.*p.member=float(obs_data_get_double(data,p.key));
}
template<class S,size_t N> void defaults(obs_data_t* data,const S& s,const FloatParameter<S>(&spec)[N]){
    for(auto& p:spec)obs_data_set_default_double(data,p.key,s.*p.member);
}
template<class S> obs_property_t* add(obs_properties_t* props,const FloatParameter<S>& p){
    auto* property=obs_properties_add_float_slider(props,p.key,p.name,p.min,p.max,p.step);
    if(*p.unit)obs_property_float_set_suffix(property,p.unit);
    return property;
}
}
