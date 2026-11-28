#include "framework/dsp/realtime_handoff.hpp"
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

using hyperdsp::RealtimeHandoff;
struct State {
    unsigned value;
    std::atomic<unsigned>& destroyed;
    std::atomic<bool>& wrong_thread;
    std::thread::id control;
    ~State(){if(std::this_thread::get_id()!=control)wrong_thread=true;++destroyed;}
};

int main(){
    std::atomic<unsigned> destroyed{0};
    std::atomic<bool> wrong_thread{false},done{false},ok{true};
    const auto control=std::this_thread::get_id();
    constexpr unsigned total=50000;
    {
        RealtimeHandoff<State> handoff;
        auto make=[&](unsigned v){return std::unique_ptr<State>(new State{v,destroyed,wrong_thread,control});};
        handoff.publish(make(1));
        std::atomic<bool> ready{false},advance{false},adopted{false};
        std::thread audio([&]{
            auto* first=handoff.consume();
            if(!first||first->value!=1)ok=false;
            ready=true;
            while(!advance.load())std::this_thread::yield();
            // Control published several candidates while the first packet
            // was still borrowed by downstream. It must remain alive.
            if(!first||first->value!=1)ok=false;
            auto* latest=handoff.consume();
            if(!latest||latest->value!=3)ok=false;
            unsigned last=latest?latest->value:0;
            adopted=true;
            while(!done.load()||last<total){
                auto* state=handoff.consume();
                if(!state||state->value<last){ok=false;break;}
                last=state->value;
            }
        });
        while(!ready.load())std::this_thread::yield();
        handoff.publish(make(2));handoff.publish(make(3));
        if(destroyed.load()!=1)ok=false; // only superseded pending state
        advance=true;
        while(!adopted.load())std::this_thread::yield();
        for(unsigned i=4;i<=total;++i)handoff.publish(make(i));
        done=true;
        audio.join();
    }
    bool pass=ok&&!wrong_thread&&destroyed==total;
    std::printf("REALTIME_HANDOFF destroyed=%u wrong_thread=%d pass=%d\n",destroyed.load(),wrong_thread.load(),pass);
    return pass?0:1;
}
