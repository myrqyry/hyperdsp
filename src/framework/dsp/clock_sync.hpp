#pragma once
#include <samplerate.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace hyperdsp {
// Standalone clock-domain adapter. Unlike graph effects, its output frame
// count may differ from input. All application-owned buffers are prepared.
class ClockSync {
public:
    static constexpr unsigned max_frames=8192, capacity=max_frames+512;
    struct Output {
        std::array<const float*,8> data{};
        unsigned frames=0;
        uint64_t timestamp=0;
        bool valid=false;
    };
    struct Diagnostics {
        double drift_ppm=0,correction_ppm=0,error_ms=0;
        uint64_t relocks=0;
        bool locked=false,limited=false;
    };
    bool prepare(unsigned rate,unsigned channels,unsigned quality,double limit_ppm){
        if(rate<8000||rate>192000||channels<1||channels>8||quality>2||
            !std::isfinite(limit_ppm)||limit_ppm<50||limit_ppm>2000)return false;
        const int types[]={SRC_SINC_FASTEST,SRC_SINC_MEDIUM_QUALITY,SRC_SINC_BEST_QUALITY};
        int error=0;
        std::unique_ptr<SRC_STATE,Deleter> next(src_new(types[quality],int(channels),&error));
        if(!next)return false;
        std::vector<float> input(max_frames*channels),packed(capacity*channels),planar(capacity*channels);
        state_=std::move(next);input_.swap(input);packed_.swap(packed);planar_.swap(planar);
        rate_=rate;channels_=channels;limit_=limit_ppm*1e-6;relocks_=0;reset();return true;
    }
    void reset(){
        if(state_)src_reset(state_.get());
        started_=false;anchor_=last_=produced_=0;last_frames_=0;
        predicted_=elapsed_=error_=integral_=0;ratio_=1;
    }
    Diagnostics diagnostics()const{
        return {integral_*1e6,(ratio_-1)*1e6,error_*1000,relocks_,
            started_&&elapsed_>=10&&std::abs(error_)<.005&&std::abs(ratio_-1)<limit_*.999,
            std::abs(ratio_-1)>=limit_*.999};
    }
    Output process(const float*const* data,unsigned frames,uint64_t timestamp){
        if(!state_||!data||!frames||frames>max_frames||!timestamp){reset();return {};}
        for(unsigned c=0;c<channels_;++c)if(!data[c]){reset();return {};}
        const double dt=double(last_frames_)/rate_;
        if(started_&&(timestamp<=last_||std::abs(double(timestamp-last_)*1e-9-dt)>.07)){
            ++relocks_;reset();
        }
        if(!started_){anchor_=timestamp;started_=true;}
        else{
            // Negative feedback on the accumulated sample clock, with slow
            // PI loop and jitter rejection. The integral is the drift estimate.
            // Positive error means the source needs MORE output samples.
            elapsed_=double(timestamp-anchor_)*1e-9;
            const double phase=elapsed_-predicted_;
            error_+=(-std::expm1(-dt/2.0))*(phase-error_);
            const double candidate=std::clamp(integral_+.0025*error_*dt,-limit_,limit_);
            const double raw=candidate+.10*error_;
            // Conditional integration prevents windup at the correction limit.
            if(std::abs(raw)<=limit_||(raw>limit_&&error_<0)||(raw<-limit_&&error_>0))integral_=candidate;
            ratio_=1+std::clamp(integral_+.10*error_,-limit_,limit_);
        }
        for(unsigned j=0;j<frames;++j)for(unsigned c=0;c<channels_;++c){
            float x=data[c][j];input_[j*channels_+c]=std::isfinite(x)?x:0;
        }
        SRC_DATA work{};
        work.data_in=input_.data();work.data_out=packed_.data();work.input_frames=frames;
        work.output_frames=capacity;work.src_ratio=ratio_;
        if(src_process(state_.get(),&work)||work.input_frames_used!=long(frames)||
            work.output_frames_gen<0||work.output_frames_gen>long(capacity)){
            reset();return {};
        }
        Output out;out.valid=true;out.frames=unsigned(work.output_frames_gen);
        // Integer quotient/remainder avoids cumulative timestamp rounding and
        // avoids overflow from multiplying the full running sample count.
        out.timestamp=anchor_+(produced_/rate_)*1000000000ULL+(produced_%rate_)*1000000000ULL/rate_;
        for(unsigned c=0;c<channels_;++c){
            auto* dest=planar_.data()+c*capacity;out.data[c]=dest;
            for(unsigned j=0;j<out.frames;++j)dest[j]=packed_[j*channels_+c];
        }
        produced_+=out.frames;predicted_+=double(frames)*ratio_/rate_;
        last_=timestamp;last_frames_=frames;return out;
    }
private:
    struct Deleter{void operator()(SRC_STATE* p)const{src_delete(p);}};
    std::unique_ptr<SRC_STATE,Deleter> state_;
    std::vector<float> input_,packed_,planar_;
    unsigned rate_=0,channels_=0,last_frames_=0;
    uint64_t anchor_=0,last_=0,produced_=0,relocks_=0;
    bool started_=false;
    double limit_=.001,predicted_=0,elapsed_=0,error_=0,integral_=0,ratio_=1;
};
}
