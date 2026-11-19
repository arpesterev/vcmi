#include "StdInc.h"
#include "OwnedBonusGraphSession.h"
#include "BonusGraphMutationState.h"
#include "OwnedBonusValueCopier.h"
struct OwnedBonusGraphSession::State
{
 struct Cache
 {
  int64_t epoch = 0;
  bool valid = true;
  std::vector<OwnedBonusValueId> accepted;
  std::map<std::string, std::vector<OwnedBonusValueId>> requests;
 };
 std::shared_ptr<const BonusEvaluationSnapshot> evaluation;
 OwnedBonusLogicalDomain domain;
 const std::shared_ptr<const OwnedSpellPersistenceFacts> persistence;
 uint64_t revision=1;
 int64_t nextEpoch=0;
 std::vector<std::shared_ptr<Cache>> caches;
 std::vector<OwnedBonusValueId> capturedIds;
 std::shared_ptr<const BonusGraphSnapshot::MutationBatch> appliedBatch;
 State(BonusEvaluationSnapshot e, OwnedBonusLogicalDomain d, std::shared_ptr<const OwnedSpellPersistenceFacts> p, const State * prior=nullptr)
 : State(std::make_shared<const BonusEvaluationSnapshot>(std::move(e)), std::move(d), std::move(p), prior) {}
 State(std::shared_ptr<const BonusEvaluationSnapshot> e, OwnedBonusLogicalDomain d, std::shared_ptr<const OwnedSpellPersistenceFacts> p, const State * prior=nullptr)
 : evaluation(std::move(e)), domain(std::move(d)), persistence(std::move(p))
 {
  if(prior)
  {
   caches=prior->caches; capturedIds=prior->capturedIds; nextEpoch=prior->nextEpoch; appliedBatch=prior->appliedBatch;
  }
  else
  {
   const auto & graph=evaluation->graph();
   for(const auto & value:graph.capturedValues()) capturedIds.push_back(domain.internOwned(value));
   for(const auto & captured:graph.nodeCaches())
   {
    Cache cache; cache.epoch=captured.epoch; cache.valid=captured.valid;
    nextEpoch=std::max(nextEpoch,cache.epoch);
    for(auto id:captured.accepted) cache.accepted.push_back(capturedIds.at(id));
    for(const auto & [key,ids]:captured.requests)
     for(auto id:ids) cache.requests[key].push_back(capturedIds.at(id));
    // Empty cached requests are hits too.
    for(const auto & [key,ids]:captured.requests) cache.requests.try_emplace(key);
    caches.push_back(std::make_shared<Cache>(std::move(cache)));
   }
  }
  if(!prior)
   while(caches.size()<evaluation->graph().nodes().size()) { Cache cache; cache.valid=false; caches.push_back(std::make_shared<Cache>(std::move(cache))); }
  installCaches(*evaluation);
 }
 Cache & cacheForUpdate(BonusGraphSnapshot::NodeId node)
 {
  auto & entry = caches.at(node);
  if(entry.use_count() != 1) entry = std::make_shared<Cache>(*entry);
  return *entry;
 }
 void invalidate(const std::vector<BonusGraphSnapshot::NodeId> & nodes)
 {
  if(nodes.empty()) return;
  if(nextEpoch>=std::numeric_limits<int32_t>::max()) throw std::overflow_error("Owned bonus epoch exhausted");
  const auto epoch=++nextEpoch;
  for(auto node:nodes)
  {
   auto & cache=cacheForUpdate(node); cache.epoch=epoch; cache.valid=false;
   cache.accepted.clear(); cache.requests.clear();
  }
 }
 void installCaches(const BonusEvaluationSnapshot & next)
 {
  while(caches.size() < next.graph().nodes().size()) caches.push_back(std::make_shared<Cache>());
  caches.resize(next.graph().nodes().size());
  const auto batch=next.graph().mutationBatch();
  if(batch && batch!=appliedBatch) invalidate(batch->invalidated);
  appliedBatch=batch;
 }
 void install(BonusEvaluationSnapshot next)
 {
  auto replacement=std::make_shared<const BonusEvaluationSnapshot>(std::move(next));
  installCaches(*replacement);
  evaluation=std::move(replacement);
 }
};
OwnedBonusGraphSession::OwnedBonusGraphSession(std::unique_ptr<State> value):state(std::move(value)){}
OwnedBonusGraphSession::~OwnedBonusGraphSession()=default;
OwnedBonusGraphSession::OwnedBonusGraphSession(OwnedBonusGraphSession&&)noexcept=default;
OwnedBonusGraphSession & OwnedBonusGraphSession::operator=(OwnedBonusGraphSession&&)noexcept=default;
OwnedBonusGraphSession OwnedBonusGraphSession::capture(BonusEvaluationSnapshot evaluation, OwnedSpellPersistenceFacts persistence)
{
 auto domain=OwnedBonusLogicalDomain::capture(evaluation.graph());
 return OwnedBonusGraphSession(std::make_unique<State>(std::move(evaluation),std::move(domain),std::make_shared<const OwnedSpellPersistenceFacts>(persistence)));
}
OwnedBonusGraphSession OwnedBonusGraphSession::fork()const
{
 if(!state)throw std::logic_error("Moved owned graph session");
 auto child=std::make_unique<State>(state->evaluation,state->domain.fork(),state->persistence,state.get());child->revision=state->revision;return OwnedBonusGraphSession(std::move(child));
}
BonusEvaluationSnapshot OwnedBonusGraphSession::evaluation()const
{
 if(!state)throw std::logic_error("Moved owned graph session");
 auto graph=state->evaluation->graph();
 graph.capturedArenaValues.clear(); graph.capturedGraphValueIds.clear(); graph.capturedNodeCaches.clear(); graph.capturedMutationBatch.reset();
 std::map<const Bonus *,BonusGraphSnapshot::ValueId> values;
 const auto intern=[&](const std::shared_ptr<const Bonus> & value)
 {
  auto [entry,inserted]=values.emplace(value.get(),values.size());
  if(inserted) graph.capturedArenaValues.push_back(value);
  return entry->second;
 };
 // Preserve capture IDs used by typed proxy memberships and extra-list metadata.
 for(const auto & id:state->capturedIds) intern(state->domain.value(id));
 for(const auto & value:graph.capturedBonuses) graph.capturedGraphValueIds.push_back(intern(value));
 for(const auto & entry:state->caches)
 {
  const auto & cache = *entry;
  BonusGraphSnapshot::NodeCache output; output.epoch=cache.epoch; output.valid=cache.valid;
  if(cache.valid)
  {
   for(const auto & id:cache.accepted) output.accepted.push_back(intern(state->domain.value(id)));
   for(const auto & [key,ids]:cache.requests)
   {
    auto & request=output.requests[key];
    for(const auto & id:ids) request.push_back(intern(state->domain.value(id)));
   }
  }
  graph.capturedNodeCaches.push_back(std::move(output));
 }
 return state->evaluation->withGraph(std::move(graph));
}
OwnedLogicalBonusEvent OwnedBonusGraphSession::eventFromBonuses(const std::vector<BonusId> & ids)const
{
 if(!state)throw std::logic_error("Moved owned graph session");
 std::vector<OwnedBonusValueId> logical;for(auto id:ids)logical.push_back(state->domain.capturedValue(id));return state->domain.fromValues(logical);
}
OwnedLogicalBonusEvent OwnedBonusGraphSession::makeEvent(const std::vector<Bonus> & values)const
{
 if(!state)throw std::logic_error("Moved owned graph session");
 return state->domain.makeEvent(values);
}
void OwnedBonusGraphSession::setTurnsRemain(BonusId id,int turns)
{
 if(!state)throw std::logic_error("Moved owned graph session");
 if(state->revision==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 auto trial=std::make_unique<State>(state->evaluation,state->domain.transactionCopy(),state->persistence,state.get());trial->domain.setTurnsRemain(trial->domain.capturedValue(id),turns);
 auto mutation=trial->evaluation->mutations();mutation.replaceOwnedValues(trial->domain.graphValues(trial->evaluation->graph()));trial->install(trial->evaluation->withGraph(mutation.snapshot()));trial->revision=state->revision+1;state.swap(trial);
}
OwnedBonusGraphSession::BonusId OwnedBonusGraphSession::addEventRoot(NodeId node,const OwnedLogicalBonusEvent & event,size_t index)
{
 if(!state)throw std::logic_error("Moved owned graph session");
 if(state->revision==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 auto resolved=state->domain.resolve(event);const auto & input=resolved.at(index);
 auto trial=std::make_unique<State>(state->evaluation,state->domain.transactionCopy(),state->persistence,state.get());auto mutation=trial->evaluation->mutations();auto id=mutation.addOwnedRoot(node,*input);auto graph=mutation.snapshot();trial->domain.adoptAppendedGraph(graph);trial->install(trial->evaluation->withGraph(std::move(graph)));trial->revision=state->revision+1;state.swap(trial);return id;
}
uint64_t OwnedBonusGraphSession::revision()const{if(!state)throw std::logic_error("Moved owned graph session");
 return state->revision;}

OwnedBonusGraphSession OwnedBonusGraphSession::transactionCopy() const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto trial = std::make_unique<State>(state->evaluation, state->domain.transactionCopy(),state->persistence,state.get());
 trial->revision = state->revision;
 return OwnedBonusGraphSession(std::move(trial));
}
const BonusEvaluationSnapshot & OwnedBonusGraphSession::evaluationRef() const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 return *state->evaluation;
}
void OwnedBonusGraphSession::replaceEvaluation(BonusEvaluationSnapshot evaluation)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto trial = std::make_unique<State>(std::move(evaluation), state->domain.transactionCopy(),state->persistence,state.get());
 trial->domain.adoptAppendedGraph(trial->evaluation->graph());
 trial->revision = state->revision;
 state.swap(trial);
}
size_t OwnedBonusGraphSession::eventSize(const OwnedLogicalBonusEvent & event) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 return state->domain.resolve(event).size();
}
void OwnedBonusGraphSession::applyEventItem(NodeId node, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd)
{
 auto trial=transactionCopy();
 trial.applyEventItemImpl(node,event,index,forceAdd);
 state.swap(trial.state);
}
void OwnedBonusGraphSession::applyEventItemImpl(NodeId node, const OwnedLogicalBonusEvent & event, size_t index, bool forceAdd)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 const auto resolved = state->domain.resolve(event);
 const auto & input = *resolved.at(index);
 if(forceAdd || query(node, Selector::source(BonusSource::SPELL_EFFECT, input.sid).And(Selector::typeSubtypeValueType(input.type, input.subtype, input.valType)))->empty())
 {
  // Native addNewBonus asserts this condition. The opt-in typed API rejects it
  // before mutation rather than relying on debug-only process termination.
  if((Bonus::NTurns(&input) || Bonus::NDays(&input) || Bonus::OneWeek(&input)) && !input.turnsRemain)
   throw std::invalid_argument("A timed bonus requires nonzero remaining duration");
  addEventRoot(node, event, index);
  return;
 }
 auto trial = std::make_unique<State>(state->evaluation, state->domain.transactionCopy(),state->persistence,state.get());
 for(auto id : state->evaluation->graph().nodes().at(node).exported)
 {
  const auto & value = *state->evaluation->graph().bonuses().at(id);
  if(value.source == input.source && value.sid == input.sid && value.type == input.type && value.subtype == input.subtype && value.valType == input.valType)
   trial->domain.setTurnsRemain(trial->domain.capturedValue(id), std::max(value.turnsRemain, input.turnsRemain));
 }
 auto mutation = trial->evaluation->mutations();
 mutation.replaceOwnedValues(trial->domain.graphValues(trial->evaluation->graph()));
 trial->install(trial->evaluation->withGraph(mutation.snapshot()));
 trial->revision = state->revision + 1;
 std::vector<NodeId> invalidated;
 std::set<NodeId> seen;
 const auto visit=[&](const auto & self,NodeId current)->void
 {
  if(!seen.insert(current).second) return;
  invalidated.push_back(current);
  for(auto child:trial->evaluation->graph().nodes().at(current).children) self(self,child);
 };
 visit(visit,node); trial->invalidate(invalidated);
 state.swap(trial);
}

bool OwnedBonusGraphSession::removeEventItem(NodeId node, const OwnedLogicalBonusEvent & event, size_t index)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 const auto resolved = state->domain.resolve(event);
 const auto & input = *resolved.at(index);
 auto mutation = state->evaluation->mutations();
 bool removed = false;
 const auto removeRecursive = [&](const auto & self, NodeId current) -> void
 {
  std::vector<BonusId> matches;
  for(auto id : mutation.nodes().at(current).exported)
  {
   const auto & value = *mutation.bonuses().at(id);
   if(input.duration == value.duration && input.type == value.type
    && input.subtype == value.subtype && input.source == value.source
    && input.val == value.val && input.sid == value.sid
    && input.valType == value.valType && input.effectRange == value.effectRange)
    matches.push_back(id);
  }
  for(auto id : matches) { mutation.removeBonus(current, id); removed = true; }
  // The native traversal revisits shared descendants. Membership is read afresh.
  const auto children = mutation.nodes().at(current).children;
  for(auto child : children) self(self, child);
 };
 removeRecursive(removeRecursive, node);
 if(!removed) return false;
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 auto trial = std::make_unique<State>(state->evaluation->withGraph(mutation.snapshot()), state->domain.transactionCopy(),state->persistence,state.get());
 // Logical values survive membership removal for previously certified nested events.
 trial->revision = state->revision + 1;
 state.swap(trial);
 return true;
}


bool OwnedBonusGraphSession::expireBonuses(NodeId node, OwnedBonusExpiry event)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 CSelector selector;
 switch(event)
 {
  case OwnedBonusExpiry::Round: selector = Bonus::NTurns; break;
  case OwnedBonusExpiry::GetsTurn: selector = Bonus::UntilGetsTurn; break;
  case OwnedBonusExpiry::Attack: selector = Bonus::UntilAttack; break;
  case OwnedBonusExpiry::OwnAttack: selector = Bonus::UntilOwnAttack; break;
  case OwnedBonusExpiry::BeingAttacked: selector = Bonus::UntilBeingAttacked; break;
  case OwnedBonusExpiry::IndirectDamage: selector = Bonus::UntilTakingIndirectDamage; break;
  default: throw std::invalid_argument("Unsupported owned bonus expiry event");
 }
 auto trial = std::make_unique<State>(state->evaluation, state->domain.transactionCopy(),state->persistence,state.get());
 auto mutation = trial->evaluation->mutations();
 bool changed = false;
 const auto visit = [&](const auto & self, NodeId current) -> void
 {
  std::vector<BonusId> matches;
  for(auto id : mutation.nodes().at(current).exported)
   if(selector(mutation.bonuses().at(id).get())) matches.push_back(id);
  for(auto id : matches)
  {
   if(event == OwnedBonusExpiry::Round)
   {
    const auto turns = mutation.bonuses().at(id)->turnsRemain;
    if(turns == std::numeric_limits<si16>::min())
     throw std::invalid_argument("Owned bonus duration decrement would underflow");
    trial->domain.setTurnsRemain(trial->domain.capturedValue(id), turns - 1);
    mutation.replaceOwnedValues(trial->domain.graphValues(trial->evaluation->graph()));
    if(turns - 1 <= 0) mutation.removeBonus(current, id);
   }
   else mutation.removeBonus(current, id);
   changed = true;
  }
  const auto children = mutation.nodes().at(current).children;
  for(auto child : children) self(self, child);
 };
 visit(visit, node);
 if(!changed) return false;
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 trial->install(trial->evaluation->withGraph(mutation.snapshot()));
 trial->revision = state->revision + 1;
 state.swap(trial);
 return true;
}

std::vector<OwnedLogicalBonusEvent> OwnedBonusGraphSession::importComponentEvents(const std::vector<std::vector<Bonus>> & groups) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 return state->domain.importComponentEvents(groups);
}


bool OwnedBonusGraphSession::removeNonpersistentSpells(NodeId node)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto mutation = state->evaluation->mutations();
 bool changed = false;
 const auto visit = [&](const auto & self, NodeId current) -> void
 {
  std::vector<BonusId> matches;
  for(auto id : mutation.nodes().at(current).exported)
  {
   const auto & bonus = *mutation.bonuses().at(id);
   if(bonus.source != BonusSource::SPELL_EFFECT) continue;
   const auto spell = bonus.sid.as<SpellID>();
   const auto fact = state->persistence->find(spell);
   if(fact == state->persistence->end())
    throw std::invalid_argument("Spell persistence was not captured for lifecycle cleanup");
   if(!fact->second) matches.push_back(id);
  }
  for(auto id : matches) { mutation.removeBonus(current, id); changed = true; }
  const auto children = mutation.nodes().at(current).children;
  for(auto child : children) self(self, child);
 };
 visit(visit, node);
 if(!changed) return false;
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 auto trial = std::make_unique<State>(state->evaluation->withGraph(mutation.snapshot()), state->domain.transactionCopy(), state->persistence,state.get());
 trial->revision = state->revision + 1;
 state.swap(trial);
 return true;
}

std::vector<OwnedBonusValueId> OwnedBonusGraphSession::queryMembership(NodeId node, const CSelector & selector, const std::string & cachingStr) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 const auto & current=*state->caches.at(node);
 if(current.valid)
 {
  if(!cachingStr.empty())
  {
   auto request=current.requests.find(cachingStr);
   if(request!=current.requests.end()) return request->second;
  }
  if(current.accepted.empty())
  {
   if(!cachingStr.empty()) state->cacheForUpdate(node).requests.try_emplace(cachingStr);
   return {};
  }
 }
 // Selector execution retains the transaction and its detached input values.
 auto trial=std::make_unique<State>(state->evaluation,state->domain.transactionCopy(),state->persistence,state.get());
 trial->revision=state->revision;
 auto & cache=trial->cacheForUpdate(node);
 if(!cache.valid)
 {
  auto accepted=trial->evaluation->evaluateAccepted(node,trial->domain.graphValues(trial->evaluation->graph()));
  std::vector<OwnedBonusValueId> ids;
  for(const auto & value:accepted) ids.push_back(trial->domain.internOwned(value));
  cache.accepted=std::move(ids); cache.requests.clear(); cache.valid=true;
 }
 std::vector<OwnedBonusValueId> selected;
 auto request=cache.requests.find(cachingStr);
 if(!cachingStr.empty() && request!=cache.requests.end()) selected=request->second;
 else
 {
  std::vector<std::shared_ptr<const Bonus>> accepted;
  for(const auto & id:cache.accepted) accepted.push_back(trial->domain.value(id));
  auto detached=BonusGraphSnapshot::detachValues(accepted);
  for(size_t i=0;i<cache.accepted.size();++i)
   if(selector(detached.at(i).get())) selected.push_back(cache.accepted.at(i));
  if(!cachingStr.empty()) cache.requests.emplace(cachingStr,selected);
 }
 state.swap(trial);
 return selected;
}
TConstBonusListPtr OwnedBonusGraphSession::resolveMembership(const std::vector<OwnedBonusValueId> & ids) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 std::vector<std::shared_ptr<const Bonus>> values;
 for(const auto & id:ids) values.push_back(state->domain.value(id));
 auto result=std::make_shared<BonusList>();
 OwnedBonusValueCopier copier;
 for(const auto & value:values) result->push_back(copier.copy(value));
 return result;
}
TConstBonusListPtr OwnedBonusGraphSession::query(NodeId node, const CSelector & selector, const std::string & cachingStr) const
{
 return resolveMembership(queryMembership(node,selector,cachingStr));
}
TConstBonusListPtr OwnedBonusGraphSession::queryWithStableCopies(NodeId node, const CSelector & selector, const std::string & key, OwnedBonusValueCopier & copies) const
{
 const auto ids = queryMembership(node, selector, key);
 auto result = std::make_shared<BonusList>();
 for(const auto & id : ids) result->push_back(copies.copy(state->domain.value(id)));
 return result;
}
OwnedLogicalBonusEvent OwnedBonusGraphSession::makeObservedEvent(const std::vector<Bonus> & values, const OwnedBonusValueCopier & copies) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto trial = std::make_unique<State>(state->evaluation, state->domain.transactionCopy(), state->persistence, state.get());
 trial->revision = state->revision;
 auto event = trial->domain.makeObservedEvent(values, copies.ownedSources());
 state.swap(trial);
 return event;
}
std::vector<OwnedBonusValueId> OwnedBonusGraphSession::capturedMembership(const std::vector<BonusGraphSnapshot::ValueId> & ids) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 std::vector<OwnedBonusValueId> result;
 for(auto id:ids) result.push_back(state->capturedIds.at(id));
 return result;
}
int64_t OwnedBonusGraphSession::nodeVersion(NodeId node) const
{
 if(!state) throw std::logic_error("Moved owned graph session");
 return state->caches.at(node)->epoch;
}
void OwnedBonusGraphSession::invalidateNodeAndDescendants(NodeId node)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto trial=std::make_unique<State>(state->evaluation,state->domain.transactionCopy(),state->persistence,state.get());
 trial->revision=state->revision;
 std::vector<NodeId> nodes;
 std::set<NodeId> seen;
 const auto visit=[&](const auto & self,NodeId current)->void
 {
  if(!seen.insert(current).second) return;
  nodes.push_back(current);
  for(auto child:trial->evaluation->graph().nodes().at(current).children) self(self,child);
 };
 visit(visit,node); trial->invalidate(nodes); state.swap(trial);
}

bool OwnedBonusGraphSession::removeBonuses(NodeId node, const CSelector & selector)
{
 if(!state) throw std::logic_error("Moved owned graph session");
 auto mutation = state->evaluation->mutations();
 bool changed = false;
 const auto visit = [&](const auto & self, NodeId current) -> void
 {
  std::vector<BonusId> matches;
  for(auto id : mutation.nodes().at(current).exported)
   if(selector(mutation.bonuses().at(id).get())) matches.push_back(id);
  for(auto id : matches) { mutation.removeBonus(current, id); changed = true; }
  const auto children = mutation.nodes().at(current).children;
  for(auto child : children) self(self, child);
 };
 visit(visit, node);
 if(!changed) return false;
 if(state->revision == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Owned graph revision exhausted");
 auto trial = std::make_unique<State>(state->evaluation->withGraph(mutation.snapshot()), state->domain.transactionCopy(), state->persistence, state.get());
 trial->revision = state->revision + 1;
 state.swap(trial);
 return true;
}
