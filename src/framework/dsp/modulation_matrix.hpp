#pragma once
#include "reactive_source.hpp"
#include <array>
namespace hyperdsp {
inline constexpr unsigned modulation_slots=4;
struct RouteConfig {
    int source=0,destination=-1,shape=0;
    double hz=1,amount=0,attack=10,release=100,sensitivity=0;
};
struct ModulationFrame {
    struct Entry {int destination=-1;float value=0;};
    std::array<Entry,modulation_slots> entries{};
    float sum(int destination)const{
        float total=0;
        if(destination>=0)for(const auto& e:entries)if(e.destination==destination)total+=e.value;
        return total;
    }
};
// Sparse, fixed-capacity routing. Destination units and bounds are owned by
// the backend; this engine never allocates or calls host APIs in next().
class ModulationMatrix {
    struct Slot {
        ReactiveSource signal;
        LinearRamp amount,external;
        int source=0,destination=-1;
        unsigned ramp=480;
    };
    std::array<Slot,modulation_slots> slots_{};
public:
    void configure(unsigned i,unsigned rate,RouteConfig c,bool reset=false){
        if(i>=slots_.size())return;
        auto& s=slots_[i];
        if(c.source<1||c.source>10||c.destination<0)c.source=0;
        if(reset)s=Slot{};
        const bool changed=s.source!=c.source||s.destination!=c.destination;
        s.source=c.source;s.destination=c.destination;
        s.signal.configure(rate,c.source>=6?0:c.source,c.shape,c.hz,c.attack,c.release,c.sensitivity);
        s.ramp=std::max(1u,rate/100);
        if(reset||changed)s.external.reset(0);
        const float amount=c.source?MixSettings::bounded(c.amount,-10000,10000,0):0;
        if(reset)s.amount.reset(amount);
        else {
            if(changed)s.amount.reset(0);
            s.amount.set(amount,std::max(1u,rate/100));
        }
    }
    bool active(unsigned i)const{return i<slots_.size()&&slots_[i].source!=0;}
    int destination(unsigned i)const{return active(i)?slots_[i].destination:-1;}
    void external_values(const std::array<float,modulation_slots>& values){
        for(unsigned i=0;i<slots_.size();++i)if(slots_[i].source>=6)
            slots_[i].external.set(MixSettings::bounded(values[i],0,1,0),slots_[i].ramp);
    }
    ModulationFrame next(const float* samples,unsigned channels){
        ModulationFrame frame;
        for(unsigned i=0;i<slots_.size();++i){
            auto& s=slots_[i];
            if(s.source)frame.entries[i]={s.destination,(s.source>=6?s.external.next():s.signal.next(samples,channels))*s.amount.next()};
        }
        return frame;
    }
};
}
