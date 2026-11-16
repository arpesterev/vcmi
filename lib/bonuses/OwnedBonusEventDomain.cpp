/* Typed immutable bonus events. GPL-2.0-or-later. */
#include "StdInc.h"
#include "OwnedBonusEventDomain.h"
#include "BonusParameters.h"
#include "Updaters.h"
#include "Limiters.h"
#include "Propagators.h"

namespace
{
struct Identity {};
template<class Pointer> struct OwnedPointerOrder
{
 bool operator()(const Pointer & a, const Pointer & b) const
 {
  if(a.owner_before(b)) return true;
  if(b.owner_before(a)) return false;
  return std::less<decltype(a.get())>{}(a.get(), b.get());
 }
};
template<class Pointer> using OwnedPointers = std::set<Pointer, OwnedPointerOrder<Pointer>>;
struct Registry
{
 OwnedPointers<TUpdaterPtr> updaters;
 OwnedPointers<TLimiterPtr> limiters;
 OwnedPointers<TPropagatorPtr> propagators;
 OwnedPointers<TBonusParametersPtr> parameters;
 OwnedPointers<std::shared_ptr<const Bonus>> bonuses;
};
struct ParameterReader
{
 std::function<void(const std::shared_ptr<const Bonus> &)> read;
 void operator&(const BonusParameters::storage_type & value)
 {
  if(const auto * combat = std::get_if<BonusParametersOnCombatEvent>(&value))
   for(const auto & effect : combat->effects)
    if(const auto * bonus = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect)) read(bonus->bonus);
 }
};
struct SpecialtyReader
{
 std::function<void(const TUpdaterPtr &)> read;
 void operator&(const TimesHeroLevelUpdater &) {}
 void operator&(const std::shared_ptr<const DivideStackLevelUpdater> & value) { read(value); }
};
class RegistryBuilder
{
 Registry registry;
 void updater(const TUpdaterPtr & value)
 {
  if(!value || !registry.updaters.insert(value).second) return;
  if(const auto * composite = dynamic_cast<const CompositeUpdater *>(value.get()))
   for(const auto & child : composite->updaters) updater(child);
  if(const auto * specialty = dynamic_cast<const TimesHeroLevelDivideStackLevelUpdater *>(value.get()))
  {
   auto copy = *specialty;
   SpecialtyReader reader{[&](const TUpdaterPtr & child){updater(child);}};
   copy.serialize(reader);
  }
 }
 void limiter(const TLimiterPtr & value)
 {
  if(!value || !registry.limiters.insert(value).second) return;
  if(const auto * aggregate = dynamic_cast<const AggregateLimiter *>(value.get()))
   for(const auto & child : aggregate->limiters) limiter(child);
 }
public:
 void bonus(const std::shared_ptr<const Bonus> & value)
 {
  if(!value || !registry.bonuses.insert(value).second) return;
  updater(value->updater); updater(value->propagationUpdater); limiter(value->limiter);
  if(value->propagator) registry.propagators.insert(value->propagator);
  if(value->parameters && registry.parameters.insert(value->parameters).second)
  {
   auto copy = *value->parameters;
   ParameterReader reader{[&](const std::shared_ptr<const Bonus> & child){bonus(child);}};
   copy.serialize(reader);
  }
 }
 Registry finish() && { return std::move(registry); }
};
template<class Pointer> bool certified(const Pointer & value, const OwnedPointers<Pointer> & registry)
{ return !value || registry.contains(value); }
bool certified(const Bonus & value, const Registry & registry)
{
 return certified(value.updater, registry.updaters)
  && certified(value.propagationUpdater, registry.updaters)
  && certified(value.limiter, registry.limiters)
  && certified(value.propagator, registry.propagators)
  && certified(value.parameters, registry.parameters);
}
}

struct OwnedBonusEvent::Storage
{
 const std::shared_ptr<const Identity> family;
 const std::shared_ptr<const Identity> branch;
 const std::vector<std::shared_ptr<const Bonus>> values;
};
struct OwnedBonusEventDomain::State
{
 const std::shared_ptr<const Identity> family;
 const std::shared_ptr<const Identity> branch;
 const std::set<std::shared_ptr<const Identity>, std::owner_less<std::shared_ptr<const Identity>>> ancestors;
 const std::shared_ptr<const Registry> registry;
 const std::vector<std::shared_ptr<const Bonus>> captured;
};
OwnedBonusEvent::OwnedBonusEvent(std::shared_ptr<const Storage> value) : storage(std::move(value)) {}
const std::vector<std::shared_ptr<const Bonus>> & OwnedBonusEvent::values() const { if(!storage) throw std::logic_error("Empty moved event"); return storage->values; }
OwnedBonusEventDomain::OwnedBonusEventDomain(std::shared_ptr<const State> value) : state(std::move(value)) {}
OwnedBonusEventDomain OwnedBonusEventDomain::capture(const BonusGraphSnapshot & graph)
{
 if(!graph.sameOrigin(graph)) throw std::invalid_argument("Owned captured graph origin required");
 RegistryBuilder builder;
 for(const auto & bonus : graph.bonuses()) builder.bonus(bonus);
 auto family = std::make_shared<const Identity>();
 auto branch = std::make_shared<const Identity>();
 return OwnedBonusEventDomain(std::make_shared<const State>(State{family,branch,{},
  std::make_shared<const Registry>(std::move(builder).finish()),graph.bonuses()}));
}
OwnedBonusEventDomain OwnedBonusEventDomain::fork() const
{
 if(!state) throw std::logic_error("Empty moved event domain");
 auto ancestors = state->ancestors; ancestors.insert(state->branch);
 return OwnedBonusEventDomain(std::make_shared<const State>(State{state->family,
  std::make_shared<const Identity>(),std::move(ancestors),state->registry,state->captured}));
}
OwnedBonusEvent OwnedBonusEventDomain::makeEvent(const std::vector<Bonus> & input) const
{
 if(!state) throw std::logic_error("Empty moved event domain");
 std::vector<std::shared_ptr<const Bonus>> values; values.reserve(input.size());
 for(const auto & bonus : input)
 {
  if(!certified(bonus,*state->registry)) throw std::invalid_argument("Bonus component is not certified in this owned event domain");
  values.push_back(std::make_shared<const Bonus>(bonus));
 }
 return OwnedBonusEvent(std::make_shared<const OwnedBonusEvent::Storage>(OwnedBonusEvent::Storage{state->family,state->branch,std::move(values)}));
}
OwnedBonusEvent OwnedBonusEventDomain::makeObservedEvent(const std::vector<Bonus> & input, const std::vector<std::shared_ptr<const Bonus>> & observations) const
{
 if(!state) throw std::logic_error("Empty moved event domain");
 RegistryBuilder builder;
 for(const auto & value : state->captured) builder.bonus(value);
 for(const auto & value : observations) builder.bonus(value);
 const auto registry = std::move(builder).finish();
 std::vector<std::shared_ptr<const Bonus>> values;
 values.reserve(input.size());
 for(const auto & value : input)
 {
  if(!certified(value, registry)) throw std::invalid_argument("Bonus component was not observed in this preview");
  values.push_back(std::make_shared<const Bonus>(value));
 }
 return OwnedBonusEvent(std::make_shared<const OwnedBonusEvent::Storage>(OwnedBonusEvent::Storage{state->family, state->branch, std::move(values)}));
}
OwnedBonusEvent OwnedBonusEventDomain::fromCaptured(const std::vector<BonusGraphSnapshot::BonusId> & ids) const
{
 if(!state) throw std::logic_error("Empty moved event domain");
 std::vector<Bonus> values; values.reserve(ids.size());
 for(auto id : ids) values.push_back(*state->captured.at(id));
 return makeEvent(values);
}
bool OwnedBonusEventDomain::accepts(const OwnedBonusEvent & event) const
{
 return state && event.storage && event.storage->family == state->family && (event.storage->branch == state->branch || state->ancestors.contains(event.storage->branch));
}
