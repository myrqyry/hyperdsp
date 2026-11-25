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
        LinearRamp amount;
        int source=0,destination=-1;
    };
    std::array<Slot,modulation_slots> slots_{};
public:
    void configure(unsigned i,unsigned rate,RouteConfig c,bool reset=false){
        if(i>=slots_.size())return;
        auto& s=slots_[i];
        if(c.source<1||c.source>5||c.destination<0)c.source=0;
        if(reset)s=Slot{};
        const bool changed=s.source!=c.source||s.destination!=c.destination;
        s.source=c.source;s.destination=c.destination;
        s.signal.configure(rate,c.source,c.shape,c.hz,c.attack,c.release,c.sensitivity);
        const float amount=c.source?MixSettings::bounded(c.amount,-10000,10000,0):0;
        if(reset)s.amount.reset(amount);
        else {
            if(changed)s.amount.reset(0);
            s.amount.set(amount,std::max(1u,rate/100));
        }
    }
    bool active(unsigned i)const{return i<slots_.size()&&slots_[i].source!=0;}
    int destination(unsigned i)const{return active(i)?slots_[i].destination:-1;}
    ModulationFrame next(const float* samples,unsigned channels){
        ModulationFrame frame;
        for(unsigned i=0;i<slots_.size();++i){
            auto& s=slots_[i];
            if(s.source)frame.entries[i]={s.destination,s.signal.next(samples,channels)*s.amount.next()};
        }
        return frame;
    }
};
}
