/* Ordinary descriptor producer and local query removal contracts. GPL-2.0-or-later. */
#include "StdInc.h"
#ifndef ENABLE_MINIMAL_LIB
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "mock/mock_Environment.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/bonuses/BonusList.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/spells/AbilityCaster.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/ISpellMechanics.h"
#include "lib/GameLibrary.h"
#include "lib/bonuses/Updaters.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "luascript/StdInc.h"
#include "luascript/api/callback/ServerCallback.h"
#include "luascript/api/library/BonusDescriptor.h"

class OrdinaryDescriptorEffectTest : public BattleTestFixture
{
public:
 testing::StrictMock<EnvironmentMock> environment;
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5)
 { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 std::shared_ptr<HypotheticBattle> project()
 {
  return std::make_shared<HypotheticBattle>(&environment,
   std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::ATTACKER)));
 }
 static CSelector selected() { return Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)); }
 static scripting::api::BonusDescriptor descriptor()
 {
  scripting::api::BonusDescriptor result;
  result.type.String() = "STACKS_SPEED";
  result.duration.String() = "N_TURNS";
  result.sourceType.String() = "SPELL_EFFECT";
  result.sourceID.String() = "core:haste";
  result.val = 7;
  result.turns = 2;
  result.limiters["type"].String() = "CREATURE_LEVEL_LIMITER";
  result.limiters["minLevel"].Integer() = 1;
  result.limiters["maxlevel"].Integer() = 7;
  result.updater["type"].String() = "TIMES_STACK_SIZE";
  result.updater["stepSize"].Integer() = 2;
  result.updater["stepValue"].Integer() = 1;
  return result;
 }
 static void same(const IBonusBearer & actual, const IBonusBearer & native)
 {
  const auto left = actual.getAllBonuses(selected());
  const auto right = native.getAllBonuses(selected());
  ASSERT_EQ(left->size(), right->size());
  for(size_t i = 0; i < left->size(); ++i)
  {
   EXPECT_EQ((*left)[i]->toJsonNode(), (*right)[i]->toJsonNode());
   EXPECT_EQ((*left)[i]->bonusOwner, (*right)[i]->bonusOwner);
  }
 }
 void nativeApply(const scripting::api::BonusDescriptor & data, uint32_t id, bool cumulative)
 {
  auto packet = data.toUnitEffect(battle()->getBattleID(), id, cumulative);
  gameHandler->sendAndApply(packet);
 }
};

TEST_F(OrdinaryDescriptorEffectTest, LuaAddUpdateAndQueriedRemovalMatchNative)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, true);
 nativeApply(data, source->unitId(), true);
 same(*local, *source);
 const auto retained = local->getAllBonuses(selected());
 ASSERT_EQ(retained->size(), 1);
 EXPECT_NE(retained->front()->limiter, source->getAllBonuses(selected())->front()->limiter);
 EXPECT_NE(retained->front()->updater, source->getAllBonuses(selected())->front()->updater);
 data.val = 9;
 data.turns = 5;
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, false);
 nativeApply(data, source->unitId(), false);
 same(*local, *source);
 EXPECT_EQ(retained->front()->turnsRemain, 2);
 EXPECT_EQ(local->getAllBonuses(selected())->front()->turnsRemain, 5);
 const auto removals = local->getAllBonuses(selected());
 ASSERT_EQ(removals->size(), 1);
 const auto nativeQuery = source->getAllBonuses(selected());
 ASSERT_EQ(nativeQuery->size(), 1);
 const auto nativeStored = source->getExportedBonusList().getFirst(selected());
 ASSERT_TRUE(nativeStored);
 EXPECT_EQ(nativeStored->val, 7); // Updating the duration does not replace the stored value with 9.
 EXPECT_EQ(removals->front()->val, 5); // TIMES_STACK_SIZE observes count 10 / step 2.
 EXPECT_EQ(nativeQuery->front()->val, 5);
 const auto epoch = local->getTreeVersion();
 SetStackEffect nativeRemoval;
 nativeRemoval.battleID = battle()->getBattleID();
 nativeRemoval.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{*nativeQuery->front()});
 scripting::api::ServerCallbackProxy::removeUnitBonuses(*frame->getServerCallback(), *frame, *local, *removals);
 gameHandler->sendAndApply(nativeRemoval);
 // Native removal matches the stored value, so a transformed query value does not remove it.
 ASSERT_EQ(local->getAllBonuses(selected())->size(), 1);
 ASSERT_EQ(source->getAllBonuses(selected())->size(), 1);
 EXPECT_EQ(local->getAllBonuses(selected())->front(), removals->front());
 EXPECT_EQ(local->getTreeVersion(), epoch);
 same(*local, *source);

 // Keep the local query's component provenance, but remove using the actual stored scalar value.
 auto localValue = *removals->front();
 localValue.val = nativeStored->val;
 SetStackEffect localRemoval;
 localRemoval.battleID = battle()->getBattleID();
 localRemoval.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{localValue});
 frame->getServerCallback()->apply(localRemoval);
 EXPECT_TRUE(local->getAllBonuses(selected())->empty());
 ASSERT_EQ(source->getAllBonuses(selected())->size(), 1); // Preview removal is isolated.
 nativeRemoval.toRemove.front().second = {*nativeStored};
 gameHandler->sendAndApply(nativeRemoval);
 EXPECT_TRUE(source->getAllBonuses(selected())->empty());
 same(*local, *source);
 EXPECT_EQ(retained->front()->turnsRemain, 2);
 EXPECT_EQ(removals->front()->turnsRemain, 5);
}

TEST_F(OrdinaryDescriptorEffectTest, RawFreshComponentsHaveNoProducerAuthority)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 const auto epoch = local->getTreeVersion();
 SetStackEffect raw;
 raw.battleID = battle()->getBattleID();
 raw.toAdd.emplace_back(source->unitId(), std::vector<Bonus>{descriptor().toBonus()});
 EXPECT_THROW(frame->getServerCallback()->apply(raw), std::invalid_argument);
 EXPECT_EQ(local->getTreeVersion(), epoch);
 same(*local, *source);
}

TEST_F(OrdinaryDescriptorEffectTest, ProducerProofIsBoundToTheCompletePacket)
{
 auto * source = add();
 auto * other = add(8);
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 const auto original = descriptor().toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 const auto epoch = local->getTreeVersion();
 const std::vector<std::function<void(SetStackEffect &)>> changes{
  [](auto & packet) { ++packet.toAdd.front().second.front().val; },
  [](auto & packet) { ++packet.toAdd.front().second.front().turnsRemain; },
  [](auto & packet) { packet.toAdd.front().second.front().limiter = std::make_shared<CreatureLevelLimiter>(); },
  [](auto & packet) { packet.toAdd.front().second.front().propagationUpdater = std::make_shared<TimesStackSizeUpdater>(); },
  [other](auto & packet) { packet.toAdd.front().first = other->unitId(); },
  [](auto & packet) { packet.battleID = BattleID(999); },
  [](auto & packet) { packet.toUpdate = std::move(packet.toAdd); packet.toAdd.clear(); },
  [](auto & packet) { packet.toAdd.push_back(packet.toAdd.front()); },
  [](auto & packet) { packet.toRemove = packet.toAdd; },
  [](auto & packet) { packet.toAdd.front().second.push_back(packet.toAdd.front().second.front()); }
 };
 for(const auto & change : changes)
 {
  auto altered = original;
  change(altered);
  EXPECT_THROW(frame->getServerCallback()->apply(altered), std::invalid_argument);
  EXPECT_EQ(local->getTreeVersion(), epoch);
  same(*local, *source);
 }
}

TEST_F(OrdinaryDescriptorEffectTest, CopiedPacketRetainsProofAndDescriptorChangesDoNotChangePayload)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 auto packet = data.toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 auto copied = packet;
 data.val = 900;
 data.updater["stepValue"].Integer() = 90;
 frame->getServerCallback()->apply(copied);
 gameHandler->sendAndApply(packet);
 same(*local, *source);
 EXPECT_NE(local->getAllBonuses(selected())->front()->val, data.val);
}

namespace
{
struct PacketFieldVisitor
{
 bool saving;
 size_t fields = 0;
 template<typename T> void operator&(T &) { ++fields; }
};
}

TEST_F(OrdinaryDescriptorEffectTest, DeserializationClearsLocalProofWithoutChangingWireFields)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto packet = descriptor().toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 PacketFieldVisitor writer{true};
 packet.serialize(writer);
 EXPECT_EQ(writer.fields, 4);
 PacketFieldVisitor reader{false};
 packet.serialize(reader);
 EXPECT_EQ(reader.fields, 4);
 EXPECT_THROW(frame->getServerCallback()->apply(packet), std::invalid_argument);
 same(*local, *source);
}

TEST_F(OrdinaryDescriptorEffectTest, ParserNormalizationAndCompositeComponentsKeepNativeMeaning)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 auto updater = data.updater;
 updater["minimum"].Integer() = 100;
 updater["maximum"].Integer() = 1;
 updater["stepSize"].Integer() = 0;
 updater["stepValue"].Integer() = 0;
 data.updater = JsonNode();
 data.updater.Vector().push_back(updater);
 data.updater.Vector().push_back(JsonNode("TIMES_STACK_LEVEL"));
 auto limiter = data.limiters;
 data.limiters = JsonNode();
 data.limiters.Vector().push_back(JsonNode("allOf"));
 data.limiters.Vector().push_back(limiter);
 data.hidden = true;
 data.stacking = "descriptor-native-parser";
 data.description = "descriptor effect";
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, true);
 nativeApply(data, source->unitId(), true);
 same(*local, *source);
}

TEST_F(OrdinaryDescriptorEffectTest, ForeignQueryComponentsRejectWholeRawPacketBeforeRemoval)
{
 auto * source = add();
 auto frame = project();
 auto sibling = project();
 auto local = frame->getForUpdate(source->unitId());
 auto remote = sibling->getForUpdate(source->unitId());
 auto data = descriptor();
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, true);
 scripting::api::ServerCallbackProxy::addUnitBonus(*sibling->getServerCallback(), *sibling, *remote, data, true);
 const auto before = local->getAllBonuses(selected());
 const auto epoch = local->getTreeVersion();
 SetStackEffect packet;
 packet.battleID = battle()->getBattleID();
 packet.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{*before->front()});
 packet.toAdd.emplace_back(source->unitId(), std::vector<Bonus>{*remote->getAllBonuses(selected())->front()});
 EXPECT_THROW(frame->getServerCallback()->apply(packet), std::invalid_argument);
 EXPECT_EQ(local->getTreeVersion(), epoch);
 EXPECT_EQ(local->getAllBonuses(selected())->size(), before->size());
}

TEST_F(OrdinaryDescriptorEffectTest, LateInvalidRowLeavesEarlierPhasesAndRetainedQueriesUnchanged)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, true);
 const auto retained = local->getAllBonuses(selected(), "descriptor-before-failure");
 ASSERT_EQ(retained->size(), 1);
 const auto savedValue = retained->front()->toJsonNode();
 const auto savedPointer = retained->front();
 const auto epoch = local->getTreeVersion();
 auto scalarData = data;
 scalarData.limiters = JsonNode();
 scalarData.updater = JsonNode();
 auto valid = scalarData.toBonus();
 auto invalid = valid;
 invalid.sid = SpellID(SpellID::SLOW);
 invalid.turnsRemain = 0;
 for(int phase = 0; phase < 2; ++phase)
 {
  SetStackEffect packet;
  packet.battleID = battle()->getBattleID();
  packet.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{*retained->front()});
  auto & rows = phase == 0 ? packet.toUpdate : packet.toAdd;
  rows.emplace_back(source->unitId(), std::vector<Bonus>{valid});
  rows.emplace_back(source->unitId(), std::vector<Bonus>{invalid});
  EXPECT_THROW(frame->getServerCallback()->apply(packet), std::invalid_argument);
  EXPECT_EQ(local->getTreeVersion(), epoch);
  const auto after = local->getAllBonuses(selected(), "descriptor-before-failure");
  ASSERT_EQ(after->size(), 1);
  EXPECT_EQ(after->front(), savedPointer);
  ASSERT_EQ(retained->size(), 1);
  EXPECT_EQ(retained->front(), savedPointer);
  EXPECT_EQ(retained->front()->toJsonNode(), savedValue);
 }
}


TEST_F(OrdinaryDescriptorEffectTest, ShippedSlayerLuaCastEvalPreservesEveryMasteryParameter)
{
 teachSpell(attackerSideHero, SpellID::SLAYER);
 EXPECT_CALL(environment, services()).Times(testing::AnyNumber()).WillRepeatedly(testing::Return(LIBRARY));
 EXPECT_CALL(environment, game()).Times(testing::AnyNumber()).WillRepeatedly(testing::Return(gameState().get()));
 const auto * spell = SpellID(SpellID::SLAYER).toSpell();
 const auto selector = Selector::type()(BonusType::SLAYER).And(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::SLAYER)));
 for(int mastery = 0; mastery < 4; ++mastery)
 {
  SCOPED_TRACE(mastery);
  setMagicMastery(attackerSideHero, mastery);
  auto * source = add(5 + mastery);
  auto frame = project();
  auto local = frame->getForUpdate(source->unitId());
  const auto mana = attackerSideHero->mana;
  spells::BattleCast preview(frame.get(), attackerSideHero, spells::Mode::HERO, spell);
  preview.setSpellLevel(mastery);
  preview.setEffectDuration(4);
  const spells::Target localTarget{battle::Destination(local.get())};
  ASSERT_TRUE(spell->battleMechanics(&preview)->canBeCastAt(localTarget));
  preview.castEval(frame->getServerCallback(), localTarget);
  const auto values = local->getAllBonuses(selector);
  ASSERT_EQ(values->size(), 1);
  ASSERT_TRUE(values->front()->parameters);
  EXPECT_EQ(values->front()->parameters->toNumber(), mastery);
  EXPECT_EQ(values->front()->turnsRemain, 4);
  EXPECT_TRUE(source->getAllBonuses(selector)->empty());
  EXPECT_EQ(attackerSideHero->mana, mana);

  spells::BattleCast native(battle(), attackerSideHero, spells::Mode::HERO, spell);
  native.setSpellLevel(mastery);
  native.setEffectDuration(4);
  native.castEval(gameHandler->spellEnv.get(), spells::Target{battle::Destination(source)});
  const auto reference = source->getAllBonuses(selector);
  ASSERT_EQ(reference->size(), 1);
  EXPECT_EQ(values->front()->toJsonNode(), reference->front()->toJsonNode());
  EXPECT_NE(values->front()->parameters, reference->front()->parameters);

  preview.setEffectDuration(8);
  preview.castEval(frame->getServerCallback(), localTarget);
  native.setEffectDuration(8);
  native.castEval(gameHandler->spellEnv.get(), spells::Target{battle::Destination(source)});
  const auto updated = local->getAllBonuses(selector);
  ASSERT_EQ(updated->size(), 1);
  EXPECT_EQ(updated->front()->turnsRemain, 8);
  EXPECT_EQ(updated->front()->parameters->toNumber(), mastery);
  EXPECT_EQ(updated->front()->toJsonNode(), source->getAllBonuses(selector)->front()->toJsonNode());
 }
}

TEST_F(OrdinaryDescriptorEffectTest, ShippedBindLuaCastEvalAndQueriedParameterRemovalMatchNative)
{
 auto * source = add();
 auto * binder = addStack(BattleSide::DEFENDER, creatureByName("core:dendroidGuard"), BattleHex(8, 5), 10);
 EXPECT_CALL(environment, services()).Times(testing::AnyNumber()).WillRepeatedly(testing::Return(LIBRARY));
 EXPECT_CALL(environment, game()).Times(testing::AnyNumber()).WillRepeatedly(testing::Return(gameState().get()));
 // The real ability belongs to the defender; an attacker-only callback cannot inspect its casting rules.
 auto frame = std::make_shared<HypotheticBattle>(&environment,
  std::make_shared<CPlayerBattleCallback>(battle(), battle()->sideToPlayer(BattleSide::DEFENDER)));
 auto local = frame->getForUpdate(source->unitId());
 auto localBinder = frame->getForUpdate(binder->unitId());
 const auto * spell = SpellID(SpellID::BIND).toSpell();
 spells::AbilityCaster previewCaster(localBinder.get(), 0);
 spells::BattleCast preview(frame.get(), &previewCaster, spells::Mode::PASSIVE, spell);
 const spells::Target localTarget{battle::Destination(local.get())};
 ASSERT_TRUE(spell->battleMechanics(&preview)->canBeCastAt(localTarget));
 preview.castEval(frame->getServerCallback(), localTarget);
 const auto selector = Selector::type()(BonusType::BIND_EFFECT);
 const auto values = local->getAllBonuses(selector);
 ASSERT_EQ(values->size(), 1);
 ASSERT_TRUE(values->front()->parameters);
 EXPECT_EQ(values->front()->parameters->toNumber(), binder->unitId());
 EXPECT_TRUE(source->getAllBonuses(selector)->empty());

 spells::AbilityCaster nativeCaster(binder, 0);
 spells::BattleCast native(battle(), &nativeCaster, spells::Mode::PASSIVE, spell);
 native.castEval(gameHandler->spellEnv.get(), spells::Target{battle::Destination(source)});
 const auto reference = source->getAllBonuses(selector);
 ASSERT_EQ(reference->size(), 1);
 EXPECT_EQ(values->front()->toJsonNode(), reference->front()->toJsonNode());
 EXPECT_NE(values->front()->parameters, reference->front()->parameters);

 scripting::api::ServerCallbackProxy::removeUnitBonuses(*frame->getServerCallback(), *frame, *local, *values);
 SetStackEffect nativeRemoval;
 nativeRemoval.battleID = battle()->getBattleID();
 nativeRemoval.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{*reference->front()});
 gameHandler->sendAndApply(nativeRemoval);
 EXPECT_TRUE(local->getAllBonuses(selector)->empty());
 EXPECT_TRUE(source->getAllBonuses(selector)->empty());
 EXPECT_EQ(values->front()->parameters->toNumber(), binder->unitId());
}

TEST_F(OrdinaryDescriptorEffectTest, ParameterProofRejectsRawValuesAndReplacedParameterObjects)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 data.type.String() = "SLAYER";
 data.limiters = JsonNode();
 data.updater = JsonNode();
 data.addInfo.Integer() = 3;
 const auto original = data.toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 const auto epoch = local->getTreeVersion();
 auto altered = original;
 altered.toAdd.front().second.front().parameters = std::make_shared<BonusParameters>(3);
 EXPECT_THROW(frame->getServerCallback()->apply(altered), std::invalid_argument);
 SetStackEffect raw;
 raw.battleID = original.battleID;
 raw.toAdd = original.toAdd;
 EXPECT_THROW(frame->getServerCallback()->apply(raw), std::invalid_argument);
 EXPECT_EQ(local->getTreeVersion(), epoch);
 EXPECT_TRUE(local->getAllBonuses(selected())->empty());
 auto accepted = original;
 frame->getServerCallback()->apply(accepted);
 ASSERT_EQ(local->getAllBonuses(selected())->size(), 1);
 EXPECT_EQ(local->getAllBonuses(selected())->front()->parameters->toNumber(), 3);
 EXPECT_NE(local->getAllBonuses(selected())->front()->parameters, original.toAdd.front().second.front().parameters);
}

TEST_F(OrdinaryDescriptorEffectTest, VectorParametersRemainLocalThroughUpdateAndRemoval)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 data.type.String() = "SPECIAL_PECULIAR_ENCHANT";
 data.addInfo.Vector() = {JsonNode(1), JsonNode(3), JsonNode(5)};
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, true);
 nativeApply(data, source->unitId(), true);
 same(*local, *source);
 const auto retained = local->getAllBonuses(selected());
 ASSERT_EQ(retained->size(), 1);
 EXPECT_EQ(retained->front()->parameters->toVector(), (std::vector<int32_t>{1, 3, 5}));
 EXPECT_NE(retained->front()->parameters, source->getAllBonuses(selected())->front()->parameters);
 data.turns = 6;
 scripting::api::ServerCallbackProxy::addUnitBonus(*frame->getServerCallback(), *frame, *local, data, false);
 nativeApply(data, source->unitId(), false);
 same(*local, *source);
 const auto current = local->getAllBonuses(selected());
 ASSERT_EQ(current->size(), 1);
 EXPECT_EQ(current->front()->parameters->toVector(), (std::vector<int32_t>{1, 3, 5}));
 const auto nativeQuery = source->getAllBonuses(selected());
 ASSERT_EQ(nativeQuery->size(), 1);
 const auto nativeStored = source->getExportedBonusList().getFirst(selected());
 ASSERT_TRUE(nativeStored);
 EXPECT_EQ(nativeStored->val, 7);
 EXPECT_EQ(current->front()->val, 5);
 EXPECT_EQ(nativeQuery->front()->val, 5);
 const auto epoch = local->getTreeVersion();
 scripting::api::ServerCallbackProxy::removeUnitBonuses(*frame->getServerCallback(), *frame, *local, *current);
 SetStackEffect nativeRemoval;
 nativeRemoval.battleID = battle()->getBattleID();
 nativeRemoval.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{*nativeQuery->front()});
 gameHandler->sendAndApply(nativeRemoval);
 // Transformed query removal is a no-op on both paths, including parameter-bearing values.
 ASSERT_EQ(local->getAllBonuses(selected())->size(), 1);
 ASSERT_EQ(source->getAllBonuses(selected())->size(), 1);
 EXPECT_EQ(local->getAllBonuses(selected())->front(), current->front());
 EXPECT_EQ(local->getTreeVersion(), epoch);
 same(*local, *source);

 auto localValue = *current->front();
 localValue.val = nativeStored->val;
 SetStackEffect localRemoval;
 localRemoval.battleID = battle()->getBattleID();
 localRemoval.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{localValue});
 frame->getServerCallback()->apply(localRemoval);
 EXPECT_TRUE(local->getAllBonuses(selected())->empty());
 ASSERT_EQ(source->getAllBonuses(selected())->size(), 1); // Local parameter removal leaves native state alone.
 nativeRemoval.toRemove.front().second = {*nativeStored};
 gameHandler->sendAndApply(nativeRemoval);
 EXPECT_TRUE(source->getAllBonuses(selected())->empty());
 same(*local, *source);
 EXPECT_EQ(retained->front()->parameters->toVector(), (std::vector<int32_t>{1, 3, 5}));
 EXPECT_EQ(current->front()->parameters->toVector(), (std::vector<int32_t>{1, 3, 5}));
}

TEST_F(OrdinaryDescriptorEffectTest, NestedParameterBonusesAreDeepDetachedWithoutMergingEqualValues)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 auto data = descriptor();
 data.type.String() = "ON_COMBAT_EVENT";
 data.limiters = JsonNode();
 data.updater = JsonNode();
 auto nested = descriptor();
 nested.type.String() = "SLAYER";
 nested.addInfo.Integer() = 3;
 JsonNode effect;
 effect["action"].String() = "bonus";
 effect["bonus"] = nested.toJson();
 data.addInfo["effect"].Vector() = {effect, effect};
 auto packet = data.toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 const auto & original = packet.toAdd.front().second.front().parameters->toCustom<BonusParametersOnCombatEvent>();
 ASSERT_EQ(original.effects.size(), 2);
 frame->getServerCallback()->apply(packet);
 const auto values = local->getAllBonuses(selected());
 ASSERT_EQ(values->size(), 1);
 const auto & owned = values->front()->parameters->toCustom<BonusParametersOnCombatEvent>();
 ASSERT_EQ(owned.effects.size(), 2);
 const auto & first = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(owned.effects[0]).bonus;
 const auto & second = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(owned.effects[1]).bonus;
 const auto & nativeFirst = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(original.effects[0]).bonus;
 EXPECT_NE(first, second);
 EXPECT_NE(first, nativeFirst);
 EXPECT_NE(first->parameters, nativeFirst->parameters);
 EXPECT_NE(first->limiter, nativeFirst->limiter);
 EXPECT_NE(first->updater, nativeFirst->updater);
 EXPECT_EQ(first->toJsonNode(), nativeFirst->toJsonNode());
 EXPECT_EQ(first->toJsonNode(), second->toJsonNode());
 EXPECT_EQ(first->parameters->toNumber(), 3);
 scripting::api::ServerCallbackProxy::removeUnitBonuses(*frame->getServerCallback(), *frame, *local, *values);
 EXPECT_TRUE(local->getAllBonuses(selected())->empty());
 EXPECT_EQ(first->parameters->toNumber(), 3);
}

TEST_F(OrdinaryDescriptorEffectTest, InPlaceParameterAndNestedComponentTamperingCannotAlterProducerProof)
{
 auto * source = add();
 auto frame = project();
 auto local = frame->getForUpdate(source->unitId());
 const auto epoch = local->getTreeVersion();
 auto data = descriptor();
 data.type.String() = "SLAYER";
 data.addInfo.Integer() = 3;
 auto packet = data.toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 auto mutableParameters = std::const_pointer_cast<BonusParameters>(packet.toAdd.front().second.front().parameters);
 mutableParameters->toCustom<int32_t>() = 1;
 EXPECT_THROW(frame->getServerCallback()->apply(packet), std::invalid_argument);
 EXPECT_EQ(local->getTreeVersion(), epoch);

 data.type.String() = "ON_COMBAT_EVENT";
 JsonNode effect;
 effect["action"].String() = "bonus";
 auto nested = descriptor();
 nested.type.String() = "SLAYER";
 nested.addInfo.Integer() = 3;
 effect["bonus"] = nested.toJson();
 data.addInfo = JsonNode();
 data.addInfo["effect"].Vector() = {effect};
 packet = data.toUnitEffect(battle()->getBattleID(), source->unitId(), true);
 const auto & nestedBonus = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(
  packet.toAdd.front().second.front().parameters->toCustom<BonusParametersOnCombatEvent>().effects.front()).bonus;
 auto limiter = std::const_pointer_cast<CreatureLevelLimiter>(std::dynamic_pointer_cast<const CreatureLevelLimiter>(nestedBonus->limiter));
 ASSERT_TRUE(limiter);
 limiter->minLevel = 7;
 EXPECT_THROW(frame->getServerCallback()->apply(packet), std::invalid_argument);
 EXPECT_EQ(local->getTreeVersion(), epoch);
 EXPECT_TRUE(local->getAllBonuses(selected())->empty());
}

#endif
