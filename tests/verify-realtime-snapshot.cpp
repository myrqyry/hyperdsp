#include "framework/dsp/realtime_snapshot.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <cstdint>
using hyperdsp::RealtimeSnapshot;
struct Frame {
    uint64_t sequence=0;
    std::array<uint64_t,2048> data{};
};
static Frame make(uint64_t n){Frame f;f.sequence=n;for(unsigned i=0;i<f.data.size();++i)f.data[i]=n*7919+i;return f;}
static bool coherent(const Frame& f){for(unsigned i=0;i<f.data.size();++i)if(f.data[i]!=f.sequence*7919+i)return false;return true;}
int main(){
    RealtimeSnapshot<Frame> snapshot;
    std::atomic<bool> ok{true};
    if(snapshot.visit([](const Frame&){return true;}))ok=false;
    if(!snapshot.publish(make(1)))ok=false;
    std::atomic<bool> pin1{false},pin2{false},release{false};
    auto hold=[&](unsigned expected,std::atomic<bool>& pinned){
        if(!snapshot.visit([&](const Frame& f){
            pinned=true;
            while(!release.load())std::this_thread::yield();
            return f.sequence==expected&&coherent(f);
        }))ok=false;
    };
    std::thread first(hold,1,std::ref(pin1));
    while(!pin1.load())std::this_thread::yield();
    if(!snapshot.publish(make(2)))ok=false;
    std::thread second(hold,2,std::ref(pin2));
    while(!pin2.load())std::this_thread::yield();
    if(!snapshot.publish(make(3))||snapshot.publish(make(4)))ok=false;
    if(!snapshot.visit([](const Frame& f){return f.sequence==3&&coherent(f);}))ok=false;
    release=true;first.join();second.join();
    if(!snapshot.publish(make(4)))ok=false;
    std::atomic<bool> done{false};
    std::atomic<unsigned> reads{0},ready{0};
    std::array<std::thread,4> readers;
    for(auto& reader:readers)reader=std::thread([&]{
        uint64_t previous=0;++ready;
        do{
            snapshot.visit([&](const Frame& f){
                if(!coherent(f)||f.sequence<previous)ok=false;
                previous=f.sequence;++reads;return true;
            });
        }while(!done.load());
    });
    while(ready.load()!=readers.size())std::this_thread::yield();
    unsigned published=0,dropped=0;
    for(unsigned i=5;i<=100004;++i){if(snapshot.publish(make(i)))++published;else ++dropped;}
    done=true;for(auto& reader:readers)reader.join();
    if(!snapshot.publish(make(100005)))ok=false;
    if(!snapshot.visit([](const Frame& f){return f.sequence==100005&&coherent(f);}))ok=false;
    bool pass=ok&&reads>100&&published>100;
    std::printf("REALTIME_SNAPSHOT published=%u dropped=%u reads=%u pass=%d\n",published,dropped,reads.load(),pass);
    return pass?0:1;
}
