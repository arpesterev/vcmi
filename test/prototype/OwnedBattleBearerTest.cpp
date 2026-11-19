#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/BattleAI/StackWithBonuses.h"
#include "lib/bonuses/BonusCache.h"
#include "lib/bonuses/BonusParameters.h"
#include "lib/bonuses/Limiters.h"
#include "lib/bonuses/Propagators.h"
#include "lib/bonuses/Updaters.h"
#include "lib/GameLibrary.h"

class OwnedBattleBearerTest : public BattleTestFixture
{
public:
 void SetUp() override { BattleTestFixture::SetUp(); startGame(); startBattle(); }
 CStack * add()
 { return addStack(BattleSide::ATTACKER, creatureByName("core:woodElf"), BattleHex(5, 5), 10); }
 static CSelector selected(int id = 791)
 { return Selector::source(BonusSource::OTHER, ArtifactID(id)); }
 static std::shared_ptr<Bonus> effect(int id = 791, int value = 3)
 { return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::BLOCK_MAGIC_ABOVE, BonusSource::OTHER, value, ArtifactID(id)); }
 static CSelector rejects()
 { return [](const Bonus *) { return false; }; }
 static void compare(const TConstBonusListPtr & expected, const TConstBonusListPtr & actual)
 {
  ASSERT_EQ(actual->size(), expected->size());
  for(size_t i = 0; i < expected->size(); ++i)
  {
   const auto & e = (*expected)[i]; const auto & a = (*actual)[i];
   EXPECT_EQ(a->type, e->type); EXPECT_EQ(a->subtype, e->subtype); EXPECT_EQ(a->source, e->source); EXPECT_EQ(a->sid, e->sid);
   EXPECT_EQ(a->val, e->val); EXPECT_EQ(a->valType, e->valType); EXPECT_EQ(a->duration, e->duration); EXPECT_EQ(a->turnsRemain, e->turnsRemain);
   EXPECT_NE(a.get(), e.get());
   for(size_t j = 0; j < expected->size(); ++j)
   {
    EXPECT_EQ(a.get() == (*actual)[j].get(), e.get() == (*expected)[j].get());
    EXPECT_EQ(a->updater.get() == (*actual)[j]->updater.get(), e->updater.get() == (*expected)[j]->updater.get());
   }
  }
 }
};

TEST_F(OwnedBattleBearerTest, ColdAndWarmRootQueriesPreserveNativeRequestKeysAndEpoch)
{
 for(bool warm : {false, true})
 {
  auto bonus = effect(warm ? 792 : 791);
  battle()->addNewBonus(bonus);
  const auto selector = selected(warm ? 792 : 791);
  const auto * native = battle()->getBonusBearer();
  if(warm)
  {
   ASSERT_EQ(native->getAllBonuses(selector, "battle-root")->size(), 1);
  }
  auto frame = HypotheticBattle::captureOwnedFixture(*battle());
  const auto * owned = frame->getBonusBearer();
  ASSERT_NE(owned, nullptr); EXPECT_NE(owned, native);
  const auto epoch = native->getTreeVersion();
  EXPECT_EQ(owned->getTreeVersion(), epoch);
  if(warm)
   compare(native->getAllBonuses(rejects(), "battle-root"), owned->getAllBonuses(rejects(), "battle-root"));
  else
   compare(native->getAllBonuses(selector, "battle-root"), owned->getAllBonuses(selector, "battle-root"));
  auto cached = owned->getAllBonuses(rejects(), "battle-root");
  ASSERT_EQ(cached->size(), 1); EXPECT_EQ(cached->front()->val, 3);
  compare(native->getAllBonuses(rejects(), "battle-root"), cached);
  EXPECT_TRUE(owned->getAllBonuses(rejects())->empty());
  EXPECT_TRUE(owned->getAllBonuses(rejects(), "battle-root-empty")->empty());
  EXPECT_TRUE(owned->getAllBonuses(selector, "battle-root-empty")->empty());
  EXPECT_EQ(owned->valOfBonuses(selector), native->valOfBonuses(selector));
  EXPECT_EQ(owned->getTreeVersion(), epoch);
  EXPECT_EQ(frame->getBonusBearer(), owned);
  battle()->removeBonus(bonus);
 }
}

TEST_F(OwnedBattleBearerTest, BattleRootUsesItsOwnLimiterAndUpdaterContext)
{
 auto * unit = add();
 auto limited = effect(); limited->limiter = std::make_shared<CreatureLevelLimiter>();
 auto scaled = effect(792); scaled->updater = std::make_shared<TimesStackSizeUpdater>();
 battle()->addNewBonus(limited); battle()->addNewBonus(scaled);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle());
 const auto selector = selected().Or(selected(792));
 const auto expected = battle()->getAllBonuses(selector);
 ASSERT_EQ(expected->size(), 1); EXPECT_EQ(expected->front()->sid, BonusSourceID(ArtifactID(792))); EXPECT_EQ(expected->front()->val, 0);
 const auto unitExpected = unit->getAllBonuses(selector);
 ASSERT_EQ(unitExpected->size(), 2); EXPECT_EQ(unitExpected->getFirst(selected(792))->val, 10);
 compare(expected, frame->getBonusBearer()->getAllBonuses(selector));
 compare(unitExpected, frame->getForUpdate(unit->unitId())->getAllBonuses(selector));
}

TEST_F(OwnedBattleBearerTest, NestedValuesAndComponentAliasesAreOwnedAndQueryEditsAreIsolated)
{
 auto first = effect(), second = effect(792, 4);
 auto updater = std::make_shared<CompositeUpdater>(); updater->updaters.push_back(std::make_shared<IUpdater>());
 first->updater = updater; second->updater = updater;
 BonusParametersOnCombatEvent parameters;
 parameters.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{second, false});
 first->parameters = std::make_shared<BonusParameters>(parameters);
 battle()->addNewBonus(first); battle()->addNewBonus(second);
 const auto selector = selected().Or(selected(792));
 auto expected = battle()->getAllBonuses(selector, "closure"); ASSERT_EQ(expected->size(), 2);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 auto actual = frame->getBonusBearer()->getAllBonuses(selector, "closure"); compare(expected, actual);
 std::shared_ptr<Bonus> a, b;
 for(const auto & value : *actual)
 {
  if(selected()(value.get())) a = value;
  if(selected(792)(value.get())) b = value;
 }
 ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(a->parameters, nullptr);
 const auto & nested = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(a->parameters->toCustom<BonusParametersOnCombatEvent>().effects.at(0));
 EXPECT_EQ(nested.bonus, b); EXPECT_NE(nested.bonus, second);
 EXPECT_EQ(a->updater, b->updater); EXPECT_NE(a->updater, updater); EXPECT_NE(a->parameters, first->parameters);
 const auto * copiedUpdater = dynamic_cast<const CompositeUpdater *>(a->updater.get());
 ASSERT_NE(copiedUpdater, nullptr); ASSERT_EQ(copiedUpdater->updaters.size(), 1);
 EXPECT_NE(copiedUpdater->updaters.front(), updater->updaters.front());
 b->val = 99; a->val = 88; a->updater.reset();
 compare(expected, frame->getBonusBearer()->getAllBonuses(selector, "closure"));
 compare(expected, sibling->getBonusBearer()->getAllBonuses(selector, "closure"));
 EXPECT_EQ(first->val, 3); EXPECT_EQ(second->val, 4);
 updater->updaters.clear(); first->val = 21; second->val = 22;
 auto captured = frame->getBonusBearer()->getAllBonuses(selector, "closure");
 EXPECT_EQ(captured->getFirst(selected())->val, 3); EXPECT_EQ(captured->getFirst(selected(792))->val, 4);
 const auto * retainedUpdater = dynamic_cast<const CompositeUpdater *>(captured->getFirst(selected())->updater.get());
 ASSERT_NE(retainedUpdater, nullptr); EXPECT_EQ(retainedUpdater->updaters.size(), 1);
}

TEST_F(OwnedBattleBearerTest, StableRootBearerTracksPropagatedExpiryAndKeepsSiblingCache)
{
 auto * unit = add(); auto bonus = effect(); bonus->duration = BonusDuration::STACK_GETS_TURN;
 bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
 unit->addNewBonus(bonus);
 ASSERT_EQ(battle()->getAllBonuses(selected(), "aura")->size(), 1);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 const auto * owned = frame->getBonusBearer(); const auto * other = sibling->getBonusBearer();
 const auto epoch = owned->getTreeVersion(); const auto nativeEpoch = battle()->getTreeVersion();
 EXPECT_EQ(epoch, nativeEpoch); EXPECT_NE(owned, other);
 BonusValueCache cache(owned, selected()); EXPECT_EQ(cache.getValue(), 3);
 auto historical = owned->getAllBonuses(selected(), "aura"); ASSERT_EQ(historical->size(), 1);
 frame->nextTurn(unit->unitId(), BattleUnitTurnReason::UNIT_SPELLCAST);
 battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::UNIT_SPELLCAST);
 EXPECT_EQ(owned->getTreeVersion(), epoch); EXPECT_EQ(battle()->getTreeVersion(), nativeEpoch); EXPECT_EQ(cache.getValue(), 3);
 EXPECT_THROW(frame->nextTurn(unit->unitId(), static_cast<BattleUnitTurnReason>(99)), std::invalid_argument);
 EXPECT_EQ(owned->getTreeVersion(), epoch); EXPECT_EQ(cache.getValue(), 3);
 frame->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
 battle()->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
 EXPECT_EQ(frame->getBonusBearer(), owned); EXPECT_NE(owned->getTreeVersion(), epoch); EXPECT_NE(battle()->getTreeVersion(), nativeEpoch);
 compare(battle()->getAllBonuses(selected(), "aura"), owned->getAllBonuses(selected(), "aura"));
 EXPECT_TRUE(owned->getAllBonuses(selected(), "aura")->empty()); EXPECT_EQ(cache.getValue(), 0);
 EXPECT_EQ(other->getTreeVersion(), epoch); ASSERT_EQ(other->getAllBonuses(rejects(), "aura")->size(), 1);
 EXPECT_EQ(historical->front()->val, 3); EXPECT_EQ(historical->front()->duration, BonusDuration::STACK_GETS_TURN);
}

TEST_F(OwnedBattleBearerTest, RootReadsAndWithdrawalSurviveNativeDestructionWithRetainedArena)
{
 auto * unit = add(); const auto id = unit->unitId(); auto bonus = effect();
 bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE); unit->addNewBonus(bonus);
 auto expected = battle()->getAllBonuses(selected(), "lifetime"); ASSERT_EQ(expected->size(), 1);
 auto frame = HypotheticBattle::captureOwnedFixture(*battle()); auto sibling = frame->forkOwnedFixture();
 const auto * owned = frame->getBonusBearer(); auto retainedUnit = frame->getForUpdate(id);
 auto historical = owned->getAllBonuses(selected(), "lifetime");
 battle()->removeUnit(id); const auto afterRemoval = battle()->getAllBonuses(selected(), "lifetime"); ASSERT_TRUE(afterRemoval->empty());
 std::weak_ptr<CGameState> lifetime = gameState(); server.gameState.reset(); BattleTestFixture::TearDown(); ASSERT_TRUE(lifetime.expired());
 {
  struct NoLibrary { GameLibrary * saved = LIBRARY; NoLibrary() { LIBRARY = nullptr; } ~NoLibrary() { LIBRARY = saved; } } unavailable;
  compare(expected, owned->getAllBonuses(rejects(), "lifetime"));
  frame->removeUnit(id);
  EXPECT_EQ(frame->getBonusBearer(), owned); compare(afterRemoval, owned->getAllBonuses(selected(), "lifetime"));
  compare(expected, sibling->getBonusBearer()->getAllBonuses(rejects(), "lifetime"));
  frame.reset();
  EXPECT_TRUE(retainedUnit->isGhost()); EXPECT_TRUE(owned->getAllBonuses(selected(), "lifetime")->empty());
  EXPECT_EQ(historical->front()->val, 3); EXPECT_EQ(historical->front()->duration, BonusDuration::PERMANENT);
 }
 // getBonusBearer returns a borrowed pointer, kept alive here by retainedUnit's arena owner.
 retainedUnit.reset();
}
