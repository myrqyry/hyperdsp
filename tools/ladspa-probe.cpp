#include <ladspa.h>
#include "../src/platform/native_library.hpp"
#include <cstdio>
#include <cmath>
#include <string>
static std::string clean(const char* s) {
    std::string out=s?s:"";
    for(char& c:out)if(c=='\t'||c=='\r'||c=='\n')c=' ';
    return out;
}
static int probe(const std::string& path) {
    void* lib=hyperdsp::platform::open_library(path);
    if(!lib)return 3;
    auto entry=hyperdsp::platform::library_symbol<LADSPA_Descriptor_Function>(lib,"ladspa_descriptor");
    if(!entry){hyperdsp::platform::close_library(lib);return 4;}
    for(unsigned i=0;i<1024;++i){
        auto* d=entry(i);if(!d)break;
        if(!d->Label||!d->Name||!LADSPA_IS_HARD_RT_CAPABLE(d->Properties)||
           !d->instantiate||!d->connect_port||!d->run||!d->cleanup||
           d->PortCount>256||!d->PortDescriptors||!d->PortNames||!d->PortRangeHints)continue;
        unsigned in=0,out=0;
        for(unsigned p=0;p<d->PortCount;++p){
            auto t=d->PortDescriptors[p];
            if(LADSPA_IS_PORT_AUDIO(t)){if(LADSPA_IS_PORT_INPUT(t))++in;else if(LADSPA_IS_PORT_OUTPUT(t))++out;}
        }
        if(in!=out||(in!=1&&in!=2))continue;
        unsigned count=0;
        for(unsigned p=0;p<d->PortCount;++p)
            if(LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p])&&LADSPA_IS_PORT_INPUT(d->PortDescriptors[p]))++count;
        std::printf("%s\t%s\t%s\t%u\t%u\n",clean(d->Label).c_str(),clean(d->Name).c_str(),clean(d->Maker).c_str(),in,count);
        for(unsigned p=0;p<d->PortCount;++p){
            if(!LADSPA_IS_PORT_CONTROL(d->PortDescriptors[p])||!LADSPA_IS_PORT_INPUT(d->PortDescriptors[p]))continue;
            auto h=d->PortRangeHints[p];
            std::printf("C\t%u\t%s\t%lu\t%.9g\t%.9g\n",p,clean(d->PortNames[p]).c_str(),
                static_cast<unsigned long>(h.HintDescriptor),double(h.LowerBound),double(h.UpperBound));
        }
    }
    hyperdsp::platform::close_library(lib);
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    return argc==2?probe(hyperdsp::platform::utf8_path(std::filesystem::path(argv[1]))):2;
}
#else
int main(int argc,char** argv) {
    return argc==2?probe(argv[1]):2;
}
#endif
