#include <ladspa.h>
#include <dlfcn.h>
#include <cstdio>
#include <string>
static std::string clean(const char* s) {
    std::string out=s?s:"";
    for(char& c:out)if(c=='\t'||c=='\r'||c=='\n')c=' ';
    return out;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib)return 3;
    auto entry=reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(lib,"ladspa_descriptor"));
    if(!entry){dlclose(lib);return 4;}
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
        std::printf("%s\t%s\t%s\t%u\n",clean(d->Label).c_str(),clean(d->Name).c_str(),clean(d->Maker).c_str(),in);
    }
    dlclose(lib);
}
