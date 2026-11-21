/* GPL-2.0-or-later. Pinned H2 retreat ordering on native VCMI geometry. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "lib/GameLibrary.h"
#include "lib/battle/BattleAction.h"
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
class RetreatDecisionCallback final : public CBattleCallback
{
public:
 explicit RetreatDecisionCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &, const BattleAction & value) override
 {
  if(action) throw std::logic_error("Duplicate retreat action");
  action = value;
 }
 std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
 {
  return std::nullopt;
 }
};
class RetreatEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;
 std::shared_ptr<RetreatDecisionCallback> callback;
public:
 RetreatEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<RetreatDecisionCallback> cb)
  : gameCallback(std::move(game)), callback(std::move(cb)) {}
 const Services * services() const override { return LIBRARY; }
 const GameCb * game() const override { return gameCallback.get(); }
 const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}

class FHeroes2RetreatCenterRing : public BattleTestFixture, public ::testing::WithParamInterface<bool> {};
TEST_P(FHeroes2RetreatCenterRing,orderedSafeTieUsesNativeQueueAndServer)
{
 startGame(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), CreatureID(18).toCreature()));
 ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id, SlotID(0)), CreatureID(16).toCreature()));
 attackerSideHero->setStackCount(SlotID(0), 20); defenderSideHero->setStackCount(SlotID(0), 30);
 startBattle(ETerrainId::SAND); ASSERT_FALSE(HasFatalFailure());
 CStack * actor=nullptr; CStack * first=nullptr;
 for(const auto & unit:battle()->stacks)
  (unit->unitSide()==BattleSide::ATTACKER ? actor:first)=unit.get();
 ASSERT_NE(actor,nullptr); ASSERT_NE(first,nullptr);
 actor->setPosition(BattleHex(115)); first->setPosition(BattleHex(98));
 auto * second=addStack(BattleSide::DEFENDER,CreatureID(16),BattleHex(175),30); ASSERT_NE(second,nullptr);
 if(GetParam()) actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::STACKS_SPEED,BonusSource::OTHER,-1,BonusSourceID()));
 ASSERT_EQ(actor->getMovementRange(),GetParam()?5u:6u);
 ASSERT_EQ(first->getMovementRange(),3u); ASSERT_EQ(second->getMovementRange(),3u);
 ASSERT_FALSE(actor->doubleWide()); ASSERT_FALSE(first->doubleWide()); ASSERT_FALSE(second->doubleWide());
 beginCombat(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_EQ(battle()->activeStack,actor->unitId()) << "Use the real first turn, never overwrite activeStack";
 auto callback=std::make_shared<RetreatDecisionCallback>(actor->unitOwner());callback->onBattleStarted(battle());
 auto view=callback->getBattle(battle()->getBattleID());
 std::vector<battle::Units> order;view->battleGetTurnOrder(order,3,1);
 ASSERT_FALSE(order.empty()); ASSERT_EQ(order.front().size(),3u); EXPECT_EQ(order.front().front(),actor);
 ASSERT_FALSE(view->battleCanShoot(actor));
 const auto reach=view->getReachability(actor);
 if(GetParam()) {EXPECT_GT(reach.distances[76],actor->getMovementRange());}
 else {ASSERT_LE(reach.distances[76],actor->getMovementRange());ASSERT_LE(reach.distances[93],actor->getMovementRange());}
 EXPECT_EQ(BattleHex::getDistance(BattleHex(76),BattleHex(93)),1);
 const auto nearest=[&](int hex){return std::min(BattleHex::getDistance(BattleHex(hex),first->getPosition()),BattleHex::getDistance(BattleHex(hex),second->getPosition()));};
 EXPECT_EQ(nearest(76),nearest(93)); EXPECT_GT(nearest(76),4);
 auto policy=AIFactory::createBattleAI("FHeroes2Tactics"); ASSERT_NE(policy,nullptr);
 auto environment=std::make_shared<RetreatEnvironment>(makeCallback(actor->unitOwner()),callback);
 AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy->playerID=actor->unitOwner();policy->initBattleInterface(environment,callback,prefs);
 policy->battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 const auto health=actor->getAvailableHealth();policy->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(callback->action);
 if(GetParam()) {EXPECT_NE(callback->action->actionType,EActionType::WALK);}
 else {ASSERT_EQ(callback->action->actionType,EActionType::WALK);ASSERT_EQ(callback->action->target.size(),1u);EXPECT_EQ(callback->action->target.front().hexValue,BattleHex(76));}
 EXPECT_EQ(actor->getPosition(),BattleHex(115));EXPECT_EQ(actor->getAvailableHealth(),health);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),actor->unitOwner(),*callback->action));
 if(!GetParam()) {EXPECT_EQ(actor->getPosition(),BattleHex(76));EXPECT_EQ(actor->getAvailableHealth(),health);}
}
INSTANTIATE_TEST_SUITE_P(SpeedGate,FHeroes2RetreatCenterRing,::testing::Bool(),[](const auto & info){return info.param?"InsufficientLead":"SafeTie";});
