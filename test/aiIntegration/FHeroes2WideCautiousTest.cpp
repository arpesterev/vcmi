/*
 * FHeroes2 wide cautious advance; GPL-2.0-or-later.
 * Source: ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541,
 * ai_battle.cpp: findOptimalPositionForSubsequentAttack/isUnitAbleToApproachPosition.
 * VCMI footprint and pathfinding remain native projections.
 */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "AI/Integration/Ports/FHeroes2NativeThreat.h"
#include "AI/Integration/Ports/FHeroes2TacticsContext.h"
#include "lib/GameLibrary.h"
#include "lib/battle/BattleAction.h"
#include "lib/battle/CObstacleInstance.h"
#include "lib/battle/CPlayerBattleCallback.h"
#include "lib/callback/AIFactory.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/callback/CBattleGameInterface.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/StackLocation.h"
#include "server/CGameHandler.h"
#include "server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class WideDecisionCallback final : public CBattleCallback
{
public:
 explicit WideDecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
 {
  if(action) throw std::logic_error("Duplicate wide cautious action");
  action = value;
 }
 std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
 {
  return std::nullopt;
 }
};
class WideEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<WideDecisionCallback> callback;
public:
 WideEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<WideDecisionCallback> cb)
  : gameCallback(std::move(game)), callback(std::move(cb)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}

class FHeroes2WideCautious : public BattleTestFixture
{
protected:
 CStack * actor = nullptr;
 CStack * enemy = nullptr;
 std::shared_ptr<WideDecisionCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 void prepare()
 {
  startGame(); ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), CreatureID(14).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id, SlotID(0)), CreatureID(0).toCreature()));
  attackerSideHero->setStackCount(SlotID(0), 40);
  defenderSideHero->setStackCount(SlotID(0), 1);
  startBattle(); ASSERT_FALSE(HasFatalFailure());
  battle()->obstacles.clear();
  for(const auto & unit : battle()->stacks)
   (unit->unitSide() == BattleSide::ATTACKER ? actor : enemy) = unit.get();
  ASSERT_NE(actor, nullptr); ASSERT_NE(enemy, nullptr);
  ASSERT_TRUE(actor->doubleWide()); ASSERT_FALSE(enemy->doubleWide());
  actor->setPosition(BattleHex(2, 5)); enemy->setPosition(BattleHex(14, 5));
  actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, -2, BonusSourceID()));
  ASSERT_EQ(actor->getMovementRange(), 4);
  callback = std::make_shared<WideDecisionCallback>(actor->unitOwner());
  callback->onBattleStarted(battle()); view = callback->getBattle(battle()->getBattleID());
 }
 void chooseAndApply(const std::function<void(const BattleAction &)> & check)
 {
  beginCombat(); ASSERT_FALSE(HasFatalFailure()); battle()->activeStack = actor->unitId();
  auto policy = AIFactory::createBattleAI("FHeroes2Tactics"); ASSERT_NE(policy, nullptr);
  auto environment = std::make_shared<WideEnvironment>(makeCallback(actor->unitOwner()), callback);
  AutocombatPreferences prefs; prefs.enableSpellsUsage = false;
  policy->playerID = actor->unitOwner(); policy->initBattleInterface(environment, callback, prefs);
  policy->battleStart(battle()->getBattleID(), battle()->getSideArmy(BattleSide::ATTACKER),
   battle()->getSideArmy(BattleSide::DEFENDER), battle()->getLocation(), attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
  policy->activeStack(battle()->getBattleID(), actor); ASSERT_TRUE(callback->action);
  check(*callback->action); ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), actor->unitOwner(), *callback->action));
 }
 void enclosedThreat(bool unsupported)
 {
  prepare(); ASSERT_FALSE(HasFatalFailure());
  auto * guard = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(8, 3), 35);
  ASSERT_NE(guard, nullptr);
  auto barrier = std::make_shared<SpellCreatedObstacle>();
  barrier->uniqueID = 1001; barrier->ID = SpellID::FORCE_FIELD;
  barrier->casterSide = BattleSide::DEFENDER; barrier->pos = guard->getPosition(); barrier->turnsRemaining = 2;
  barrier->customSize = guard->getPosition().getNeighbouringTiles();
  battle()->obstacles.push_back(barrier);
  const auto blocked = view->getReachability(guard);
  for(const auto & hex : barrier->customSize) ASSERT_FALSE(blocked.isReachable(hex));
  if(unsupported)
   guard->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_MIRROR, BonusSource::OTHER, 1, BonusSourceID()));
  ASSERT_EQ(bool(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view, *actor, {enemy, guard})), !unsupported);
  const auto stance = AIIntegration::FHeroes2TacticsContext::tactics(*view, *actor, {enemy, guard});
  ASSERT_FALSE(stance.defensive); ASSERT_TRUE(stance.cautiousOffensive);
  const auto origin = actor->getPosition(); const auto health = actor->getAvailableHealth();
  const auto route = view->getReachability(actor);
  BattleHex destination;
  chooseAndApply([&](const BattleAction & action)
  {
   ASSERT_EQ(action.actionType, EActionType::WALK); ASSERT_EQ(action.target.size(), 1u);
   destination = action.target.front().hexValue;
   EXPECT_EQ(actor->getPosition(), origin); EXPECT_EQ(actor->getAvailableHealth(), health);
   ASSERT_TRUE(route.isReachable(destination));
   if(unsupported) EXPECT_LT(route.distances[destination.toInt()], actor->getMovementRange());
   else EXPECT_EQ(route.distances[destination.toInt()], actor->getMovementRange());
   // Check the head AND tail at every predecessor, not only the endpoint.
   auto step = destination; int count = 0;
   while(step != origin && count++ < GameConstants::BFIELD_SIZE)
   {
    ASSERT_TRUE(step.isValid());
    ASSERT_EQ(actor->getHexes(step).size(), 2u);
    for(const auto & hex : actor->getHexes(step))
    {
     EXPECT_TRUE(hex.isAvailable()); EXPECT_FALSE(barrier->customSize.contains(hex));
     EXPECT_FALSE(enemy->coversPos(hex)); EXPECT_FALSE(guard->coversPos(hex));
     EXPECT_FALSE(actor->getSurroundingHexes(step).contains(hex));
    }
    step = route.predecessors[step.toInt()];
   }
   EXPECT_EQ(step, origin);
  });
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(actor->getPosition(), destination); EXPECT_EQ(actor->getAvailableHealth(), health);
 }
};

TEST_F(FHeroes2WideCautious,blockedEnemyAllowsFullWideAdvanceAcceptedByServer)
{
 enclosedThreat(false);
}
TEST_F(FHeroes2WideCautious,unsupportedThreatRetainsShorterNativeAdvanceAcceptedByServer)
{
 enclosedThreat(true);
}
TEST_F(FHeroes2WideCautious,blockedTailPreventsPassageDespiteClearHead)
{
 prepare(); ASSERT_FALSE(HasFatalFailure());
 actor->setPosition(BattleHex(8, 5)); enemy->setPosition(BattleHex(8, 0));
 for(int y = 0; y < GameConstants::BFIELD_HEIGHT; ++y)
  for(int x = 1; x < GameConstants::BFIELD_WIDTH - 1; ++x)
   if(x != 8 && !(x == 7 && y == 5)) addForceField(BattleHex(x, y));
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view, *actor, {enemy}));
 EXPECT_FALSE(view->getReachability(actor).isReachable(BattleHex(8, 2)));
 const auto origin = actor->getPosition();
 chooseAndApply([&](const BattleAction & action) { EXPECT_EQ(action.actionType, EActionType::NO_ACTION); });
 ASSERT_FALSE(HasFatalFailure()); EXPECT_EQ(actor->getPosition(), origin);
}
