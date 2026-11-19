/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#define PinnedForecastPath NativeMixedPinnedPath
#define PinnedMixedLive NativeMixedPinnedLive
#include "homm3MixedLive/PinnedMixedLive.h"
#undef PinnedMixedLive
#undef PinnedForecastPath
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
class HoMM3MixedNative : public BattleTestFixture
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
 std::array<std::unique_ptr<F::NativeMixedHistory>,2> histories;
 std::optional<BattleAction> running;
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

 void prepare(int count=2,bool near=false,bool tied=false,bool lethal=false)
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
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(armies[0]->id,SlotID(0)),creatureByName("core:halberdier").toCreature()));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(armies[0]->id,SlotID(1)),creatureByName("core:archer").toCreature()));
  if(lethal)
  {
   ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(armies[0]->id,SlotID(1)),1000,ChangeValueMode::ABSOLUTE));
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
  for(int side=0;side<2;++side){histories[side]=std::make_unique<F::NativeMixedHistory>(*views[side]);ASSERT_TRUE(histories[side]->fresh(true));}
  startControllers();
  events.before=[this](CPackForClient & pack){
   if(auto * p=dynamic_cast<StartAction *>(&pack)){running=p->ba;for(auto & h:histories)h->start(p->ba);for(auto & l:lifecycle)l->actionStarted(p->battleID,p->ba);both([&](auto & ai){ai.actionStarted(p->battleID,p->ba);});}
   if(auto * p=dynamic_cast<BattleAttack *>(&pack)){for(auto & h:histories){h->attack(*p);h->damage(p->bsa,p->shot());}both([&](auto & ai){ai.battleAttack(p->battleID,p);ai.battleStacksAttacked(p->battleID,p->bsa,p->shot());});}
   if(auto * p=dynamic_cast<StacksInjured *>(&pack)){for(auto & h:histories)h->damage(p->stacks,false);both([&](auto & ai){ai.battleStacksAttacked(p->battleID,p->stacks,false);});}
   if(auto * p=dynamic_cast<BattleStackMoved *>(&pack)){const auto * u=battle()->getStack(p->stack,false);for(auto & h:histories)h->movement(u,p->tilesToMove,p->teleporting);both([&](auto & ai){ai.battleStackMoved(p->battleID,u,p->tilesToMove,p->distance,p->teleporting);});}
   if(auto * p=dynamic_cast<BattleNextRound *>(&pack)){for(auto & h:histories)h->newRound();both([&](auto & ai){ai.battleNewRoundFirst(p->battleID);});}
  };
  events.after=[this](CPackForClient & pack){
   if(auto * p=dynamic_cast<EndAction *>(&pack)){ASSERT_TRUE(running);for(auto & h:histories)h->finish(*running);both([&](auto & ai){ai.actionFinished(p->battleID,*running);});running.reset();}
   if(auto * round=dynamic_cast<BattleNextRound *>(&pack)) {for(auto & item:lifecycle)item->battleNewRound(round->battleID);both([&](auto & ai){ai.battleNewRound(round->battleID);});}
   if(auto * activation=dynamic_cast<BattleSetActiveStack *>(&pack))if(activation->reason!=BattleUnitTurnReason::AUTOMATIC_ACTION)
    lifecycle.at(static_cast<int>(views[0]->battleActiveUnit()->controllingSide()))->ownActivation(activation->battleID);
  };
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 void comparePinned(const F::OrdinaryLiveMixedInput & input)
 {
  namespace R=NativeMixedPinnedLive;R::manager=R::combatManager{};R::game.m_setup.m_difficulty=input.state.common.difficulty;
  R::manager.IsHuman=input.state.common.human;R::manager.m_actingSide=static_cast<int>(input.state.common.actingSide);R::manager.m_currentSide=static_cast<int>(input.state.currentSide);
  R::manager.secondPhase=R::manager.m_inSecondPhase=input.state.common.secondPhase;NativeMixedPinnedPath::manager.setupAdjacencyArray();
  for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=NativeMixedPinnedPath::manager.m_adjacentCells[h][d];
  R::army * active=nullptr;int type=0;
  for(const auto * u:units){const int side=static_cast<int>(u->unitSide()),index=u->unitSlot().getNum();auto & a=R::manager.m_armies[side][index];R::manager.m_numArmies[side]=std::max(R::manager.m_numArmies[side],index+1);
   a.id=u->unitId();a.group=side;a.index=index;a.m_creatureType=type++;a.m_numTroops=u->getCount();a.m_monInfo.hitPoints=u->unitType()->getMaxHealth();a.m_topCreatureDamage=a.m_numTroops*a.m_monInfo.hitPoints-u->getAvailableHealth();
   a.m_monInfo.damageLowBound=u->unitType()->getMinDamage(false);a.m_monInfo.damageHighBound=u->unitType()->getMaxDamage(false);a.m_monInfo.attackSkill=u->unitType()->getAttack(false);a.m_monInfo.defenseSkill=u->unitType()->getDefense(false);a.speed=u->getMovementRange();
   auto & trait=R::akCreatureTypeTraits[a.m_creatureType];trait.baseFightValue=u->unitType()->getFightValue();trait.attackSkill=a.m_monInfo.attackSkill;trait.defenseSkill=a.m_monInfo.defenseSkill;
   a.m_gridIndex=u->getPosition().toInt();if(u->alive())R::manager.m_cells[a.m_gridIndex].value=&a;else a.flags|=R::creatureImmobilized;
   a.retaliates=u->counterAttacks.available()>0;a.m_retaliationCount=u->counterAttacks.available();if(u->moved()||u->defended())a.flags|=R::creatureDone;if(u->waited())a.flags|=R::creatureWaiting;
   a.shooting=u->alive()&&views[0]->battleCanShoot(u);if(u->hasBonusOfType(BonusType::SHOOTER))a.flags|=R::creatureShootingArmy;if(u->hasBonusOfType(BonusType::NO_MELEE_PENALTY))a.flags|=R::creatureNoMeleePenalty;
   const auto * h=histories[static_cast<int>(input.state.currentSide)]->source().find(u->unitId());ASSERT_NE(h,nullptr);a.m_facing=h->facing.value_or(0);a.m_expectedMoveOrder=h->rank.value_or(0);
   if(u->unitId()==input.activeId)active=&a;
  }
  const auto nativeById=[&](uint32_t id){return *std::find_if(units.begin(),units.end(),[&](const auto * u){return u->unitId()==id;});};
  R::manager.nativeDistancePenalty=[&](const R::army *a,const R::army *b){const auto * u=nativeById(a->id);return views[0]->battleHasDistancePenalty(u,u->getPosition(),nativeById(b->id)->getPosition());};
  R::manager.nativeWallPenalty=[&](const R::army *a,int x,int y){return views[0]->battleHasWallPenalty(nativeById(a->id),BattleHex(x),BattleHex(y));};
  ASSERT_NE(active,nullptr);R::manager.currentArmy=active;R::draws=input.state.randomFactors;R::drawCursor=0;
  const auto result=F::chooseOrdinaryLiveMixed(input);ASSERT_TRUE(result);R::manager.doCompAI(1-R::manager.m_currentSide);ASSERT_FALSE(R::manager.trace.empty());const auto & expected=R::manager.trace.back();const auto & actual=result->choice;
  EXPECT_EQ(actual.actor,expected.actor);EXPECT_EQ(static_cast<int>(actual.order),expected.order);EXPECT_EQ(actual.target,expected.target);EXPECT_EQ(result->drawsConsumed,R::drawCursor);
  if(!active->shooting){EXPECT_EQ(actual.value,expected.value);EXPECT_EQ(actual.danger,expected.danger);EXPECT_EQ(actual.dangerousEnemies,expected.marked);}
  if(expected.order==2||expected.order==6||expected.order==7)
  {
   EXPECT_EQ(actual.destination,expected.destination);
  }
  if(expected.order==6)
  {
   EXPECT_EQ(actual.attackHex,expected.hex);
  }
 }
 std::optional<F::OrdinaryLiveMeleeInput> capture(bool known=true)
 {
  const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
  return F::captureNativeOrdinaryGround(*views[side],*lifecycle[side],{2,{true,true}},*active,std::vector<int>(21,100),known);
 }
};

TEST_F(HoMM3MixedNative, FreshMeleeThenShooterUseActualControllerAndCompleteHistory)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 int shots=0,differences=0;
 for(int turn=0;turn<4;++turn){
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 const int side=static_cast<int>(active->controllingSide());
 auto policy=F::captureNativeShooterPolicy(*views[side],environments[side].get());ASSERT_TRUE(policy);
 histories[side]->beforeOwnChoice();
 const auto captured=F::captureNativeOrdinaryMixed(*views[side],*lifecycle[side],*policy,*active,std::vector<int>(21,100),histories[side]->source(),true);ASSERT_TRUE(captured);comparePinned(*captured);
 auto choice=F::chooseNativeOrdinaryLiveMixed(*views[side],*lifecycle[side],*policy,*active,true,*histories[side],expectedRandom[side]);ASSERT_TRUE(choice)<<turn;
 controllers[side][0]->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callbacks[side][0]->action);
 EXPECT_EQ(callbacks[side][0]->action->toString(),choice->action.toString());
 controllers[side][1]->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callbacks[side][1]->action);differences+=callbacks[side][1]->action->toString()!=choice->action.toString();
 shots+=choice->action.actionType==EActionType::SHOOT;
 ASSERT_TRUE(act(choice->action));EXPECT_TRUE(histories[side]->ready())<<turn;
 }
 EXPECT_GT(shots,0);EXPECT_GT(differences,0);
}

TEST_F(HoMM3MixedNative, RejectedMapAndWrongActorPreserveCandidateRandom)
{
 prepare();ASSERT_FALSE(HasFatalFailure());const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
 const auto policy=F::captureNativeShooterPolicy(*views[side],environments[side].get());ASSERT_TRUE(policy);
 const auto original=expectedRandom[side];
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[side],*lifecycle[side],*policy,*active,false,*histories[side],expectedRandom[side]));
 EXPECT_EQ(original,expectedRandom[side]);
 const auto other=std::find_if(units.begin(),units.end(),[&](const auto * u){return u!=active;});ASSERT_NE(other,units.end());
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[side],*lifecycle[side],*policy,**other,true,*histories[side],expectedRandom[side]));
 EXPECT_EQ(original,expectedRandom[side]);EXPECT_TRUE(histories[side]->ready());
}
TEST_F(HoMM3MixedNative, UnpairedDamageAndInterruptedActionInvalidateFacingHistory)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 histories[0]->damage({},false);EXPECT_FALSE(histories[0]->ready());
 const auto * active=views[1]->battleActiveUnit();
 const auto action=BattleAction::makeWait(active);
 histories[1]->start(action);EXPECT_FALSE(histories[1]->ready());
 histories[1]->newRound();EXPECT_FALSE(histories[1]->source().known());
}

TEST_F(HoMM3MixedNative, OwnMeleeWriterThenOwnShotKillRetainsCorpseRankForNextRound)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());const auto id=battle()->getBattleID();
 const auto activate=[&](const CStack * u){BattleSetActiveStack p;p.battleID=id;p.stack=u->unitId();p.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(p);};
 const auto own=[&](int slot){return *std::find_if(units.begin(),units.end(),[&](const auto * u){return u->unitSide()==BattleSide::ATTACKER&&u->unitSlot()==SlotID(slot);});};
 const auto choose=[&](){const auto * u=views[0]->battleActiveUnit();histories[0]->beforeOwnChoice();auto result=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*u,true,*histories[0],expectedRandom[0]);if(result){controllers[0][0]->activeStack(id,dynamic_cast<const CStack *>(u));EXPECT_TRUE(callbacks[0][0]->action);if(callbacks[0][0]->action){EXPECT_EQ(callbacks[0][0]->action->toString(),result->action.toString());}}return result;};
 activate(own(0));auto melee=choose();ASSERT_TRUE(melee);ASSERT_TRUE(act(melee->action));ASSERT_TRUE(histories[0]->ready());
 std::map<F::UnitId,int> ranks;for(const auto * u:units){const auto * h=histories[0]->source().find(u->unitId());ASSERT_NE(h,nullptr);ASSERT_TRUE(h->rank);ranks[u->unitId()]=*h->rank;}
 activate(own(1));auto shot=choose();ASSERT_TRUE(shot);ASSERT_EQ(shot->action.actionType,EActionType::SHOOT);ASSERT_TRUE(act(shot->action));ASSERT_TRUE(histories[0]->ready());
 const auto dead=std::find_if(units.begin(),units.end(),[](const auto * u){return !u->alive();});ASSERT_NE(dead,units.end());
 const auto * corpse=histories[0]->source().find((*dead)->unitId());ASSERT_NE(corpse,nullptr);ASSERT_TRUE(corpse->rank);EXPECT_EQ(*corpse->rank,ranks.at((*dead)->unitId()));EXPECT_FALSE(corpse->facing);
 BattleNextRound next;next.battleID=id;gameHandler->sendAndApply(next);activate(own(0));
 auto continued=choose();ASSERT_TRUE(continued);EXPECT_EQ(continued->action.stackNumber,own(0)->unitId());
 const auto input=F::captureNativeOrdinaryMixed(*views[0],*lifecycle[0],{2,{true,true}},*own(0),std::vector<int>(21,100),histories[0]->source(),true);ASSERT_TRUE(input);
 comparePinned(*input);EXPECT_EQ(input->state.common.units.size(),units.size());EXPECT_TRUE(act(continued->action));
}

TEST_F(HoMM3MixedNative, ShooterFirstKillDoesNotInventCorpseRank)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());const auto id=battle()->getBattleID();
 const auto own=[&](int slot){return *std::find_if(units.begin(),units.end(),[&](const auto * u){return u->unitSide()==BattleSide::ATTACKER&&u->unitSlot()==SlotID(slot);});};
 const auto activate=[&](const CStack * u){BattleSetActiveStack p;p.battleID=id;p.stack=u->unitId();p.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(p);};
 activate(own(1));histories[0]->beforeOwnChoice();auto shot=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*own(1),true,*histories[0],expectedRandom[0]);ASSERT_TRUE(shot);ASSERT_EQ(shot->action.actionType,EActionType::SHOOT);ASSERT_TRUE(act(shot->action));ASSERT_TRUE(histories[0]->ready());
 const auto dead=std::find_if(units.begin(),units.end(),[](const auto * u){return !u->alive();});ASSERT_NE(dead,units.end());const auto * corpse=histories[0]->source().find((*dead)->unitId());ASSERT_NE(corpse,nullptr);EXPECT_FALSE(corpse->rank);
 BattleNextRound next;next.battleID=id;gameHandler->sendAndApply(next);activate(own(0));const auto rng=expectedRandom[0];
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*own(0),true,*histories[0],expectedRandom[0]));EXPECT_EQ(expectedRandom[0],rng);
}

TEST_F(HoMM3MixedNative, BlockedArcherUsesMeleeWithIntrinsicPenaltyAndPinnedDecision)
{
 prepare(2,true);ASSERT_FALSE(HasFatalFailure());const auto it=std::find_if(units.begin(),units.end(),[](const auto *u){return u->unitSide()==BattleSide::ATTACKER&&u->unitSlot()==SlotID(1);});ASSERT_NE(it,units.end());const auto * active=*it;
 BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=active->unitId();activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 ASSERT_TRUE(active->hasBonusOfType(BonusType::SHOOTER));ASSERT_FALSE(views[0]->battleCanShoot(active));
 const auto input=F::captureNativeOrdinaryMixed(*views[0],*lifecycle[0],{2,{true,true}},*active,std::vector<int>(21,100),histories[0]->source(),true);ASSERT_TRUE(input);comparePinned(*input);
 bool penalty=false;for(const auto & [pair,facts]:input->state.melee)if(pair.first==active->unitId()&&pair.second!=pair.first)penalty|=facts.meleeShooterPenalty;EXPECT_TRUE(penalty);
 const auto choice=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*active,true,*histories[0],expectedRandom[0]);ASSERT_TRUE(choice);EXPECT_NE(choice->action.actionType,EActionType::SHOOT);
 controllers[0][0]->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callbacks[0][0]->action);EXPECT_EQ(callbacks[0][0]->action->toString(),choice->action.toString());ASSERT_TRUE(act(choice->action));EXPECT_TRUE(histories[0]->ready());
}
TEST_F(HoMM3MixedNative, UnstagedManualActionDoesNotManufactureSourceOrderWriter)
{
 prepare();ASSERT_FALSE(HasFatalFailure());const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
 auto input=F::captureNativeOrdinaryMixed(*views[side],*lifecycle[side],{2,{true,true}},*active,std::vector<int>(21,100),histories[side]->source(),true);ASSERT_TRUE(input);
 auto candidate=histories[side]->source();F::MoveOrderInput order;order.actingSide=input->state.common.actingSide;order.secondPhase=input->state.common.secondPhase;for(const auto &u:input->state.common.units)order.units.push_back(u.state.order);ASSERT_TRUE(candidate.recordSourceOrder(order));
 const auto staged=BattleAction::makeDefend(active);ASSERT_TRUE(histories[side]->stage(candidate,staged));
 const auto manual=BattleAction::makeWait(active);ASSERT_TRUE(act(manual));ASSERT_TRUE(histories[side]->ready());
 for(const auto * u:units){const auto * h=histories[side]->source().find(u->unitId());ASSERT_NE(h,nullptr);EXPECT_FALSE(h->rank);EXPECT_TRUE(h->facing);}
}
TEST_F(HoMM3MixedNative, UnknownSpellOrUnitReplacementDisablesControllerSourceBranch)
{
 prepare();ASSERT_FALSE(HasFatalFailure());const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);const int side=static_cast<int>(active->controllingSide());const auto id=battle()->getBattleID();
 controllers[side][0]->battleUnitsChanged(id,{});controllers[side][0]->battleSpellCast(id,nullptr);
 for(int n=0;n<3;++n){controllers[side][0]->activeStack(id,active);controllers[side][1]->activeStack(id,active);ASSERT_TRUE(callbacks[side][0]->action);ASSERT_TRUE(callbacks[side][1]->action);EXPECT_EQ(callbacks[side][0]->action->toString(),callbacks[side][1]->action->toString());}
}
}
