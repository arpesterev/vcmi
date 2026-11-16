#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "server/CGameHandler.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/battle/OwnedStackEffect.h"
#include "lib/networkPacks/SetStackEffect.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Updaters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/BonusList.h"
#ifndef ENABLE_MINIMAL_LIB
#include "luascript/StdInc.h"
#include "luascript/api/library/BonusDescriptor.h"
#endif

class OwnedScalarProducerTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add(int x = 5) { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(x, 5), 10); }
 static Bonus effect(int value = 7, int turns = 2)
 {
  Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, value, SpellID(SpellID::HASTE));
  result.turnsRemain = turns;
  return result;
 }
 SetStackEffect packet() const { SetStackEffect result; result.battleID = battle()->getBattleID(); return result; }
 static void same(const IBonusBearer & owned, const IBonusBearer & native)
 {
  const auto a = owned.getAllBonuses(Selector::all), b = native.getAllBonuses(Selector::all);
  ASSERT_EQ(a->size(), b->size());
  for(size_t i = 0; i < a->size(); ++i)
  {
   EXPECT_EQ((*a)[i]->toJsonNode(), (*b)[i]->toJsonNode());
   EXPECT_EQ((*a)[i]->bonusOwner, (*b)[i]->bonusOwner);
  }
 }
 void compare(HypotheticBattle & owned, SetStackEffect & raw, const std::vector<CStack *> & units)
 {
  owned.applyOwnedStackEffect(owned.makeOwnedScalarStackEffect(raw));
  gameHandler->sendAndApply(raw);
  for(const auto * unit : units) same(*owned.getForUpdate(unit->unitId()), *unit);
 }
};

TEST_F(OwnedScalarProducerTest, PreservesPhasesRepeatedRecipientsEmptyGroupsAndItems)
{
 auto * a = add(); auto * b = add(10);
 auto initial = packet(); initial.toAdd.emplace_back(a->unitId(), std::vector<Bonus>{effect()}); gameHandler->sendAndApply(initial);
 auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto raw = packet();
 raw.toAdd = {{a->unitId(), {effect(4, 1), effect(5, 3)}}, {b->unitId(), {}}, {a->unitId(), {effect(9, 5)}}, {b->unitId(), {effect(2, 7)}}};
 raw.toUpdate = {{a->unitId(), {}}, {a->unitId(), {effect(11, 4)}}, {b->unitId(), {effect(3, 1)}}, {a->unitId(), {effect(6, 6)}}};
 raw.toRemove = {{b->unitId(), {}}, {a->unitId(), {effect()}}, {a->unitId(), {effect()}}};
 const auto certified = owned->makeOwnedScalarStackEffect(raw);
 ASSERT_EQ(certified.toRemove.size(), raw.toRemove.size());
 ASSERT_EQ(certified.toUpdate.size(), raw.toUpdate.size());
 ASSERT_EQ(certified.toAdd.size(), raw.toAdd.size());
 for(size_t i = 0; i < raw.toAdd.size(); ++i) EXPECT_EQ(certified.toAdd[i].first, raw.toAdd[i].first);
 compare(*owned, raw, {a, b});
}

TEST_F(OwnedScalarProducerTest, CopiesScalarPayloadAndRetainsMetadata)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto value = effect(); value.hidden = true; value.stacking = "scalar-producer"; value.bonusOwner = PlayerColor(1);
 value.targetSourceType = BonusSource::ARTIFACT; value.targetSourceID = ArtifactID(0);
 value.description.appendRawString("scalar effect");
 auto raw = packet(); raw.toAdd = {{a->unitId(), {value}}};
 const auto certified = owned->makeOwnedScalarStackEffect(raw);
 auto expected = raw;
 raw.toAdd[0].second[0].val = 999; raw.toAdd.clear();
 owned->applyOwnedStackEffect(certified); gameHandler->sendAndApply(expected);
 same(*owned->getForUpdate(a->unitId()), *a);
}

TEST_F(OwnedScalarProducerTest, RejectsEveryComponentInEveryPhaseBeforeMutation)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto revision = owned->getForUpdate(a->unitId())->getTreeVersion();
 std::vector<Bonus> unsupported(5, effect());
 unsupported[0].limiter = std::make_shared<CCreatureTypeLimiter>();
 unsupported[1].updater = std::make_shared<TimesStackSizeUpdater>();
 unsupported[2].propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
 unsupported[3].propagationUpdater = std::make_shared<TimesStackSizeUpdater>();
 unsupported[4].parameters = std::make_shared<BonusParameters>();
 for(const auto & value : unsupported)
  for(int phase = 0; phase < 3; ++phase)
  {
   auto raw = packet();
   raw.toRemove = {{a->unitId(), {effect()}}}; raw.toUpdate = {{a->unitId(), {effect(8)}}}; raw.toAdd = {{a->unitId(), {effect(9)}}};
   auto & entries = phase == 0 ? raw.toRemove : phase == 1 ? raw.toUpdate : raw.toAdd;
   entries.emplace_back(a->unitId(), std::vector<Bonus>{effect(10), value});
   EXPECT_THROW(owned->makeOwnedScalarStackEffect(raw), std::invalid_argument);
   EXPECT_EQ(owned->getForUpdate(a->unitId())->getTreeVersion(), revision);
   same(*owned->getForUpdate(a->unitId()), *a);
  }
}

TEST_F(OwnedScalarProducerTest, ValidatesBattleEvenForEmptyPackets)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto revision = owned->getForUpdate(a->unitId())->getTreeVersion();
 auto raw = packet(); compare(*owned, raw, {a});
 EXPECT_EQ(owned->getForUpdate(a->unitId())->getTreeVersion(), revision);
 raw.battleID = BattleID::NONE; EXPECT_THROW(owned->makeOwnedScalarStackEffect(raw), std::invalid_argument);
 raw.battleID = BattleID(999); EXPECT_THROW(owned->makeOwnedScalarStackEffect(raw), std::invalid_argument);
}

TEST_F(OwnedScalarProducerTest, KeepsAncestorEventsAndRejectsSiblingEvents)
{
 auto * a = add(); auto parent = HypotheticBattle::captureOwnedFixture(*battle());
 auto raw = packet(); raw.toAdd = {{a->unitId(), {effect()}}};
 const auto ancestral = parent->makeOwnedScalarStackEffect(raw);
 auto left = parent->forkOwnedFixture(); auto right = parent->forkOwnedFixture();
 const auto child = left->makeOwnedScalarStackEffect(raw);
 EXPECT_THROW(right->applyOwnedStackEffect(child), std::invalid_argument);
 same(*right->getForUpdate(a->unitId()), *a);
 left->applyOwnedStackEffect(ancestral); right->applyOwnedStackEffect(ancestral);
 same(*parent->getForUpdate(a->unitId()), *a);
 gameHandler->sendAndApply(raw);
 same(*left->getForUpdate(a->unitId()), *a); same(*right->getForUpdate(a->unitId()), *a);
}

TEST_F(OwnedScalarProducerTest, CertificationDefersTimedValidationAndApplyRetainsCompletedPrefix)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 auto unit = owned->getForUpdate(a->unitId());
 const auto revision = unit->getTreeVersion();
 auto raw = packet(); raw.toAdd = {{a->unitId(), {effect(7, 2), effect(9, 0), effect(11, 4)}}};
 std::optional<OwnedStackEffect> certified;
 ASSERT_NO_THROW(certified.emplace(owned->makeOwnedScalarStackEffect(raw)));
 EXPECT_EQ(unit->getTreeVersion(), revision);
 same(*unit, *a);
 EXPECT_THROW(owned->applyOwnedStackEffect(*certified), std::invalid_argument);
 EXPECT_NE(unit->getTreeVersion(), revision);
 auto prefix = packet(); prefix.toAdd = {{a->unitId(), {effect(7, 2)}}};
 gameHandler->sendAndApply(prefix);
 same(*unit, *a);
 const auto actual = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::HASTE)));
 ASSERT_EQ(actual->size(), 1);
 EXPECT_EQ((*actual)[0]->val, 7);
 EXPECT_EQ((*actual)[0]->turnsRemain, 2);
}

TEST_F(OwnedScalarProducerTest, MissingTargetsPreserveNativeNoopSemantics)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 const auto revision = owned->getForUpdate(a->unitId())->getTreeVersion();
 auto raw = packet();
 raw.toRemove = {{999999, {effect()}}}; raw.toUpdate = {{999999, {effect(3)}}}; raw.toAdd = {{999999, {effect(4)}}};
 compare(*owned, raw, {a});
 EXPECT_EQ(owned->getForUpdate(a->unitId())->getTreeVersion(), revision);
}

#ifndef ENABLE_MINIMAL_LIB
TEST_F(OwnedScalarProducerTest, LuaDescriptorScalarPayloadMatchesNativeAddAndUpdate)
{
 auto * a = add(); auto owned = HypotheticBattle::captureOwnedFixture(*battle());
 scripting::api::BonusDescriptor descriptor;
 descriptor.type.String() = "STACKS_SPEED"; descriptor.duration.String() = "N_TURNS";
 descriptor.sourceType.String() = "SPELL_EFFECT"; descriptor.sourceID.String() = "core:haste";
 descriptor.val = 7; descriptor.turns = 2;
 auto raw = packet(); raw.toAdd = {{a->unitId(), {descriptor.toBonus()}}}; compare(*owned, raw, {a});
 descriptor.val = 9; descriptor.turns = 5;
 raw.toAdd.clear(); raw.toUpdate = {{a->unitId(), {descriptor.toBonus()}}}; compare(*owned, raw, {a});
 descriptor.val = 7;
 raw.toRemove = {{a->unitId(), {descriptor.toBonus()}}}; raw.toUpdate.clear(); compare(*owned, raw, {a});
}

#endif
