/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../lib/callback/CCallback.h"
#include <vcmi/Environment.h>

#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeOrdinaryGround.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../AI/Integration/Ports/HoMM3Forecast/NativeLiveMeleeDecision.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"

namespace
{
namespace F=HoMM3Forecast;
class ControllerEventServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient &)> before,after;
 unsigned packs=0;
 void applyPack(CPackForClient & pack) override
 {if(before)before(pack);RecordingGameServer::applyPack(pack);++packs;if(after)after(pack);}
};
class ControllerDecisionCallback : public CBattleCallback
{
public:
 explicit ControllerDecisionCallback(PlayerColor player):CBattleCallback(player,nullptr){}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &,const BattleAction & value) override {action=value;}
};
class ControllerEnvironment : public Environment
{
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<ControllerDecisionCallback> battleCb;
public:
 ControllerEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<ControllerDecisionCallback> battle):gameCb(std::move(game)),battleCb(std::move(battle)){}
 const Services * services()const override{return LIBRARY;}
 const GameCb * game()const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id)const override{return battleCb->getBattle(id).get();}
};
class HoMM3GroundController : public BattleTestFixture
{
protected:
 ControllerEventServer events;
 std::array<std::unique_ptr<CPlayerBattleCallback>,2> views;
 std::array<std::unique_ptr<F::NativeLifecycle>,2> lifecycle;
 std::vector<CStack *> units;
 std::array<std::array<std::unique_ptr<AIIntegration::HoMM3TacticsBattleAI>,2>,2> controllers;
 std::array<std::array<std::shared_ptr<ControllerDecisionCallback>,2>,2> callbacks;
 std::array<std::shared_ptr<ControllerEnvironment>,2> environments;
 std::array<std::minstd_rand,2> expectedRandom;
 int controllerSeed=31;
 void startControllers()
 {
  for(int side=0;side<2;++side)for(int variant=0;variant<2;++variant)
  {
   auto callback=std::make_shared<ControllerDecisionCallback>(PlayerColor(side));callback->onBattleStarted(battle());callbacks[side][variant]=callback;
   auto env=std::make_shared<ControllerEnvironment>(makeCallback(PlayerColor(side)),callback);if(!variant)environments[side]=env;
   auto controller=std::make_unique<AIIntegration::HoMM3TacticsBattleAI>();AutocombatPreferences prefs;prefs.enableSpellsUsage=false;
   controller->initBattleInterface(env,callback,prefs);
   if(!variant)controller->battleStartBefore(battle()->getBattleID(),nullptr,nullptr,int3(),nullptr,nullptr);
   CRandomGenerator parallel;parallel.setSeed(controllerSeed+side);expectedRandom[side].seed(static_cast<std::minstd_rand::result_type>(parallel.nextInt()));
   CRandomGenerator::getDefault().setSeed(controllerSeed+side);
   controller->battleStart(battle()->getBattleID(),nullptr,nullptr,int3(),nullptr,nullptr,static_cast<BattleSide>(side),false);
   controllers[side][variant]=std::move(controller);
  }
 }
 void both(const std::function<void(AIIntegration::HoMM3TacticsBattleAI &)> & f)
 {for(auto & side:controllers)for(auto & controller:side)if(controller)f(*controller);}

 void prepare(int count=2,bool near=false,bool tied=false)
 {
  startGame();ASSERT_FALSE(HasFatalFailure());events.gameState=gameState();
  gameHandler=std::make_shared<CGameHandler>(events,gameState());gameHandler->randomizer->setSeed(seed);
  const auto creature=creatureByName("core:pikeman");
  gameHandler->createWanderingMonster({8,8,0},creature,20);
  gameHandler->createWanderingMonster({9,9,0},tied?creature:creatureByName("core:halberdier"),20);
  std::vector<CGCreature *> armies;
  for(const auto & object:map()->objects) if(auto * monster=dynamic_cast<CGCreature *>(object.get())) armies.push_back(monster);
  ASSERT_EQ(armies.size(),2u);
  for(int side=0;side<2;++side)
  {
   armies[side]->setOwner(PlayerColor(side));
   for(int i=1;i<count;++i) ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(armies[side]->id,SlotID(i)),(side&&!tied?creatureByName("core:halberdier"):creature).toCreature(),20+i));
  }
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),armies[0],armies[1]);
  const std::string fieldName="core:sand_shore";
  BattleField field(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),"battlefield",fieldName));
  BattleStart start;start.info=BattleInfo::setupBattle(gameState().get(),int3(4,4,0),ETerrainId::SAND,field,
   {armies[0],armies[1]},{nullptr,nullptr},layout,nullptr);start.battleID=BattleID(0);gameHandler->sendAndApply(start);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->tacticDistance=0;battle()->obstacles.clear();
  for(const auto & unit:battle()->stacks)
  {
   const int side=static_cast<int>(unit->unitSide());
   unit->setPosition(BattleHex((unit->unitSlot().getNum()+1)*34+(side?(near?8:14):(near?7:2))));
   units.push_back(unit.get());
  }
  for(int side=0;side<2;++side)
  {
   views[side]=std::make_unique<CPlayerBattleCallback>(battle(),PlayerColor(side));
   lifecycle[side]=std::make_unique<F::NativeLifecycle>(*views[side]);
   lifecycle[side]->attach(battle()->getBattleID());
  }
  startControllers();
  events.before=[this](CPackForClient & pack){if(auto * action=dynamic_cast<StartAction *>(&pack))  {for(auto & item:lifecycle)item->actionStarted(action->battleID,action->ba);both([&](auto & ai){ai.actionStarted(action->battleID,action->ba);});}};
  events.after=[this](CPackForClient & pack){
   if(auto * round=dynamic_cast<BattleNextRound *>(&pack)) {for(auto & item:lifecycle)item->battleNewRound(round->battleID);both([&](auto & ai){ai.battleNewRound(round->battleID);});}
   if(auto * activation=dynamic_cast<BattleSetActiveStack *>(&pack))if(activation->reason!=BattleUnitTurnReason::AUTOMATIC_ACTION)
    lifecycle.at(static_cast<int>(views[0]->battleActiveUnit()->controllingSide()))->ownActivation(activation->battleID);
  };
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 std::optional<F::OrdinaryLiveMeleeInput> capture(bool known=true)
 {
  const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
  return F::captureNativeOrdinaryGround(*views[side],*lifecycle[side],{2,{true,true}},*active,std::vector<int>(21,100),known);
 }
};

TEST_F(HoMM3GroundController, RoutesLiveSourceDecisionAndRetainsExplicitLegacyFallback)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());
 int differences=0;
 for(int seedValue=1;seedValue<=16;++seedValue)
 {
  const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
  const int side=static_cast<int>(active->controllingSide());
  const auto policy=F::captureNativeShooterPolicy(*views[side],environments[side].get());ASSERT_TRUE(policy);
  auto rng=expectedRandom[side];
  const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],*policy,*active,true,rng);ASSERT_TRUE(expected);
  controllers[side][0]->activeStack(battle()->getBattleID(),active);
  controllers[side][1]->activeStack(battle()->getBattleID(),active);
  ASSERT_TRUE(callbacks[side][0]->action);ASSERT_TRUE(callbacks[side][1]->action);
  EXPECT_EQ(callbacks[side][0]->action->toString(),expected->action.toString());
  differences+=callbacks[side][0]->action->toString()!=callbacks[side][1]->action->toString();
  expectedRandom[side]=rng;
 }
 EXPECT_GT(differences,0)<<"fixture must distinguish source dispatch from legacy fallback";
}
TEST_F(HoMM3GroundController, UnsupportedHeroActivationFallsBackWithoutConsumingFutureSourceSequence)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 const int side=static_cast<int>(active->controllingSide());const auto id=battle()->getBattleID();
 const auto oldHero=battle()->getSide(BattleSide::ATTACKER).heroID;
 battle()->getSide(BattleSide::ATTACKER).heroID=attackerSideHero->id;
 for(int i=0;i<8;++i)
 {
  controllers[side][0]->activeStack(id,active);controllers[side][1]->activeStack(id,active);
  ASSERT_TRUE(callbacks[side][0]->action);ASSERT_TRUE(callbacks[side][1]->action);
  EXPECT_EQ(callbacks[side][0]->action->toString(),callbacks[side][1]->action->toString());
 }
 battle()->getSide(BattleSide::ATTACKER).heroID=oldHero;
 const auto policy=F::captureNativeShooterPolicy(*views[side],environments[side].get());ASSERT_TRUE(policy);
 for(int i=0;i<16;++i)
 {
  const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],*policy,*active,true,expectedRandom[side]);ASSERT_TRUE(expected);
  controllers[side][0]->activeStack(id,active);ASSERT_TRUE(callbacks[side][0]->action);
  EXPECT_EQ(callbacks[side][0]->action->toString(),expected->action.toString());
 }
}
TEST_F(HoMM3GroundController, NativeExecutionThenNewRoundRetainsActualActorSourceRouting)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());const auto id=battle()->getBattleID();
 const auto * first=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(first,nullptr);
 const int firstSide=static_cast<int>(first->controllingSide());
 const auto firstPolicy=F::captureNativeShooterPolicy(*views[firstSide],environments[firstSide].get());ASSERT_TRUE(firstPolicy);
 const auto expectedFirst=F::chooseNativeOrdinaryLiveMelee(*views[firstSide],*lifecycle[firstSide],*firstPolicy,*first,true,expectedRandom[firstSide]);ASSERT_TRUE(expectedFirst);
 controllers[firstSide][0]->activeStack(id,first);ASSERT_TRUE(callbacks[firstSide][0]->action);
 EXPECT_EQ(callbacks[firstSide][0]->action->toString(),expectedFirst->action.toString());ASSERT_TRUE(act(*callbacks[firstSide][0]->action));
 both([&](auto & ai){ai.battleNewRoundFirst(id);});
 BattleNextRound next;next.battleID=id;gameHandler->sendAndApply(next);
 auto it=std::find_if(units.begin(),units.end(),[](const auto * u){return u->unitSide()==BattleSide::DEFENDER && u->unitSlot()==SlotID(1);});ASSERT_NE(it,units.end());
 BattleSetActiveStack activation;activation.battleID=id;activation.stack=(*it)->unitId();activation.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activation);
 const auto policy=F::captureNativeShooterPolicy(*views[1],environments[1].get());ASSERT_TRUE(policy);
 const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[1],*lifecycle[1],*policy,**it,true,expectedRandom[1]);ASSERT_TRUE(expected);
 controllers[1][0]->activeStack(id,*it);ASSERT_TRUE(callbacks[1][0]->action);
 EXPECT_EQ(callbacks[1][0]->action->stackNumber,(*it)->unitId());EXPECT_EQ(callbacks[1][0]->action->toString(),expected->action.toString());
 EXPECT_TRUE(act(*callbacks[1][0]->action));
}
TEST_F(HoMM3GroundController, InvalidatedMapRoutesOnlyExistingFallback)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 const int side=static_cast<int>(active->controllingSide());const auto id=battle()->getBattleID();
 controllers[side][0]->battleObstaclesChanged(id,{});
 for(int i=0;i<8;++i)
 {
  controllers[side][0]->activeStack(id,active);controllers[side][1]->activeStack(id,active);
  ASSERT_TRUE(callbacks[side][0]->action);ASSERT_TRUE(callbacks[side][1]->action);
  EXPECT_EQ(callbacks[side][0]->action->toString(),callbacks[side][1]->action->toString());
 }
}

}

TEST_F(HoMM3GroundController, MirroredTieRoutesInvariantSourceForActualNonfirstActor)
{
 prepare(2,false,true);ASSERT_FALSE(HasFatalFailure());
 const auto target=std::find_if(units.begin(),units.end(),[](const auto *u){return u->unitSide()==BattleSide::ATTACKER&&u->unitSlot().getNum()==1;});ASSERT_NE(target,units.end());
 BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=(*target)->unitId();activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);ASSERT_EQ(active,*target);
 const auto policy=F::captureNativeShooterPolicy(*views[0],environments[0].get());ASSERT_TRUE(policy);
 const auto input=F::captureNativeOrdinaryGround(*views[0],*lifecycle[0],*policy,*active,std::vector<int>(21,100),true);ASSERT_TRUE(input);
 F::MoveOrderInput order;order.actingSide=input->state.common.actingSide;order.secondPhase=input->state.common.secondPhase;for(const auto &u:input->state.common.units)order.units.push_back(u.state.order);
 const auto projected=F::projectInvariantMoveOrder(order);ASSERT_TRUE(projected);ASSERT_FALSE(projected->units.empty());EXPECT_NE(projected->units.front().id,active->unitId());EXPECT_TRUE(projected->containsComparatorTies);
 int differsFromLegacy=0;
 for(int i=0;i<16;++i)
 {
  auto random=expectedRandom[0];const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[0],*lifecycle[0],*policy,*active,true,random);ASSERT_TRUE(expected);
  controllers[0][0]->activeStack(battle()->getBattleID(),active);controllers[0][1]->activeStack(battle()->getBattleID(),active);
  ASSERT_TRUE(callbacks[0][0]->action);ASSERT_TRUE(callbacks[0][1]->action);EXPECT_EQ(callbacks[0][0]->action->toString(),expected->action.toString());
  differsFromLegacy+=callbacks[0][0]->action->toString()!=callbacks[0][1]->action->toString();expectedRandom[0]=random;
 }
 EXPECT_GT(differsFromLegacy,0)<<"new source branch must be distinguishable from legacy fallback";
 ASSERT_TRUE(callbacks[0][0]->action);EXPECT_TRUE(act(*callbacks[0][0]->action));
}
