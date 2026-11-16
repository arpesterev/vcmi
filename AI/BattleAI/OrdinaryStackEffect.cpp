/* Descriptor-produced ordinary bonus packets. GPL-2.0-or-later. */
#include "StdInc.h"
#include "HypotheticFrameArena.h"
#include "../../lib/battle/OwnedStackEffect.h"
#include "../../lib/networkPacks/SetStackEffect.h"

namespace
{
template<typename T>
bool sameComponent(const std::shared_ptr<const T> & left, const std::shared_ptr<const T> & right)
{
 return left == right && !left.owner_before(right) && !right.owner_before(left);
}

bool sameDescriptorValue(const Bonus & left, const Bonus & right)
{
 return std::tie(left.duration, left.val, left.turnsRemain, left.valType, left.source,
  left.targetSourceType, left.targetSourceID, left.effectRange, left.type, left.subtype,
  left.sid, left.stacking, left.customIconPath, left.description, left.bonusOwner, left.hidden)
  == std::tie(right.duration, right.val, right.turnsRemain, right.valType, right.source,
  right.targetSourceType, right.targetSourceID, right.effectRange, right.type, right.subtype,
  right.sid, right.stacking, right.customIconPath, right.description, right.bonusOwner, right.hidden)
  && sameComponent(left.parameters, right.parameters)
  && sameComponent(left.limiter, right.limiter)
  && sameComponent(left.propagator, right.propagator)
  && sameComponent(left.updater, right.updater)
  && sameComponent(left.propagationUpdater, right.propagationUpdater);
}
}

void HypotheticFrameArena::applyOrdinaryStackEffect(const SetStackEffect & effect)
{
 std::lock_guard<std::recursive_mutex> lock(accessMutex);
 if(!nativeDefinitions) throw std::logic_error("Ordinary effect requires a native-definition frame");
 synchronize();
 OwnedStackEffect prepared;
 if(const auto & origin = effect.descriptorEffect)
 {
  const auto & entries = origin->cumulative ? effect.toAdd : effect.toUpdate;
  const auto & other = origin->cumulative ? effect.toUpdate : effect.toAdd;
  if(effect.battleID != origin->battleID || !effect.toRemove.empty() || !other.empty()
   || entries.size() != 1 || entries.front().first != origin->unit
   || entries.front().second.size() != 1
   || !sameDescriptorValue(entries.front().second.front(), origin->value)
   || SetStackEffect::snapshotDescriptorValue(entries.front().second.front()) != origin->configuration)
   throw std::invalid_argument("Descriptor effect packet differs from its producer payload");
  const auto events = importComponentEvents({{origin->value}});
  auto & output = origin->cumulative ? prepared.toAdd : prepared.toUpdate;
  output.emplace_back(origin->unit, events.front());
 }
 else
 {
  const auto prepare = [this](const auto & input, auto & output)
  {
   output.reserve(input.size());
   for(const auto & [id, values] : input)
    output.emplace_back(id, makeBonusEvent(values));
  };
  prepare(effect.toRemove, prepared.toRemove);
  prepare(effect.toUpdate, prepared.toUpdate);
  prepare(effect.toAdd, prepared.toAdd);
 }

 // Preserve native phase order, but publish the ordinary packet only after every row succeeds.
 auto candidate = model.transactionCopy();
 bool changed = false;
 for(const auto & [id, event] : prepared.toRemove)
  for(size_t index = 0; index < candidate.bonusEventSize(event); ++index)
   changed = candidate.removeBonusEventItem(id, event, index) || changed;
 const auto apply = [&](const auto & entries, bool forceAdd)
 {
  for(const auto & [id, event] : entries)
   if(candidate.bonusEventSize(event) != 0)
    changed = candidate.applyBonusEvent(id, event, forceAdd) || changed;
 };
 apply(prepared.toUpdate, false);
 apply(prepared.toAdd, true);
 if(changed) commitModel(std::move(candidate));
}
