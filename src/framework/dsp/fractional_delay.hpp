#pragma once
#include <algorithm>
#include <vector>
#include <cstddef>
namespace hyperdsp {
// All storage is prepared by reset() on the control thread. Reads use
// linear fractional interpolation; write positions advance once per sample.
class FractionalDelay {
    std::vector<float> data_;
    unsigned length_=0,channels_=0,pos_=0;
public:
    void prepare(unsigned rate,unsigned channels){prepare_samples(2*rate,channels);}
    void prepare_samples(unsigned max_delay,unsigned channels){
        length_=std::max(1u,max_delay)+2;channels_=channels;data_.assign(size_t(length_)*channels_,0);pos_=0;
    }
    void clear(){std::fill(data_.begin(),data_.end(),0.f);pos_=0;}
    float read(unsigned c,float delay)const{
        if(c>=channels_||!length_)return 0;
        delay=std::clamp(delay,1.f,float(length_-2));
        unsigned n=unsigned(delay);float frac=delay-n;
        unsigned a=pos_>=n?pos_-n:pos_+length_-n,b=a?a-1:length_-1;
        const auto* row=data_.data()+size_t(c)*length_;
        return row[a]+frac*(row[b]-row[a]);
    }
    void write(unsigned c,float value){data_[size_t(c)*length_+pos_]=value;}
    void advance(){if(++pos_==length_)pos_=0;}
};
}
