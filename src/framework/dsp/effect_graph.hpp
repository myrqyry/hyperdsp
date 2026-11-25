#pragma once
#include "parameter_schema.hpp"
#include "fractional_delay.hpp"
#include "engines/mic_dsp_engine.hpp"
#include "ladspa_engine.hpp"
#include <memory>
#include <variant>
#include <functional>
#include <charconv>
namespace hyperdsp {
struct GraphEdge {int source=-1;double weight=1;};
struct GraphNode {std::string effect;Parameters params;std::vector<GraphEdge> inputs;};
struct GraphSpec {std::vector<GraphNode> nodes;int output=-1;};
class PreparedGraph {
    static constexpr unsigned chunk=256;
    struct Edge {int source;float weight;unsigned delay;FractionalDelay line;};
    struct MicNode {
        std::array<stts::MicDspEngine,8> engines;
        void reset(unsigned rate,unsigned count,int mode,MicSettings s){
            for(unsigned c=0;c<count;++c){
                engines[c].configure(stts::MicDspEngine::Profile(mode),rate);
                engines[c].set_parameters(s.vibe,s.resonance);
            }
        }
        void process(float*const* p,unsigned channels,unsigned frames){
            for(unsigned c=0;c<channels;++c)engines[c].process(p[c],frames);
        }
    };
    struct Node {
        std::variant<float,std::unique_ptr<Color>,std::unique_ptr<Spectral>,std::unique_ptr<NativeFilter>,std::unique_ptr<Dynamics>,std::unique_ptr<TimeEffect>,std::unique_ptr<Space>,std::unique_ptr<Formant>,std::unique_ptr<MicNode>,std::unique_ptr<LadspaEngine>> engine{1.f};
        std::vector<Edge> inputs;
        std::vector<float> audio;
        unsigned latency=0;
    };
    std::vector<Node> nodes_;
    std::vector<unsigned> order_;
    unsigned channels_=0,latency_=0;
    int output_=-1;
    bool unknown_latency_=false;
    static bool mode(std::string_view id,std::string_view prefix,int& value,int maximum=5){
        if(!id.starts_with(prefix))return false;
        id.remove_prefix(prefix.size());
        auto [end,ec]=std::from_chars(id.data(),id.data()+id.size(),value);
        return id.size()==1&&ec==std::errc{}&&end==id.data()+id.size()&&value>=1&&value<=maximum;
    }
public:
    unsigned latency()const{return latency_;}
    bool latency_known()const{return !unknown_latency_;}
    static std::unique_ptr<PreparedGraph> prepare(const GraphSpec& s,unsigned rate,unsigned channels,std::string& error,const std::vector<std::string>& allowed={}){
        error.clear();
        auto fail=[&](std::string why)->std::unique_ptr<PreparedGraph>{error=std::move(why);return nullptr;};
        if(rate<8000||rate>192000||channels<1||channels>8)return fail("Unsupported audio format");
        if(s.nodes.size()>16)return fail("At most 16 nodes");
        const int count=int(s.nodes.size());
        if(s.output<-1||s.output>=count||(count&&s.output<0))return fail("Invalid output");
        auto g=std::make_unique<PreparedGraph>();g->channels_=channels;g->output_=s.output;g->nodes_.resize(count);
        unsigned edges=0;
        for(int i=0;i<count;++i){
            auto& n=s.nodes[i];
            if(n.inputs.empty()||n.inputs.size()>8||n.params.size()>(n.effect.starts_with("ladspa:")?259:24))return fail("Invalid node input or parameter count");
            edges+=n.inputs.size();if(edges>32)return fail("At most 32 edges");
            for(auto& e:n.inputs)if(e.source<-1||e.source>=count||!std::isfinite(e.weight)||std::abs(e.weight)>4)return fail("Invalid edge");
        }
        std::vector<unsigned> marks(count);
        std::function<bool(int)> visit=[&](int i){
            if(i<0)return true;
            if(marks[i]==1)return false;
            if(marks[i]==2)return true;
            marks[i]=1;
            for(auto& e:s.nodes[i].inputs)if(!visit(e.source))return false;
            marks[i]=2;g->order_.push_back(i);return true;
        };
        if(!visit(s.output))return fail("Graph contains a cycle");
        if(g->order_.size()!=s.nodes.size())return fail("Every node must contribute to output");
        for(auto i:g->order_){
            auto& n=g->nodes_[i];auto& spec=s.nodes[i];
            unsigned input_latency=0,own_latency=0;
            for(auto& e:spec.inputs)if(e.source>=0)input_latency=std::max(input_latency,g->nodes_[e.source].latency);
            int m=0;
            if(spec.effect=="gain"){
                float db=0;
                for(auto& [k,v]:spec.params){if(k!=gain_key||!std::isfinite(v)||v<gain_min||v>gain_max)return fail("Invalid gain parameter");db=float(v);}
                n.engine=std::pow(10.f,db/20);
            }else if(mode(spec.effect,"native:hyperdsp_color_filter:",m)){
                ColorSettings settings;settings.mode=ColorMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="oversample"){
                        if(!valid_oversampling(v))return fail("Invalid oversampling factor");
                        settings.oversample=unsigned(v);
                    }else if(!assign_parameter(settings,color_parameters,k,v))return fail("Invalid Color parameter: "+k);
                }
                auto engine=std::make_unique<Color>();engine->reset(rate,settings);own_latency=engine->latency_samples();n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_spectral_filter:",m)){
                SpectralSettings settings;settings.mode=SpectralMode(m);
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,spectral_parameters,k,v))return fail("Invalid Spectral parameter: "+k);
                auto engine=std::make_unique<Spectral>();engine->reset(rate,channels,settings);own_latency=engine->latency_samples();n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:mic_modeler_filter:",m,6)){
                MicSettings settings;
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,mic_parameters,k,v))return fail("Invalid Microphone parameter: "+k);
                auto engine=std::make_unique<MicNode>();engine->reset(rate,channels,m,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_native_filter:",m,9)){
                FilterSettings settings;settings.mode=FilterMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="stages"){if(!valid_choice(v,filter_stages))return fail("Invalid filter stages");settings.stages=unsigned(v);}
                    else if(k=="alignment"){if(!valid_choice(v,alignment_choices))return fail("Invalid filter alignment");settings.alignment=FilterAlignment(int(v));}
                    else if(!assign_parameter(settings,native_parameters,k,v))return fail("Invalid Filter parameter: "+k);
                }
                auto engine=std::make_unique<NativeFilter>();engine->reset(rate,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_dynamics_filter:",m,4)){
                DynamicsSettings settings;settings.mode=DynamicsMode(m);
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,dynamics_parameters,k,v))return fail("Invalid Dynamics parameter: "+k);
                auto engine=std::make_unique<Dynamics>();engine->reset(rate,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_time_filter:",m)){
                TimeSettings settings;settings.mode=TimeMode(m);
                if(m>=3)settings.delay_ms=short_delay_default(m);
                for(auto& [k,v]:spec.params){
                    if(k=="delay_ms"&&m>=3&&(v<.1||v>short_delay_max(m)))return fail("Invalid short-delay center");
                    if(!assign_parameter(settings,time_parameters,k,v))return fail("Invalid Time parameter: "+k);
                }
                auto engine=std::make_unique<TimeEffect>();engine->reset(rate,channels,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_space_filter:",m,2)){
                SpaceSettings settings;settings.mode=SpaceMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="stages"){if(!valid_choice(v,space_stages))return fail("Invalid phaser stages");settings.stages=unsigned(v);}
                    else if(!assign_parameter(settings,space_parameters,k,v))return fail("Invalid Space parameter: "+k);
                }
                auto engine=std::make_unique<Space>();engine->reset(rate,channels,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_formant_filter:",m,2)){
                FormantSettings settings;settings.mode=FormantMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="from"||k=="to"){if(!valid_choice(v,vowel_choices))return fail("Invalid vowel");(k=="from"?settings.from:settings.to)=unsigned(v);}
                    else if(!assign_parameter(settings,formant_parameters,k,v))return fail("Invalid Formant parameter: "+k);
                }
                auto engine=std::make_unique<Formant>();engine->reset(rate,settings);n.engine=std::move(engine);
            }else if(spec.effect.starts_with("ladspa:")){
                if(std::find(allowed.begin(),allowed.end(),spec.effect)==allowed.end())
                    return fail("LADSPA effect not in the local scanned catalog");
                auto id=spec.effect.substr(7);auto sep=id.find('\t');
                if(sep==std::string::npos||id.find('\t',sep+1)!=std::string::npos)return fail("Invalid LADSPA identity");
                auto engine=std::make_unique<LadspaEngine>();
                std::string parameter_error;
                if(!engine->open(id.substr(0,sep),id.substr(sep+1),rate,channels,[&](auto& e){
                    for(auto& c:e.controls)e.set_control(c,c.integer||c.toggle?std::clamp(std::round(c.initial),c.min,c.max):c.initial);
                    double wet=1,input=0,output=0;
                    for(auto& [k,v]:spec.params){
                        if(k=="mix"||k=="input_db"||k=="output_db"){
                            double lo=k=="mix"?0:-24,hi=k=="mix"?1:24;
                            if(!std::isfinite(v)||v<lo||v>hi){parameter_error=k;return false;}
                            (k=="mix"?wet:k=="input_db"?input:output)=v;
                        }else{
                            auto c=std::find_if(e.controls.begin(),e.controls.end(),[&](auto& c){return key(c.port)==k;});
                            if(c==e.controls.end()||!std::isfinite(v)||v<c->min||v>c->max||
                               ((c->integer||c->toggle)&&std::round(v)!=v)){parameter_error=k;return false;}
                            e.set_control(*c,float(v));
                        }
                    }
                    e.modulation.reset(rate,MixSettings::from(wet,input,output));return true;
                }))return fail(parameter_error.empty()?"Cannot instantiate compatible LADSPA effect":"Invalid LADSPA parameter: "+parameter_error);
                n.engine=std::move(engine);
                g->unknown_latency_=true;
                // LADSPA has no universal latency contract. Do not invent latency.
            }else return fail("Unsupported graph effect: "+spec.effect);
            n.latency=input_latency+own_latency;
            n.audio.resize(size_t(channels)*chunk);
            for(auto& e:spec.inputs){
                unsigned delay=input_latency-(e.source<0?0:g->nodes_[e.source].latency);
                Edge edge{e.source,float(e.weight),delay,{}};
                if(delay)edge.line.prepare_samples(delay,channels);
                n.inputs.push_back(std::move(edge));
            }
        }
        if(s.output>=0)g->latency_=g->nodes_[s.output].latency;
        return g;
    }
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!p||channels!=channels_||output_<0)return;
        for(unsigned offset=0;offset<frames;){
            unsigned size=std::min(chunk,frames-offset);
            for(auto index:order_){
                auto& node=nodes_[index];std::fill(node.audio.begin(),node.audio.end(),0);
                for(auto& edge:node.inputs){
                    for(unsigned j=0;j<size;++j){
                        for(unsigned c=0;c<channels;++c){
                            float x=edge.source<0?(p[c]?p[c][offset+j]:0):nodes_[edge.source].audio[c*chunk+j];
                            if(!std::isfinite(x))x=0;
                            if(edge.delay){float delayed=edge.line.read(c,float(edge.delay));edge.line.write(c,x);x=delayed;}
                            node.audio[c*chunk+j]+=edge.weight*x;
                        }
                        if(edge.delay)edge.line.advance();
                    }
                }
                for(unsigned c=0;c<channels;++c)for(unsigned j=0;j<size;++j)
                    if(!std::isfinite(node.audio[c*chunk+j]))node.audio[c*chunk+j]=0;
                std::array<float*,8> ptr{};
                for(unsigned c=0;c<channels;++c)ptr[c]=node.audio.data()+c*chunk;
                std::visit([&](auto& engine){
                    using T=std::decay_t<decltype(engine)>;
                    if constexpr(std::is_same_v<T,float>){
                        for(unsigned c=0;c<channels;++c)for(unsigned j=0;j<size;++j)ptr[c][j]*=engine;
                    }else engine->process(ptr.data(),channels,size);
                },node.engine);
                for(unsigned c=0;c<channels;++c)for(unsigned j=0;j<size;++j)
                    if(!std::isfinite(ptr[c][j]))ptr[c][j]=0;
            }
            for(unsigned c=0;c<channels;++c)if(p[c])
                std::copy_n(nodes_[output_].audio.data()+c*chunk,size,p[c]+offset);
            offset+=size;
        }
    }
};
class GraphRuntime {
    std::unique_ptr<PreparedGraph> active_;
public:
    bool replace(const GraphSpec& s,unsigned rate,unsigned channels,std::string& error,const std::vector<std::string>& allowed={}){
        auto next=PreparedGraph::prepare(s,rate,channels,error,allowed);
        if(!next)return false;
        active_=std::move(next);return true;
    }
    unsigned latency()const{return active_?active_->latency():0;}
    void process(float*const* p,unsigned channels,unsigned frames){if(active_)active_->process(p,channels,frames);}
};
}
