#include "StdInc.h"
#include "OwnedBonusLogicalDomain.h"
#include "BonusParameters.h"
#include "OwnedBonusValueCopier.h"
namespace
{
struct Namespace {};
struct Key
{
 std::shared_ptr<const Namespace> space;
 size_t ordinal;
 bool operator<(const Key & other) const
 {
  if(space.owner_before(other.space))return true;
  if(other.space.owner_before(space))return false;
  return ordinal<other.ordinal;
 }
};
template<class P> struct PointerOrder
{
 bool operator()(const P&a,const P&b)const
 {if(a.owner_before(b))return true;if(b.owner_before(a))return false;return std::less<decltype(a.get())>{}(a.get(),b.get());}
};
using BonusPtr=std::shared_ptr<const Bonus>;
using ParamPtr=std::shared_ptr<const BonusParameters>;
using BonusIndex=std::map<BonusPtr,Key,PointerOrder<BonusPtr>>;
using ParamIndex=std::map<ParamPtr,Key,PointerOrder<ParamPtr>>;
struct ReadParameters
{
 std::function<void(const BonusPtr&)> read;
 void operator&(const BonusParameters::storage_type & data)
 {
  if(const auto * combat=std::get_if<BonusParametersOnCombatEvent>(&data))
   for(const auto & effect:combat->effects)
    if(const auto * b=std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))read(b->bonus);
 }
};
}
struct OwnedBonusValueId::Identity
{
 std::shared_ptr<const Namespace> family;
 Key key;
};
struct OwnedLogicalBonusEvent::Storage
{
 std::shared_ptr<const Namespace> family;
 std::shared_ptr<const Namespace> branch;
 std::vector<BonusPtr> roots;
 std::vector<std::optional<Key>> parameterIds;
};
struct OwnedBonusLogicalDomain::State
{
 struct Data
 {
  OwnedBonusEventDomain certificate;
  std::shared_ptr<const BonusGraphSnapshot::CaptureIdentity> graphOrigin;
  std::shared_ptr<const Namespace> family=std::make_shared<const Namespace>();
  std::shared_ptr<const Namespace> branch=std::make_shared<const Namespace>();
  std::set<std::shared_ptr<const Namespace>,std::owner_less<std::shared_ptr<const Namespace>>> ancestors;
  size_t nextOrdinal=0;
  std::vector<Key> captured;
  std::map<Key,BonusPtr> bonuses;
  std::map<Key,ParamPtr> parameters;
  BonusIndex currentBonusIds;
  ParamIndex currentParamIds;
  ParamIndex initialParamIds;
  explicit Data(OwnedBonusEventDomain c):certificate(std::move(c)){}
  Key allocate()
  {
   if(nextOrdinal==std::numeric_limits<size_t>::max())throw std::overflow_error("Owned logical ordinal exhausted");
   return {branch,nextOrdinal++};
  }
  Key index(const BonusPtr & bonus)
  {
   if(!bonus)throw std::invalid_argument("Null logical Bonus");
   if(auto it=currentBonusIds.find(bonus);it!=currentBonusIds.end())return it->second;
   auto id=allocate();currentBonusIds.emplace(bonus,id);bonuses.emplace(id,bonus);
   if(bonus->parameters && !currentParamIds.contains(bonus->parameters))
   {
    auto pid=allocate();currentParamIds.emplace(bonus->parameters,pid);parameters.emplace(pid,bonus->parameters);
    auto copy=*bonus->parameters;ReadParameters visitor{[&](const BonusPtr & nested){if(nested)index(nested);}};copy.serialize(visitor);
   }
   return id;
  }
 };
 std::shared_ptr<Data> data;
 explicit State(OwnedBonusEventDomain certificate) : data(std::make_shared<Data>(std::move(certificate))) {}
 explicit State(std::shared_ptr<Data> shared) : data(std::move(shared)) {}
 State(const State & other) : data(std::make_shared<Data>(*other.data)) {}
};
OwnedBonusValueId::OwnedBonusValueId(std::shared_ptr<const Identity> value):identity(std::move(value)){}
OwnedLogicalBonusEvent::OwnedLogicalBonusEvent(std::shared_ptr<const Storage> value):storage(std::move(value)){}
OwnedBonusLogicalDomain::OwnedBonusLogicalDomain(std::unique_ptr<State> value):state(std::move(value)){}
OwnedBonusLogicalDomain::~OwnedBonusLogicalDomain()=default;
OwnedBonusLogicalDomain::OwnedBonusLogicalDomain(OwnedBonusLogicalDomain&&)noexcept=default;
OwnedBonusLogicalDomain & OwnedBonusLogicalDomain::operator=(OwnedBonusLogicalDomain&&)noexcept=default;
OwnedBonusLogicalDomain OwnedBonusLogicalDomain::capture(const BonusGraphSnapshot & graph)
{
 auto s=std::make_unique<State>(OwnedBonusEventDomain::capture(graph));
 s->data->graphOrigin=graph.capturedIdentity;
 for(const auto & b:graph.bonuses())s->data->captured.push_back(s->data->index(b));
 s->data->initialParamIds=s->data->currentParamIds;
 for(const auto & b:graph.capturedValues()) s->data->index(b);
 return OwnedBonusLogicalDomain(std::move(s));
}
OwnedBonusLogicalDomain OwnedBonusLogicalDomain::fork()const
{
 if(!state)throw std::logic_error("Moved logical domain");
 auto s=std::make_unique<State>(*state);s->data->certificate=state->data->certificate.fork();s->data->ancestors.insert(state->data->branch);s->data->branch=std::make_shared<const Namespace>();s->data->nextOrdinal=0;return OwnedBonusLogicalDomain(std::move(s));
}
OwnedBonusValueId OwnedBonusLogicalDomain::capturedValue(BonusGraphSnapshot::BonusId id)const
{
 if(!state)throw std::logic_error("Moved logical domain");
 return OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{state->data->family,state->data->captured.at(id)}));
}
std::shared_ptr<const Bonus> OwnedBonusLogicalDomain::value(const OwnedBonusValueId & id)const
{
 if(!state||!id.identity||id.identity->family!=state->data->family)throw std::invalid_argument("Foreign logical value");
 return state->data->bonuses.at(id.identity->key);
}
OwnedBonusValueId OwnedBonusLogicalDomain::nestedValue(const OwnedBonusValueId & parent,size_t effectIndex)const
{
 const auto b=value(parent);if(!b->parameters)throw std::invalid_argument("No nested parameters");
 const auto & effect=b->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(effectIndex);
 const auto & nested=std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(effect).bonus;
 return OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{state->data->family,state->data->currentBonusIds.at(nested)}));
}
OwnedBonusValueId OwnedBonusLogicalDomain::defineScalar(const Bonus & input)
{
 if(!state)throw std::logic_error("Moved logical domain");
 if(typeid(input)!=typeid(Bonus)||input.updater||input.propagationUpdater||input.limiter||input.propagator||input.parameters)throw std::invalid_argument("Only component-free scalar definitions are supported");
 auto trial=std::make_unique<State>(*state);auto id=trial->data->index(std::make_shared<const Bonus>(input));
 auto result=OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{trial->data->family,id}));state=std::move(trial);return result;
}
void OwnedBonusLogicalDomain::setTurnsRemain(const OwnedBonusValueId & id,int turns)
{
 auto original=value(id);OwnedBonusValueCopier copier;auto trial=std::make_unique<State>(*state);
 for(auto & [key,b]:trial->data->bonuses)b=copier.copy(b);
 for(auto & [key,p]:trial->data->parameters){auto replacement=copier.copiedParameters(p.get());if(!replacement)throw std::logic_error("Unreachable logical parameter");p=std::move(replacement);}
 copier.copy(original)->turnsRemain=turns;
 trial->data->currentBonusIds.clear();trial->data->currentParamIds.clear();
 for(const auto & [key,b]:trial->data->bonuses)trial->data->currentBonusIds.emplace(b,key);
 for(const auto & [key,p]:trial->data->parameters)trial->data->currentParamIds.emplace(p,key);
 state=std::move(trial);
}
OwnedLogicalBonusEvent OwnedBonusLogicalDomain::fromValues(const std::vector<OwnedBonusValueId> & ids)const
{
 if(!state)throw std::logic_error("Moved logical domain");
 auto result=std::make_shared<OwnedLogicalBonusEvent::Storage>();result->family=state->data->family;result->branch=state->data->branch;
 for(const auto & id:ids)
 {
  auto b=value(id);result->roots.push_back(std::make_shared<const Bonus>(*b));
  result->parameterIds.push_back(b->parameters?std::optional<Key>(state->data->currentParamIds.at(b->parameters)):std::nullopt);
 }
 return OwnedLogicalBonusEvent(std::move(result));
}
OwnedLogicalBonusEvent OwnedBonusLogicalDomain::makeEvent(const std::vector<Bonus> & values)const
{
 if(!state)throw std::logic_error("Moved logical domain");
 auto certified=state->data->certificate.makeEvent(values);
 auto result=std::make_shared<OwnedLogicalBonusEvent::Storage>();result->family=state->data->family;result->branch=state->data->branch;result->roots=certified.values();
 for(const auto & b:result->roots)result->parameterIds.push_back(b->parameters?std::optional<Key>(state->data->initialParamIds.at(b->parameters)):std::nullopt);
 return OwnedLogicalBonusEvent(std::move(result));
}
OwnedLogicalBonusEvent OwnedBonusLogicalDomain::makeObservedEvent(const std::vector<Bonus> & values, const std::vector<std::shared_ptr<const Bonus>> & observations)
{
 if(!state) throw std::logic_error("Moved logical domain");
 const auto certified = state->data->certificate.makeObservedEvent(values, observations);
 auto trial = std::make_unique<State>(*state);
 auto result = std::make_shared<OwnedLogicalBonusEvent::Storage>();
 result->family = state->data->family;
 result->branch = state->data->branch;
 result->roots = certified.values();
 for(const auto & value : result->roots)
 {
  trial->data->index(value);
  result->parameterIds.push_back(value->parameters ? std::optional<Key>(trial->data->currentParamIds.at(value->parameters)) : std::nullopt);
 }
 state = std::move(trial);
 return OwnedLogicalBonusEvent(std::move(result));
}
bool OwnedBonusLogicalDomain::accepts(const OwnedLogicalBonusEvent & event)const
{
 return state&&event.storage&&event.storage->family==state->data->family&&(event.storage->branch==state->data->branch||state->data->ancestors.contains(event.storage->branch));
}
std::vector<BonusPtr> OwnedBonusLogicalDomain::resolve(const OwnedLogicalBonusEvent & event)const
{
 if(!accepts(event))throw std::invalid_argument("Foreign or non-ancestor logical event");
 std::vector<BonusPtr> result;
 for(size_t i=0;i<event.storage->roots.size();++i)
 {
  auto root=std::make_shared<Bonus>(*event.storage->roots[i]);
  if(const auto & id=event.storage->parameterIds[i])root->parameters=state->data->parameters.at(*id);
  result.push_back(std::move(root));
 }
 return result;
}
size_t OwnedBonusLogicalDomain::retainedBonusCount()const{if(!state)throw std::logic_error("Moved logical domain");return state->data->bonuses.size();}
size_t OwnedBonusLogicalDomain::retainedParameterCount()const{if(!state)throw std::logic_error("Moved logical domain");return state->data->parameters.size();}

OwnedBonusLogicalDomain OwnedBonusLogicalDomain::transactionCopy() const
{
 if(!state)throw std::logic_error("Moved logical domain");
 return OwnedBonusLogicalDomain(std::make_unique<State>(state->data));
}
std::vector<std::shared_ptr<const Bonus>> OwnedBonusLogicalDomain::graphValues(const BonusGraphSnapshot & graph) const
{
 if(!state||graph.capturedIdentity!=state->data->graphOrigin||graph.bonuses().size()!=state->data->captured.size())throw std::invalid_argument("Graph/value binding differs");
 std::vector<std::shared_ptr<const Bonus>> result;result.reserve(state->data->captured.size());
 for(const auto & id:state->data->captured)result.push_back(state->data->bonuses.at(id));
 return result;
}
void OwnedBonusLogicalDomain::adoptAppendedGraph(const BonusGraphSnapshot & graph)
{
 if(!state||graph.capturedIdentity!=state->data->graphOrigin||graph.bonuses().size()<state->data->captured.size())throw std::invalid_argument("Foreign or truncated graph values");
 for(size_t i=0;i<state->data->captured.size();++i)
  if(graph.bonuses()[i]!=state->data->bonuses.at(state->data->captured[i]))throw std::invalid_argument("Existing graph value identity changed outside owner");
 if(graph.bonuses().size() == state->data->captured.size()) return;
 auto trial=std::make_unique<State>(*state);
 for(size_t i=trial->data->captured.size();i<graph.bonuses().size();++i)trial->data->captured.push_back(trial->data->index(graph.bonuses()[i]));
 state=std::move(trial);
}

std::vector<OwnedLogicalBonusEvent> OwnedBonusLogicalDomain::importComponentEvents(const std::vector<std::vector<Bonus>> & groups)
{
 if(!state) throw std::logic_error("Moved logical domain");
 std::vector<BonusPtr> input;
 for(const auto & group : groups)
  for(const auto & value : group) input.push_back(std::make_shared<const Bonus>(value));
 // The private producer boundary owns the input; the copier rejects unknown types and cycles.
 const auto copied = BonusGraphSnapshot::detachValues(input);
 auto trial = std::make_unique<State>(*state);
 std::vector<OwnedLogicalBonusEvent> result;
 result.reserve(groups.size());
 size_t offset = 0;
 for(const auto & group : groups)
 {
  auto storage = std::make_shared<OwnedLogicalBonusEvent::Storage>();
  storage->family = state->data->family;
  storage->branch = state->data->branch;
  for(size_t i = 0; i < group.size(); ++i)
  {
   const auto & value = copied.at(offset++);
   trial->data->index(value);
   storage->roots.push_back(value);
   storage->parameterIds.push_back(value->parameters
    ? std::optional<Key>(trial->data->currentParamIds.at(value->parameters)) : std::nullopt);
  }
  result.push_back(OwnedLogicalBonusEvent(std::move(storage)));
 }
 state = std::move(trial);
 return result;
}

OwnedBonusValueId OwnedBonusLogicalDomain::internOwned(const std::shared_ptr<const Bonus> & value)
{
 if(!state) throw std::logic_error("Moved logical domain");
 if(!value) throw std::invalid_argument("Null logical Bonus");
 if(auto found = state->data->currentBonusIds.find(value); found != state->data->currentBonusIds.end())
  return OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{state->data->family, found->second}));
 // Query transactions share the indexes until an updater creates a new value.
 if(state->data.use_count() != 1)
 {
  auto trial = std::make_unique<State>(*state);
  auto key = trial->data->index(value);
  auto result = OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{trial->data->family, key}));
  state = std::move(trial);
  return result;
 }
 auto key = state->data->index(value);
 return OwnedBonusValueId(std::make_shared<const OwnedBonusValueId::Identity>(OwnedBonusValueId::Identity{state->data->family,key}));
}
