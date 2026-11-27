// Test-only LADSPA amplifier, implementing the SDK example's port contract.
#include <ladspa.h>
#include <array>
#include <new>
struct Amp { std::array<LADSPA_Data*,5> ports{}; };
static LADSPA_Handle create(const LADSPA_Descriptor*,unsigned long){return new(std::nothrow) Amp;}
static void connect(LADSPA_Handle h,unsigned long port,LADSPA_Data* p){if(port<5)static_cast<Amp*>(h)->ports[port]=p;}
static void run(LADSPA_Handle h,unsigned long n){auto& p=static_cast<Amp*>(h)->ports;for(unsigned long i=0;i<n;++i){p[2][i]=p[1][i]* *p[0];if(p[3]&&p[4])p[4][i]=p[3][i]* *p[0];}}
static void cleanup(LADSPA_Handle h){delete static_cast<Amp*>(h);}
static const LADSPA_PortDescriptor ports[]={LADSPA_PORT_CONTROL|LADSPA_PORT_INPUT,LADSPA_PORT_AUDIO|LADSPA_PORT_INPUT,LADSPA_PORT_AUDIO|LADSPA_PORT_OUTPUT,LADSPA_PORT_AUDIO|LADSPA_PORT_INPUT,LADSPA_PORT_AUDIO|LADSPA_PORT_OUTPUT};
static const char* const names[]={"Gain","Input","Output","Input 2","Output 2"};
static const LADSPA_PortRangeHint hints[]={{LADSPA_HINT_BOUNDED_BELOW|LADSPA_HINT_LOGARITHMIC|LADSPA_HINT_DEFAULT_1,0,0},{},{},{},{}};
static LADSPA_Descriptor descriptor(bool stereo){LADSPA_Descriptor d{};d.UniqueID=stereo?1049:1048;d.Label=stereo?"amp_stereo":"amp_mono";d.Properties=LADSPA_PROPERTY_HARD_RT_CAPABLE;d.Name=stereo?"Stereo Amplifier":"Mono Amplifier";d.Maker="HyperDSP test fixture";d.Copyright="Test-only";d.PortCount=stereo?5:3;d.PortDescriptors=ports;d.PortNames=names;d.PortRangeHints=hints;d.instantiate=create;d.connect_port=connect;d.run=run;d.cleanup=cleanup;return d;}
#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C" __attribute__((visibility("default")))
#endif
const LADSPA_Descriptor* ladspa_descriptor(unsigned long index){static const std::array<LADSPA_Descriptor,2> d={descriptor(false),descriptor(true)};return index<d.size()?&d[index]:nullptr;}
