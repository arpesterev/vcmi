#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "lib/battle/OwnedStackEffect.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/BonusList.h"

namespace
{
struct UnknownUpdater : TimesStackSizeUpdater {};
struct UnknownLimiter : CreatureLevelLimiter {};
struct UnknownPropagator : CPropagatorNodeType {};
}
class OwnedComponentProducerTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static OwnedFreshStackEffect::Value effect(int value = 7, int turns = 2)
 {
  OwnedFreshStackEffect::Value result;
  result.scalar = Bonus(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, SpellID(SpellID::HASTE));
  result.scalar.turnsRemain = turns; result.updater = 0; result.limiter = 0;
  return result;
 }
 OwnedFreshStackEffect packet() const
 {
  OwnedFreshStackEffect result; result.battleID = battle()->getBattleID(); result.updaters.emplace_back(); result.limiters.emplace_back(); return result;
 }
 static SetStackEffect native(const OwnedFreshStackEffect & input)
 {
  SetStackEffect result; result.battleID = input.battleID;
  std::vector<TUpdaterPtr> updaters;
  for(const auto & value : input.updaters) updaters.push_back(std::make_shared<TimesStackSizeUpdater>(value.minimum, value.maximum, value.stepSize, value.stepValue));
  std::vector<TLimiterPtr> limiters;
  for(const auto & value : input.limiters) limiters.push_back(std::make_shared<CreatureLevelLimiter>(value.minimum, value.maximum));
  std::vector<TPropagatorPtr> propagators;
  for(const auto type : input.propagators) propagators.push_back(std::make_shared<CPropagatorNodeType>(type));
  const auto convert = [&](const auto & source, auto & target)
  {
   for(const auto & [id, values] : source)
   {
    auto & entry = target.emplace_back(id, std::vector<Bonus>{});
    for(const auto & value : values)
    {
     auto & bonus = entry.second.emplace_back(value.scalar);
     if(value.updater) bonus.updater = updaters.at(*value.updater);
     if(value.propagationUpdater) bonus.propagationUpdater = updaters.at(*value.propagationUpdater);
     if(value.limiter) bonus.limiter = limiters.at(*value.limiter);
     if(value.propagator) bonus.propagator = propagators.at(*value.propagator);
    }
   }
  };
  convert(input.toRemove, result.toRemove); convert(input.toUpdate, result.toUpdate); convert(input.toAdd, result.toAdd);
  return result;
 }
 static void same(const IBonusBearer & owned, const IBonusBearer & reference)
 {
  const auto a = owned.getAllBonuses(Selector::all), b = reference.getAllBonuses(Selector::all);
  ASSERT_EQ(a->size(), b->size());
  for(size_t i = 0; i < a->size(); ++i)
  { EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode()); EXPECT_EQ((*a)[i]->bonusOwner, (*b)[i]->bonusOwner); }
 }
 void compare(HypotheticBattle & owned, const OwnedFreshStackEffect & descriptor, const std::vector<CStack *> & units)
 {
  owned.applyOwnedStackEffect(owned.makeOwnedFreshStackEffect(descriptor));
  auto raw = native(descriptor); gameHandler->sendAndApply(raw);
  for(const auto * unit : units) same(*owned.getForUpdate(unit->unitId()), *unit);
 }
 static TConstBonusListPtr effects(const IBonusBearer & unit)
 { return unit.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE))); }
};

TEST_F(OwnedComponentProducerTest, NativeFreshComparatorPreservesPhasesRepeatedRecipientsAndItems)
{
 auto * a = add(); auto * b = add(10); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto raw = packet(); raw.toAdd = {{a->unitId(), {effect(4), effect(5)}}, {b->unitId(), {effect(2)}}}; compare(*owned, raw, {a, b});
 raw.toRemove = {{a->unitId(), {effect(4)}}, {b->unitId(), {}}};
 raw.toUpdate = {{a->unitId(), {effect(9, 6)}}, {b->unitId(), {effect(3, 7)}}, {a->unitId(), {effect(8, 8)}}};
 raw.toAdd = {{a->unitId(), {}}, {b->unitId(), {effect(2, 3)}}, {a->unitId(), {effect(7, 4)}}}; compare(*owned, raw, {a, b});
}

TEST_F(OwnedComponentProducerTest, SharedIndicesAndDistinctEqualConfigurationsKeepNativeAliasPartitions)
{
 auto * a = add(); auto * b = add(10); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto descriptor = packet(); descriptor.updaters.emplace_back(); descriptor.limiters.emplace_back();
 auto shared = effect(); shared.propagationUpdater = 0;
 auto distinct = effect(8, 3); distinct.updater = 1; distinct.propagationUpdater = 1; distinct.limiter = 1;
 descriptor.toAdd = {{a->unitId(), {shared}}, {b->unitId(), {shared}}, {a->unitId(), {distinct}}};
 compare(*owned, descriptor, {a, b});
 const auto av = effects(*owned->getForUpdate(a->unitId())); const auto bv = effects(*owned->getForUpdate(b->unitId()));
 ASSERT_EQ(av->size(), 2); ASSERT_EQ(bv->size(), 1);
 const auto byTurns = [](const TConstBonusListPtr & values, int turns)
 {
  std::vector<std::shared_ptr<Bonus>> result;
  for(const auto & value : *values)
   if(value->turnsRemain == turns) result.push_back(value);
  return result;
 };
 // Updaters erase the input value distinction; sorted query positions are not identities.
 const auto ownedSharedA = byTurns(av, 2), ownedSharedB = byTurns(bv, 2), ownedDistinctA = byTurns(av, 3);
 ASSERT_EQ(ownedSharedA.size(), 1); ASSERT_EQ(ownedSharedB.size(), 1); ASSERT_EQ(ownedDistinctA.size(), 1);
 EXPECT_EQ(ownedSharedA[0]->val, 10); EXPECT_EQ(ownedDistinctA[0]->val, 10);
 EXPECT_EQ(ownedSharedA[0]->updater, ownedSharedB[0]->updater);
 EXPECT_EQ(ownedSharedA[0]->limiter, ownedSharedB[0]->limiter);
 EXPECT_EQ(ownedSharedA[0]->updater, ownedSharedA[0]->propagationUpdater);
 EXPECT_NE(ownedSharedA[0]->updater, ownedDistinctA[0]->updater);
 EXPECT_NE(ownedSharedA[0]->limiter, ownedDistinctA[0]->limiter);
 const auto an = effects(*a); const auto bn = effects(*b);
 ASSERT_EQ(an->size(), 2); ASSERT_EQ(bn->size(), 1);
 const auto nativeSharedA = byTurns(an, 2), nativeSharedB = byTurns(bn, 2), nativeDistinctA = byTurns(an, 3);
 ASSERT_EQ(nativeSharedA.size(), 1); ASSERT_EQ(nativeSharedB.size(), 1); ASSERT_EQ(nativeDistinctA.size(), 1);
 EXPECT_EQ(nativeSharedA[0]->val, 10); EXPECT_EQ(nativeDistinctA[0]->val, 10);
 EXPECT_EQ(nativeSharedA[0]->updater, nativeSharedB[0]->updater);
 EXPECT_EQ(nativeSharedA[0]->limiter, nativeSharedB[0]->limiter);
 EXPECT_EQ(nativeSharedA[0]->updater, nativeSharedA[0]->propagationUpdater);
 EXPECT_NE(nativeSharedA[0]->updater, nativeDistinctA[0]->updater);
 EXPECT_NE(nativeSharedA[0]->limiter, nativeDistinctA[0]->limiter);
 EXPECT_NE(ownedSharedA[0]->updater, nativeSharedA[0]->updater);
 descriptor.toAdd.clear(); descriptor.toRemove = {{a->unitId(), {shared}}}; compare(*owned, descriptor, {a, b});
}

TEST_F(OwnedComponentProducerTest, SurvivesDescriptorMutationAndDestructionWithMetadata)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto descriptor = packet(); auto value = effect(); value.scalar.hidden = true; value.scalar.stacking = "fresh-producer";
 value.scalar.bonusOwner = PlayerColor(1); value.scalar.description.appendRawString("fresh effect");
 descriptor.toAdd = {{a->unitId(), {value}}}; auto expected = native(descriptor);
 const auto certified = owned->makeOwnedFreshStackEffect(descriptor);
 descriptor.updaters[0].stepSize = 100; descriptor.limiters[0].minimum = 99; descriptor.toAdd[0].second[0].scalar.val = 999;
 descriptor = OwnedFreshStackEffect{};
 owned->applyOwnedStackEffect(certified); gameHandler->sendAndApply(expected); same(*owned->getForUpdate(a->unitId()), *a);
}

TEST_F(OwnedComponentProducerTest, RejectsAllRawComponentsIncludingUnknownSubtypesInEveryPhase)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto revision = owned->getForUpdate(a->unitId())->getTreeVersion();
 std::vector<OwnedFreshStackEffect::Value> bad(8, effect());
 bad[0].scalar.updater = std::make_shared<TimesStackSizeUpdater>(); bad[1].scalar.propagationUpdater = bad[0].scalar.updater;
 bad[2].scalar.limiter = std::make_shared<CreatureLevelLimiter>(); bad[3].scalar.propagator = std::make_shared<CPropagatorNodeType>();
 bad[4].scalar.parameters = std::make_shared<BonusParameters>(); bad[5].scalar.updater = std::make_shared<UnknownUpdater>();
 bad[6].scalar.limiter = std::make_shared<UnknownLimiter>(); bad[7].scalar.propagator = std::make_shared<UnknownPropagator>();
 for(const auto & value : bad) for(int phase = 0; phase < 3; ++phase)
 {
  auto descriptor = packet(); descriptor.toRemove = {{a->unitId(), {effect()}}}; descriptor.toUpdate = descriptor.toRemove; descriptor.toAdd = descriptor.toRemove;
  auto & entries = phase == 0 ? descriptor.toRemove : phase == 1 ? descriptor.toUpdate : descriptor.toAdd;
  entries.emplace_back(a->unitId(), std::vector<OwnedFreshStackEffect::Value>{value});
  EXPECT_THROW(owned->makeOwnedFreshStackEffect(descriptor), std::invalid_argument);
  EXPECT_EQ(owned->getForUpdate(a->unitId())->getTreeVersion(), revision); same(*owned->getForUpdate(a->unitId()), *a);
 }
}

TEST_F(OwnedComponentProducerTest, RejectsCapturedLiveAliasesAndCreatesIndependentEqualFreshComponents)
{
 auto * a = add(); auto initial = packet(); initial.toAdd = {{a->unitId(), {effect(20)}}};
 auto live = native(initial); gameHandler->sendAndApply(live);
 auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto candidate = packet(); candidate.toAdd = {{a->unitId(), {effect()}}};
 candidate.toAdd[0].second[0].scalar.updater = live.toAdd[0].second[0].updater;
 EXPECT_THROW(owned->makeOwnedFreshStackEffect(candidate), std::invalid_argument);
 candidate.toAdd[0].second[0].scalar.updater = (*effects(*owned->getForUpdate(a->unitId())))[0]->updater;
 EXPECT_THROW(owned->makeOwnedFreshStackEffect(candidate), std::invalid_argument);
 candidate.toAdd[0].second[0].scalar.updater.reset(); compare(*owned, candidate, {a});
 const auto values = effects(*owned->getForUpdate(a->unitId())); ASSERT_EQ(values->size(), 2);
 EXPECT_NE((*values)[0]->updater, (*values)[1]->updater);
 candidate.toAdd.clear(); candidate.toRemove = {{a->unitId(), {effect()}}}; compare(*owned, candidate, {a});
}

TEST_F(OwnedComponentProducerTest, PostCaptureLiveMutationCannotEnterFreshDescriptor)
{
 auto * a = add(); auto external = std::make_shared<CompositeUpdater>(); external->updaters.push_back(std::make_shared<TimesStackSizeUpdater>());
 auto descriptor = packet(); descriptor.toAdd = {{a->unitId(), {effect(20)}}}; auto raw = native(descriptor);
 raw.toAdd[0].second[0].updater = external; gameHandler->sendAndApply(raw);
 auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto before = effects(*owned->getForUpdate(a->unitId())); ASSERT_EQ(before->size(), 1); const auto expected = (*before)[0]->toJsonNode();
 external->updaters.clear();
 auto bad = packet(); auto value = effect(); value.scalar.updater = external; bad.toAdd = {{a->unitId(), {value}}};
 EXPECT_THROW(owned->makeOwnedFreshStackEffect(bad), std::invalid_argument);
 EXPECT_EQ((*effects(*owned->getForUpdate(a->unitId())))[0]->toJsonNode(), expected);
}

TEST_F(OwnedComponentProducerTest, ValidatesConfigurationAndIndicesBeforePublication)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto revision = owned->getForUpdate(a->unitId())->getTreeVersion();
 std::vector<OwnedFreshStackEffect> invalid;
 auto p = packet(); p.toAdd = {{a->unitId(), {effect()}}};
 invalid.push_back(p); invalid.back().updaters[0].stepSize = 0;
 invalid.push_back(p); invalid.back().updaters[0].stepSize = -1;
 invalid.push_back(p); invalid.back().updaters[0].minimum = 2; invalid.back().updaters[0].maximum = 1;
 invalid.push_back(p); invalid.back().updaters[0].stepValue = 2;
 invalid.push_back(p); invalid.back().updaters[0].stepValue = -1;
 invalid.push_back(p); invalid.back().limiters[0] = {4, 3};
 invalid.push_back(p); invalid.back().propagators = {static_cast<BonusNodeType>(999)};
 invalid.push_back(p); invalid.back().propagators = {BonusNodeType::NONE};
 invalid.push_back(p); invalid.back().battleID = BattleID::NONE;
 for(int field = 0; field < 4; ++field)
 {
  invalid.push_back(p); auto & value = invalid.back().toAdd[0].second[0];
  if(field == 0) value.updater = 99;
  if(field == 1) value.propagationUpdater = 99;
  if(field == 2) value.limiter = 99;
  if(field == 3) value.propagator = 99;
 }
 for(const auto & value : invalid)
 {
  EXPECT_THROW(owned->makeOwnedFreshStackEffect(value), std::exception);
  EXPECT_EQ(owned->getForUpdate(a->unitId())->getTreeVersion(), revision); same(*owned->getForUpdate(a->unitId()), *a);
 }
 p.updaters[0] = {-10, 10, 1, -2}; compare(*owned, p, {a});
}

TEST_F(OwnedComponentProducerTest, ForksKeepProvenanceAndNativeCompletedPrefix)
{
 auto * a = add(); auto parent = HypotheticBattle::captureOwnedFixture(*battle());
 auto left = parent->forkOwnedFixture(); auto right = parent->forkOwnedFixture();
 auto descriptor = packet(); descriptor.toAdd = {{a->unitId(), {effect(7, 2), effect(9, 0), effect(11, 4)}}};
 const auto event = parent->makeOwnedFreshStackEffect(descriptor);
 EXPECT_THROW(left->applyOwnedStackEffect(event), std::invalid_argument);
 EXPECT_THROW(right->applyOwnedStackEffect(event), std::invalid_argument);
 auto prefix = packet(); prefix.toAdd = {{a->unitId(), {effect(7, 2)}}}; auto raw = native(prefix); gameHandler->sendAndApply(raw);
 same(*left->getForUpdate(a->unitId()), *a); same(*right->getForUpdate(a->unitId()), *a);
 const auto child = left->makeOwnedFreshStackEffect(prefix);
 EXPECT_THROW(right->applyOwnedStackEffect(child), std::invalid_argument);
 EXPECT_THROW(parent->applyOwnedStackEffect(child), std::invalid_argument);
}

TEST_F(OwnedComponentProducerTest, FreshPropagatorRemovalMatchesNative)
{
 auto * a = add(); auto * b = add(10); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto descriptor = packet(); descriptor.propagators = {BonusNodeType::STACK_BATTLE, BonusNodeType::STACK_BATTLE};
 auto shared = effect(); shared.propagator = 0; shared.propagationUpdater = 0;
 auto distinct = effect(8); distinct.propagator = 1;
 descriptor.toAdd = {{a->unitId(), {shared}}, {b->unitId(), {shared, distinct}}};
 compare(*owned, descriptor, {a, b});
 descriptor.toAdd.clear(); descriptor.toRemove = {{a->unitId(), {shared}}, {b->unitId(), {distinct}}};
 compare(*owned, descriptor, {a, b});
}
