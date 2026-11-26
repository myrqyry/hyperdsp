#pragma once
#include "parameter_schema.hpp"
#include "fractional_delay.hpp"
#include "engines/mic_dsp_engine.hpp"
#include "ladspa_engine.hpp"
#include <memory>
#include <variant>
#include <functional>
#include <charconv>
#include <set>
namespace hyperdsp {
struct GraphEdge {int source=-1;double weight=1;};
struct GraphNode {std::string effect;Parameters params;std::vector<GraphEdge> inputs;};
struct GraphBinding {int node;std::string parameter;double low,high;};
struct GraphControl {std::string id,name;double initial=0;std::vector<GraphBinding> bindings;};
struct GraphRoute {std::string control;RouteConfig config;std::string channel,category;};
struct GraphSpec {std::vector<GraphNode> nodes;int output=-1;std::vector<GraphControl> controls;std::vector<GraphRoute> routes;};
inline bool graph_control_id(std::string_view id){
    if(id.empty()||id.size()>32)return false;
    for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
class PreparedGraph {
    static constexpr unsigned chunk=256;
    struct Edge {int source;float weight;unsigned delay;FractionalDelay line;};
    struct MicNode {
        std::array<stts::MicDspEngine,8> engines;
        void reset(unsigned rate,unsigned count,int mode,MicSettings s){
            count_=count;
            for(unsigned c=0;c<count;++c){
                engines[c].configure(stts::MicDspEngine::Profile(mode),rate);
                engines[c].set_parameters(s.vibe,s.resonance);
            }
        }
        unsigned count_=0;
        void set(MicSettings s){for(unsigned c=0;c<count_;++c)engines[c].set_parameters(s.vibe,s.resonance);}
        void process(float*const* p,unsigned channels,unsigned frames){
            for(unsigned c=0;c<channels;++c)engines[c].process(p[c],frames);
        }
    };
    static constexpr unsigned max_controls=8;
    using Values=std::array<double,max_controls>;
    struct Mapping {unsigned control;std::string key;double lo,hi;double value(const Values& v)const{return std::lerp(lo,hi,v[control]);}};
    struct GainNode {
        LinearRamp gain;
        unsigned ramp=480;
        void set(float db){gain.set(std::pow(10.f,db/20),ramp);}
        void process(float*const* p,unsigned channels,unsigned frames){
            for(unsigned j=0;j<frames;++j){float v=gain.next();for(unsigned c=0;c<channels;++c)p[c][j]*=v;}
        }
    };
    struct Node {
        std::variant<GainNode,std::unique_ptr<Color>,std::unique_ptr<Spectral>,std::unique_ptr<NativeFilter>,std::unique_ptr<Dynamics>,std::unique_ptr<TimeEffect>,std::unique_ptr<Space>,std::unique_ptr<Formant>,std::unique_ptr<MicNode>,std::unique_ptr<LadspaEngine>> engine{GainNode{}};
        std::function<void(const Values&)> apply;
        std::vector<Edge> inputs;
        std::vector<float> audio;
        unsigned latency=0;
    };
    std::vector<Node> nodes_;
    std::vector<unsigned> order_;
    unsigned channels_=0,latency_=0;
    int output_=-1;
    bool unknown_latency_=false;
    std::vector<GraphControl> controls_;
    Values values_{},effective_{},offsets_{},sums_{};
    ModulationMatrix modulation_;
    static constexpr unsigned control_quantum=64;
    unsigned quantum_position_=0;
    bool reactive_=false;
    void apply_effective(){
        auto next=values_;
        for(unsigned i=0;i<controls_.size();++i)next[i]=std::clamp(next[i]+offsets_[i],0.,1.);
        if(next!=effective_){for(auto& n:nodes_)if(n.apply)n.apply(next);effective_=next;}
    }

    template<class S,class E,size_t N>
    static bool bind_native(Node& node,E* engine,S& settings,const FloatParameter<S>(&schema)[N],
        const std::vector<Mapping>& mappings,const Values& values,int short_mode=0){
        if(mappings.empty())return true;
        for(auto& m:mappings){
            auto temp=settings;
            if(!assign_parameter(temp,schema,m.key,m.lo)||!assign_parameter(temp,schema,m.key,m.hi))return false;
            if(short_mode>=3&&m.key=="delay_ms"&&(std::min(m.lo,m.hi)<.1||std::max(m.lo,m.hi)>short_delay_max(short_mode)))return false;
        }
        const auto base=settings;
        auto resolve=[base,mappings,&schema](const Values& v){
            auto out=base;
            for(auto& m:mappings)assign_parameter(out,schema,m.key,m.value(v));
            return out;
        };
        settings=resolve(values);
        node.apply=[engine,resolve](const Values& v){engine->set(resolve(v));};
        return true;
    }

    static bool mode(std::string_view id,std::string_view prefix,int& value,int maximum=5){
        if(!id.starts_with(prefix))return false;
        id.remove_prefix(prefix.size());
        auto [end,ec]=std::from_chars(id.data(),id.data()+id.size(),value);
        return id.size()==1&&ec==std::errc{}&&end==id.data()+id.size()&&value>=1&&value<=maximum;
    }
public:
    unsigned latency()const{return latency_;}
    bool latency_known()const{return !unknown_latency_;}
    static std::unique_ptr<PreparedGraph> prepare(const GraphSpec& s,unsigned rate,unsigned channels,std::string& error,const std::vector<std::string>& allowed={},const Parameters& overrides={}){
        error.clear();
        auto fail=[&](std::string why)->std::unique_ptr<PreparedGraph>{error=std::move(why);return nullptr;};
        if(rate<8000||rate>192000||channels<1||channels>8)return fail("Unsupported audio format");
        if(s.nodes.size()>16)return fail("At most 16 nodes");
        const int count=int(s.nodes.size());
        if(s.output<-1||s.output>=count||(count&&s.output<0))return fail("Invalid output");
        auto g=std::make_unique<PreparedGraph>();g->channels_=channels;g->output_=s.output;g->nodes_.resize(count);
        if(s.controls.size()>max_controls)return fail("At most 8 exposed controls");
        std::vector<std::vector<Mapping>> mappings(count);
        std::set<std::string> ids;
        std::set<std::pair<int,std::string>> targets;
        unsigned bindings=0;
        for(unsigned i=0;i<s.controls.size();++i){
            const auto& c=s.controls[i];
            if(!graph_control_id(c.id)||!ids.insert(c.id).second||c.name.empty()||c.name.size()>80||
               !std::isfinite(c.initial)||c.initial<0||c.initial>1||c.bindings.empty()||c.bindings.size()>16)
                return fail("Invalid exposed control");
            bindings+=c.bindings.size();if(bindings>32)return fail("At most 32 control bindings");
            g->values_[i]=c.initial;
            for(auto& b:c.bindings){
                if(b.node<0||b.node>=count||b.parameter.empty()||b.parameter.size()>64||
                   !std::isfinite(b.low)||!std::isfinite(b.high)||!targets.emplace(b.node,b.parameter).second)
                    return fail("Invalid or duplicate control binding");
                mappings[b.node].push_back({i,b.parameter,b.low,b.high});
            }
        }
        g->controls_=s.controls;
        if(!g->resolve_values(overrides,g->values_,error))return nullptr;
        g->effective_=g->values_;
        if(s.routes.size()>modulation_slots)return fail("At most 4 graph modulation routes");
        auto in_range=[](double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
        for(unsigned i=0;i<s.routes.size();++i){
            const auto& route=s.routes[i];auto c=route.config;
            auto it=std::find_if(s.controls.begin(),s.controls.end(),[&](auto& control){return control.id==route.control;});
            if(it==s.controls.end()||c.source<1||c.source>10||c.shape<0||c.shape>3||
               !in_range(c.amount,-1,1)||!in_range(c.hz,.05,20)||!in_range(c.attack,1,200)||
               !in_range(c.release,10,2000)||!in_range(c.sensitivity,-24,24)||
               route.channel.size()>160||route.category.size()>256||
               (c.source>=6&&route.channel.empty())||(c.source==6&&route.category.empty()))
                return fail("Invalid graph modulation route");
            c.destination=int(it-s.controls.begin());
            g->modulation_.configure(i,rate,c,true);g->reactive_=true;
        }

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
                if(!mappings[i].empty()){
                    auto b=mappings[i][0];
                    if(mappings[i].size()!=1||b.key!=gain_key||std::min(b.lo,b.hi)<gain_min||std::max(b.lo,b.hi)>gain_max)return fail("Invalid gain binding");
                    db=float(b.value(g->values_));
                    n.apply=[&n,b](const Values& v){std::get<GainNode>(n.engine).set(float(b.value(v)));};
                }
                auto& gain=std::get<GainNode>(n.engine);
                gain.ramp=std::max(1u,rate/100);gain.gain.reset(std::pow(10.f,db/20));
            }else if(mode(spec.effect,"native:hyperdsp_color_filter:",m)){
                ColorSettings settings;settings.mode=ColorMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="oversample"){
                        if(!valid_oversampling(v))return fail("Invalid oversampling factor");
                        settings.oversample=unsigned(v);
                    }else if(!assign_parameter(settings,color_parameters,k,v))return fail("Invalid Color parameter: "+k);
                }
                auto engine=std::make_unique<Color>();
                if(!bind_native(n,engine.get(),settings,color_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,settings);own_latency=engine->latency_samples();n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_spectral_filter:",m)){
                SpectralSettings settings;settings.mode=SpectralMode(m);
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,spectral_parameters,k,v))return fail("Invalid Spectral parameter: "+k);
                auto engine=std::make_unique<Spectral>();
                if(!bind_native(n,engine.get(),settings,spectral_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,channels,settings);own_latency=engine->latency_samples();n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:mic_modeler_filter:",m,6)){
                MicSettings settings;
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,mic_parameters,k,v))return fail("Invalid Microphone parameter: "+k);
                auto engine=std::make_unique<MicNode>();
                if(!bind_native(n,engine.get(),settings,mic_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,channels,m,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_native_filter:",m,9)){
                FilterSettings settings;settings.mode=FilterMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="stages"){if(!valid_choice(v,filter_stages))return fail("Invalid filter stages");settings.stages=unsigned(v);}
                    else if(k=="alignment"){if(!valid_choice(v,alignment_choices))return fail("Invalid filter alignment");settings.alignment=FilterAlignment(int(v));}
                    else if(!assign_parameter(settings,native_parameters,k,v))return fail("Invalid Filter parameter: "+k);
                }
                auto engine=std::make_unique<NativeFilter>();
                if(!bind_native(n,engine.get(),settings,native_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_dynamics_filter:",m,4)){
                DynamicsSettings settings;settings.mode=DynamicsMode(m);
                for(auto& [k,v]:spec.params)if(!assign_parameter(settings,dynamics_parameters,k,v))return fail("Invalid Dynamics parameter: "+k);
                auto engine=std::make_unique<Dynamics>();
                if(!bind_native(n,engine.get(),settings,dynamics_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_time_filter:",m)){
                TimeSettings settings;settings.mode=TimeMode(m);
                if(m>=3)settings.delay_ms=short_delay_default(m);
                for(auto& [k,v]:spec.params){
                    if(k=="delay_ms"&&m>=3&&(v<.1||v>short_delay_max(m)))return fail("Invalid short-delay center");
                    if(!assign_parameter(settings,time_parameters,k,v))return fail("Invalid Time parameter: "+k);
                }
                auto engine=std::make_unique<TimeEffect>();
                if(!bind_native(n,engine.get(),settings,time_parameters,mappings[i],g->values_,m))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,channels,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_space_filter:",m,2)){
                SpaceSettings settings;settings.mode=SpaceMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="stages"){if(!valid_choice(v,space_stages))return fail("Invalid phaser stages");settings.stages=unsigned(v);}
                    else if(!assign_parameter(settings,space_parameters,k,v))return fail("Invalid Space parameter: "+k);
                }
                auto engine=std::make_unique<Space>();
                if(!bind_native(n,engine.get(),settings,space_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,channels,settings);n.engine=std::move(engine);
            }else if(mode(spec.effect,"native:hyperdsp_formant_filter:",m,2)){
                FormantSettings settings;settings.mode=FormantMode(m);
                for(auto& [k,v]:spec.params){
                    if(k=="from"||k=="to"){if(!valid_choice(v,vowel_choices))return fail("Invalid vowel");(k=="from"?settings.from:settings.to)=unsigned(v);}
                    else if(!assign_parameter(settings,formant_parameters,k,v))return fail("Invalid Formant parameter: "+k);
                }
                auto engine=std::make_unique<Formant>();
                if(!bind_native(n,engine.get(),settings,formant_parameters,mappings[i],g->values_))return fail("Invalid continuous parameter binding: "+spec.effect);
                engine->reset(rate,settings);n.engine=std::move(engine);
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
                    struct PortBinding {Mapping map;const Control* port;};
                    std::vector<PortBinding> ports;
                    for(auto& b:mappings[i]){
                        if(b.key=="mix"||b.key=="input_db"||b.key=="output_db"){
                            double lo=b.key=="mix"?0:-24,hi=b.key=="mix"?1:24;
                            if(std::min(b.lo,b.hi)<lo||std::max(b.lo,b.hi)>hi){parameter_error=b.key;return false;}
                            ports.push_back({b,nullptr});
                        }else{
                            auto c=std::find_if(e.controls.begin(),e.controls.end(),[&](auto& c){return key(c.port)==b.key;});
                            if(c==e.controls.end()||c->integer||c->toggle||std::min(b.lo,b.hi)<c->min||std::max(b.lo,b.hi)>c->max){parameter_error=b.key;return false;}
                            ports.push_back({b,&*c});
                        }
                    }
                    auto apply=[engine=&e,ports,wet,input,output](const Values& v){
                        double w=wet,in=input,out=output;
                        for(auto& b:ports){
                            double value=b.map.value(v);
                            if(b.port)engine->set_control(*b.port,float(value));
                            else (b.map.key=="mix"?w:b.map.key=="input_db"?in:out)=value;
                        }
                        return MixSettings::from(w,in,out);
                    };
                    e.modulation.reset(rate,apply(g->values_));
                    if(!ports.empty())n.apply=[engine=&e,apply](const Values& v){engine->modulation.set(apply(v),{});};
                    return true;
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
    bool resolve_values(const Parameters& updates,Values& target,std::string& error)const{
        for(auto& [id,value]:updates){
            auto it=std::find_if(controls_.begin(),controls_.end(),[&](auto& c){return c.id==id;});
            if(it==controls_.end()||!std::isfinite(value)||value<0||value>1){error="Invalid exposed control value: "+id;return false;}
            target[it-controls_.begin()]=value;
        }
        return true;
    }
    const std::vector<GraphControl>& controls()const{return controls_;}
    double base_value(unsigned i)const{return i<controls_.size()?values_[i]:0;}
    double effective_value(unsigned i)const{return i<controls_.size()?effective_[i]:0;}
    Parameters base_values()const{
        Parameters result;
        for(unsigned i=0;i<controls_.size();++i)result.emplace(controls_[i].id,values_[i]);
        return result;
    }
    bool set_controls(const Parameters& updates,std::string& error){
        error.clear();auto next=values_;
        if(!resolve_values(updates,next,error))return false;
        if(next!=values_){values_=next;apply_effective();}
        return true;
    }
    void external_values(const std::array<float,modulation_slots>& values){modulation_.external_values(values);}
    void process(float*const* p,unsigned channels,unsigned frames){
        if(!p||channels!=channels_||output_<0)return;
        for(unsigned offset=0;offset<frames;){
            unsigned size=std::min(chunk,frames-offset);
            if(reactive_)size=std::min(size,control_quantum-quantum_position_);
            if(reactive_){
                for(unsigned j=0;j<size;++j){
                    std::array<float,8> samples{};
                    for(unsigned c=0;c<channels;++c)if(p[c]&&std::isfinite(p[c][offset+j]))samples[c]=p[c][offset+j];
                    auto frame=modulation_.next(samples.data(),channels);
                    for(auto& e:frame.entries)if(e.destination>=0)sums_[e.destination]+=e.value;
                }
            }

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
                    if constexpr(std::is_same_v<T,GainNode>)engine.process(ptr.data(),channels,size);
                    else engine->process(ptr.data(),channels,size);
                },node.engine);
                for(unsigned c=0;c<channels;++c)for(unsigned j=0;j<size;++j)
                    if(!std::isfinite(ptr[c][j]))ptr[c][j]=0;
            }
            for(unsigned c=0;c<channels;++c)if(p[c])
                std::copy_n(nodes_[output_].audio.data()+c*chunk,size,p[c]+offset);
            offset+=size;
            if(reactive_){
                quantum_position_+=size;
                if(quantum_position_==control_quantum){
                    for(unsigned i=0;i<controls_.size();++i)offsets_[i]=sums_[i]/control_quantum;
                    sums_={};quantum_position_=0;apply_effective();
                }
            }
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
