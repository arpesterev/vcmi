/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#define PinnedForecastPath MixedDefendPinnedPath
#define PinnedMixedLive MixedDefendPinnedLive
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
class DefendEventServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient &)> before,after;
 unsigned packs=0;
 void applyPack(CPackForClient & pack) override
 {if(before)before(pack);RecordingGameServer::applyPack(pack);++packs;if(after)after(pack);}
};
class DefendDecisionCallback : public CBattleCallback
{
public:
 explicit DefendDecisionCallback(PlayerColor player):CBattleCallback(player,nullptr){}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &,const BattleAction & value) override {action=value;}
};
class DefendEnvironment : public Environment
{
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<DefendDecisionCallback> battleCb;
public:
 DefendEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<DefendDecisionCallback> battle):gameCb(std::move(game)),battleCb(std::move(battle)){}
 const Services * services()const override{return LIBRARY;}
 const GameCb * game()const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id)const override{return battleCb->getBattle(id).get();}
};
class HoMM3MixedDefend : public BattleTestFixture
{
protected:
 DefendEventServer events;
 std::array<std::unique_ptr<CPlayerBattleCallback>,2> views;
 std::array<std::unique_ptr<F::NativeLifecycle>,2> lifecycle;
 std::vector<CStack *> units;
 std::array<std::array<std::unique_ptr<AIIntegration::HoMM3TacticsBattleAI>,2>,2> controllers;
 std::array<std::array<std::shared_ptr<DefendDecisionCallback>,2>,2> callbacks;
 std::array<std::shared_ptr<DefendEnvironment>,2> environments;
 std::array<std::minstd_rand,2> expectedRandom;
 std::array<std::unique_ptr<F::NativeMixedHistory>,2> histories;
 std::optional<BattleAction> running;
 std::array<std::unique_ptr<AIIntegration::Ports::HoMM3::CurrentDefendSession>,2> stanceSessions;
 bool lowDefense=false;
 bool distinguishingLayout=false;
 int controllerSeed=31;
 void startControllers()
 {
  for(int side=0;side<2;++side)for(int variant=0;variant<2;++variant)
  {
   auto callback=std::make_shared<DefendDecisionCallback>(PlayerColor(side));callback->onBattleStarted(battle());callbacks[side][variant]=callback;
   auto env=std::make_shared<DefendEnvironment>(makeCallback(PlayerColor(side)),callback);if(!variant)environments[side]=env;
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
  if(lethal) ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(armies[0]->id,SlotID(1)),1000,ChangeValueMode::ABSOLUTE));
  if(lowDefense)ASSERT_TRUE(gameHandler->changeStackType(StackLocation(armies[1]->id,SlotID(0)),creatureByName("core:gremlin").toCreature()));
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
   if(distinguishingLayout){const std::array<int,4> positions{42,75,108,141};unit->setPosition(BattleHex(positions.at(side*2+unit->unitSlot().getNum())));}
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
  for(int side=0;side<2;++side)stanceSessions[side]=std::make_unique<AIIntegration::Ports::HoMM3::CurrentDefendSession>(callbacks[side][0],battle()->getBattleID());
  events.before=[this](CPackForClient & pack){
   if(dynamic_cast<BattleAttack *>(&pack)||dynamic_cast<StacksInjured *>(&pack)||dynamic_cast<BattleStackMoved *>(&pack)||dynamic_cast<BattleNextRound *>(&pack))for(auto & x:stanceSessions)x->invalidate();
   if(auto * p=dynamic_cast<StartAction *>(&pack)){running=p->ba;for(auto & x:stanceSessions)x->start(p->ba);for(auto & h:histories)h->start(p->ba);for(auto & l:lifecycle)l->actionStarted(p->battleID,p->ba);both([&](auto & ai){ai.actionStarted(p->battleID,p->ba);});}
   if(auto * p=dynamic_cast<BattleAttack *>(&pack)){for(auto & h:histories){h->attack(*p);h->damage(p->bsa,p->shot());}both([&](auto & ai){ai.battleAttack(p->battleID,p);ai.battleStacksAttacked(p->battleID,p->bsa,p->shot());});}
   if(auto * p=dynamic_cast<StacksInjured *>(&pack)){for(auto & h:histories)h->damage(p->stacks,false);both([&](auto & ai){ai.battleStacksAttacked(p->battleID,p->stacks,false);});}
   if(auto * p=dynamic_cast<BattleStackMoved *>(&pack)){const auto * u=battle()->getStack(p->stack,false);for(auto & h:histories)h->movement(u,p->tilesToMove,p->teleporting);both([&](auto & ai){ai.battleStackMoved(p->battleID,u,p->tilesToMove,p->distance,p->teleporting);});}
   if(auto * p=dynamic_cast<BattleNextRound *>(&pack)){for(auto & h:histories)h->newRound();both([&](auto & ai){ai.battleNewRoundFirst(p->battleID);});}
  };
  events.after=[this](CPackForClient & pack){
   if(auto * p=dynamic_cast<SetStackEffect *>(&pack)){for(auto & x:stanceSessions)x->effects(*p);for(auto & h:histories)h->effects(*p);both([&](auto & ai){ai.battleStacksEffectsSet(p->battleID,*p);});}
   if(auto * p=dynamic_cast<EndAction *>(&pack)){ASSERT_TRUE(running);for(auto & x:stanceSessions)x->finish(*running);for(auto & h:histories)h->finish(*running);both([&](auto & ai){ai.actionFinished(p->battleID,*running);});running.reset();}
   if(auto * round=dynamic_cast<BattleNextRound *>(&pack)) {for(auto & item:lifecycle)item->battleNewRound(round->battleID);both([&](auto & ai){ai.battleNewRound(round->battleID);});}
   if(auto * activation=dynamic_cast<BattleSetActiveStack *>(&pack))if(activation->reason!=BattleUnitTurnReason::AUTOMATIC_ACTION)
    lifecycle.at(static_cast<int>(views[0]->battleActiveUnit()->controllingSide()))->ownActivation(activation->battleID);
  };
  beginCombat();ASSERT_FALSE(HasFatalFailure());
 }
 void comparePinned(const F::OrdinaryLiveMixedInput & input)
 {
  namespace R=MixedDefendPinnedLive;R::manager=R::combatManager{};R::game.m_setup.m_difficulty=input.state.common.difficulty;
  R::manager.IsHuman=input.state.common.human;R::manager.m_actingSide=static_cast<int>(input.state.common.actingSide);R::manager.m_currentSide=static_cast<int>(input.state.currentSide);
  R::manager.secondPhase=R::manager.m_inSecondPhase=input.state.common.secondPhase;MixedDefendPinnedPath::manager.setupAdjacencyArray();
  for(int h=0;h<187;++h)for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=MixedDefendPinnedPath::manager.m_adjacentCells[h][d];
  R::army * active=nullptr;int type=0;
  for(const auto * u:units){const int side=static_cast<int>(u->unitSide()),index=u->unitSlot().getNum();auto & a=R::manager.m_armies[side][index];R::manager.m_numArmies[side]=std::max(R::manager.m_numArmies[side],index+1);
   a.id=u->unitId();a.group=side;a.index=index;a.m_creatureType=type++;a.m_numTroops=u->getCount();a.m_monInfo.hitPoints=u->unitType()->getMaxHealth();a.m_topCreatureDamage=a.m_numTroops*a.m_monInfo.hitPoints-u->getAvailableHealth();
   a.m_monInfo.damageLowBound=u->unitType()->getMinDamage(false);a.m_monInfo.damageHighBound=u->unitType()->getMaxDamage(false);a.m_monInfo.attackSkill=u->unitType()->getAttack(false);a.m_monInfo.defenseSkill=u->unitType()->getDefense(false);a.speed=u->getMovementRange();
   auto & trait=R::akCreatureTypeTraits[a.m_creatureType];trait.baseFightValue=u->unitType()->getFightValue();trait.attackSkill=a.m_monInfo.attackSkill;trait.defenseSkill=a.m_monInfo.defenseSkill;
   if(u->defended())a.m_monInfo.defenseSkill+=std::max(a.m_monInfo.defenseSkill*20/100,1);
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
  if(expected.order==2||expected.order==6||expected.order==7)EXPECT_EQ(actual.destination,expected.destination);if(expected.order==6)EXPECT_EQ(actual.attackHex,expected.hex);
 }
 void activate(const CStack * u){BattleSetActiveStack p;p.battleID=battle()->getBattleID();p.stack=u->unitId();p.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(p);}
 const CStack * unitAt(int side,int slot){return *std::find_if(units.begin(),units.end(),[&](const auto * u){return static_cast<int>(u->unitSide())==side&&u->unitSlot()==SlotID(slot);});}
 void defendThenChoose(int actorSlot,bool near=true)
 {
  prepare(2,near);ASSERT_FALSE(HasFatalFailure());const auto * defender=unitAt(1,0);const auto * actor=unitAt(0,actorSlot);
  const auto baseDefense=defender->getDefense(false);activate(defender);ASSERT_TRUE(act(BattleAction::makeDefend(defender)));
  EXPECT_EQ(defender->getDefense(false),baseDefense+std::max(baseDefense*20/100,1));activate(actor);
  auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);ASSERT_TRUE(histories[0]->ready());
  const auto missing=F::captureNativeOrdinaryMixed(*views[0],*lifecycle[0],{2,{true,true}},*actor,std::vector<int>(21,100),histories[0]->source(),true);EXPECT_FALSE(missing);
  const auto input=F::captureNativeOrdinaryMixed(*views[0],*lifecycle[0],{2,{true,true}},*actor,std::vector<int>(21,100),histories[0]->source(),true,&*witness);ASSERT_TRUE(input);comparePinned(*input);
  for(const auto & [pair,facts]:input->state.melee)if(pair.second==defender->unitId())EXPECT_EQ(facts.defense,baseDefense+std::max(baseDefense*20/100,1));
  auto choice=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*actor,true,*histories[0],expectedRandom[0],&*witness);ASSERT_TRUE(choice);if(!near)EXPECT_EQ(choice->action.actionType,EActionType::SHOOT);
  controllers[0][0]->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(callbacks[0][0]->action);EXPECT_EQ(callbacks[0][0]->action->toString(),choice->action.toString());ASSERT_TRUE(act(choice->action));
 }
};
TEST_F(HoMM3MixedDefend, CanonicalPercentDefendFeedsLiveMeleeControllerAndPinnedSource){defendThenChoose(0);}
TEST_F(HoMM3MixedDefend, CanonicalAddOneDefendFeedsLiveMeleeControllerAndPinnedSource){lowDefense=true;defendThenChoose(0);}
TEST_F(HoMM3MixedDefend, CurrentDefendFeedsArcherLiveControllerAndPinnedSource){defendThenChoose(1,false);}

TEST_F(HoMM3MixedDefend, MissingAndRevokedWitnessRejectWithoutRandomConsumption)
{
 prepare();ASSERT_FALSE(HasFatalFailure());const auto * d=unitAt(1,0);activate(d);ASSERT_TRUE(act(BattleAction::makeDefend(d)));activate(unitAt(0,0));auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);
 const auto before=expectedRandom[0];
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*unitAt(0,0),true,*histories[0],expectedRandom[0]));EXPECT_EQ(before,expectedRandom[0]);
 stanceSessions[0]->invalidate();
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*unitAt(0,0),true,*histories[0],expectedRandom[0],&*witness));EXPECT_EQ(before,expectedRandom[0]);
}
TEST_F(HoMM3MixedDefend, LatestWitnessCannotCertifyTwoDefendedUnits)
{
 prepare();ASSERT_FALSE(HasFatalFailure());for(int slot=0;slot<2;++slot){activate(unitAt(1,slot));ASSERT_TRUE(act(BattleAction::makeDefend(unitAt(1,slot))));}
 activate(unitAt(0,0));auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);const auto before=expectedRandom[0];
 EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*unitAt(0,0),true,*histories[0],expectedRandom[0],&*witness));EXPECT_EQ(before,expectedRandom[0]);
}
TEST_F(HoMM3MixedDefend, RoundTransitionRevokesExistingStanceWitness)
{
 prepare();ASSERT_FALSE(HasFatalFailure());activate(unitAt(1,0));ASSERT_TRUE(act(BattleAction::makeDefend(unitAt(1,0))));auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);
 BattleNextRound round;round.battleID=battle()->getBattleID();gameHandler->sendAndApply(round);activate(unitAt(0,0));EXPECT_FALSE(witness->matchesContext(*views[0]));
 const auto before=expectedRandom[0];EXPECT_FALSE(F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*unitAt(0,0),true,*histories[0],expectedRandom[0],&*witness));EXPECT_EQ(before,expectedRandom[0]);
}
TEST_F(HoMM3MixedDefend, UnpairedInjuryAfterDefendDoesNotLeaveAControllerCertificate)
{
 prepare();ASSERT_FALSE(HasFatalFailure());activate(unitAt(1,0));ASSERT_TRUE(act(BattleAction::makeDefend(unitAt(1,0))));activate(unitAt(0,0));
 auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);stanceSessions[0]->invalidate();histories[0]->damage({},false);
 both([&](auto &ai){ai.battleStacksAttacked(battle()->getBattleID(),{},false);});
 controllers[0][0]->activeStack(battle()->getBattleID(),unitAt(0,0));controllers[0][1]->activeStack(battle()->getBattleID(),unitAt(0,0));ASSERT_TRUE(callbacks[0][0]->action);ASSERT_TRUE(callbacks[0][1]->action);EXPECT_EQ(callbacks[0][0]->action->toString(),callbacks[0][1]->action->toString());
}

TEST_F(HoMM3MixedDefend, DefendWitnessChangesRealControllerRoutingBeyondLegacy)
{
 distinguishingLayout=true;prepare();ASSERT_FALSE(HasFatalFailure());const auto * defender=unitAt(1,0);activate(defender);ASSERT_TRUE(act(BattleAction::makeDefend(defender)));const auto *actor=unitAt(0,0);activate(actor);auto witness=stanceSessions[0]->capture();ASSERT_TRUE(witness);
 int differences=0;
 for(int i=0;i<16;++i){
 auto choice=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],{2,{true,true}},*actor,true,*histories[0],expectedRandom[0],&*witness);ASSERT_TRUE(choice);
 controllers[0][0]->activeStack(battle()->getBattleID(),actor);controllers[0][1]->activeStack(battle()->getBattleID(),actor);ASSERT_TRUE(callbacks[0][0]->action);ASSERT_TRUE(callbacks[0][1]->action);
 EXPECT_EQ(callbacks[0][0]->action->toString(),choice->action.toString());differences+=choice->action.toString()!=callbacks[0][1]->action->toString();
 }
 EXPECT_GT(differences,0);
}
}
