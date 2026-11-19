/* GPL-2.0-or-later. Native forecast-driven withdrawal and conservative boundaries. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../AI/Integration/Ports/HoMM3ForecastRetreat.h"
#define PinnedTwoPass RetreatPinnedTwoPass
#include "homm3TwoPass/PinnedTwoPass.h"
#undef PinnedTwoPass
#define PinnedForecastPath RetreatGroundPinnedPath
#define PinnedGroundTwoPass RetreatGroundPinned
#include "homm3GroundTwoPass/PinnedGroundTwoPass.h"
#undef PinnedGroundTwoPass
#undef PinnedForecastPath
#include "../../lib/callback/CCallback.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../lib/battle/BattleLayout.h"
#include <vcmi/Environment.h>
namespace {
namespace F=HoMM3Forecast;
class ForecastRetreatCallback final : public CBattleCallback
{
public:
 explicit ForecastRetreatCallback(PlayerColor p):CBattleCallback(p,nullptr){}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &,const BattleAction &a) override {ASSERT_FALSE(action);action=a;}
 void battleMakeSpellAction(const BattleID &,const BattleAction &a) override {ASSERT_FALSE(action);action=a;}
};
class ForecastRetreatEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<ForecastRetreatCallback> battleCb;
public:
 ForecastRetreatEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<ForecastRetreatCallback> b):gameCb(g),battleCb(b){}
 const Services * services()const override{return LIBRARY;}
 const GameCb * game()const override{return gameCb.get();}
 const BattleCb * battle(const BattleID &id)const override{return battleCb->getBattle(id).get();}
};
class HoMM3ForecastRetreat : public BattleTestFixture
{
protected:
 bool humanPlayer=false;
 void configurePlayer(PlayerSettings &p) const override {if(p.color==PlayerColor(0)&&!humanPlayer)p.connectedPlayerIDs.clear();}
 CGTownInstance * town=nullptr;CStack *active=nullptr;CStack *enemy=nullptr;
 std::shared_ptr<ForecastRetreatCallback> callback;
 std::shared_ptr<ForecastRetreatEnvironment> environment;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::unique_ptr<F::NativeLifecycle> lifecycle;
 std::shared_ptr<CBattleGameInterface> controller;
 void prepare(bool winning=false,bool humanControlled=false,bool autocombat=false,bool secondOwn=false)
 {
  humanPlayer=humanControlled;startGame();ASSERT_FALSE(HasFatalFailure());
  gameState()->getStartInfo()->difficulty=2;
  attackerSideHero->exp=2000;
  if(attackerSideHero->hasSpellbook())attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
  ASSERT_FALSE(attackerSideHero->canCastSpells());
  town=const_cast<CGTownInstance *>(findFirst<CGTownInstance>());ASSERT_NE(town,nullptr);
  gameHandler->setOwner(town,PlayerColor(0));town->addBuilding(BuildingID::TAVERN);
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),creatureByName("core:archer").toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id,SlotID(0)),winning?200:1,ChangeValueMode::ABSOLUTE));
  if(secondOwn)
  {
   ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(attackerSideHero->id,SlotID(1)),creatureByName("core:masterGremlin").toCreature(),1));
  }
  gameHandler->createWanderingMonster({9,9,0},creatureByName("core:woodElf"),winning?1:40);
  auto *neutral=const_cast<CGCreature *>(findFirst<CGCreature>());ASSERT_NE(neutral,nullptr);
  gameState()->getMap().getTile(neutral->visitablePos()).terrainType=ETerrainId::SAND;
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),attackerSideHero,neutral);
  gameHandler->battles->startBattle(attackerSideHero,neutral,neutral->visitablePos(),attackerSideHero,nullptr,layout,nullptr);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();battle()->tacticDistance=0;
  for(const auto &u:battle()->stacks){if(u->unitSide()==BattleSide::ATTACKER){if(u->unitSlot()==SlotID(0))active=u.get();u->setPosition(BattleHex(87+34*u->unitSlot().getNum()));}else{enemy=u.get();u->setPosition(BattleHex(82));}}
  ASSERT_NE(active,nullptr);ASSERT_NE(enemy,nullptr);ASSERT_FALSE(active->isSummoned());ASSERT_FALSE(enemy->isSummoned());
  const auto id=battle()->getBattleID();callback=std::make_shared<ForecastRetreatCallback>(PlayerColor(0));callback->onBattleStarted(battle());view=callback->getBattle(id);
  environment=std::make_shared<ForecastRetreatEnvironment>(makeCallback(PlayerColor(0)),callback);
  lifecycle=std::make_unique<F::NativeLifecycle>(*view);lifecycle->attach(id);
  controller=AIFactory::createBattleAI("HoMM3OriginalTactics");ASSERT_TRUE(controller);controller->playerID=PlayerColor(0);
  if(autocombat){AutocombatPreferences p;p.enableSpellsUsage=false;controller->initBattleInterface(environment,callback,p);}
  else controller->initBattleInterface(environment,callback);
  controller->battleStart(id,nullptr,nullptr,int3(),attackerSideHero,nullptr,BattleSide::ATTACKER,false);
  battle()->nextRound();lifecycle->battleNewRound(id);controller->battleNewRound(id);
  battle()->activeStack=active->unitId();lifecycle->ownActivation(id);
  ASSERT_TRUE(view->battleCanFlee());
 }
 std::optional<BattleAction> choose(bool human=false,bool autocombat=false)
 {return AIIntegration::Ports::HoMM3::forecastAnnihilationRetreat(*view,environment.get(),*active,*lifecycle,human,autocombat);}
 void baseline(){ASSERT_TRUE(choose());}
};
TEST_F(HoMM3ForecastRetreat, PredictedAnnihilationUsesFactoryAndNativeEscape)
{
 prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());
 const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);
 const auto input=F::captureNativeOrdinaryFacts(*view,*lifecycle,*policy,F::Side::ATTACKER,true);ASSERT_TRUE(input);
 const auto forecast=F::simulateOrdinaryTwoPass(*input);ASSERT_TRUE(forecast);ASSERT_EQ(forecast->passes,2u);
 namespace R=RetreatPinnedTwoPass;
 R::manager=R::combatManager{};R::game.m_setup.m_difficulty=2;R::manager.IsHuman={false,false};
 R::manager.m_actingSide=0;R::manager.secondPhase=false;R::manager.distancePenalty=true;
 std::array<R::hero,2> heroes{};
 for(int side=0;side<2;++side)
 {
  const auto *u=side?enemy:active;auto &a=R::manager.m_armies[side][0];R::manager.m_numArmies[side]=1;
  heroes[side].m_id=side;a.controller=&heroes[side];a.id=u->unitId();a.index=0;a.group=side;a.m_creatureType=side;
  a.shooting=true;a.flags=R::creatureShootingArmy;a.speed=side?6:4;
  const int hp=side?15:10,lo=side?3:2,hi=side?5:3,attack=side?9:6,defense=side?5:3;
  ASSERT_EQ(u->getInitiative(0),a.speed);ASSERT_EQ(u->getAttack(false),attack);ASSERT_EQ(u->getDefense(false),defense);
  a.m_monInfo.hitPoints=hp;a.m_numTroops=u->getCount();a.m_topCreatureDamage=a.m_numTroops*hp-u->getAvailableHealth();
  a.m_monInfo.damageLowBound=lo;a.m_monInfo.damageHighBound=hi;a.m_monInfo.attackSkill=attack;a.m_monInfo.defenseSkill=defense;
  R::akCreatureTypeTraits[side].baseFightValue=u->unitType()->getFightValue();R::akCreatureTypeTraits[side].attackSkill=attack;R::akCreatureTypeTraits[side].defenseSkill=defense;
  a.m_gridIndex=u->getPosition().toInt();R::manager.m_cells[a.m_gridIndex].value=&a;
 }
 R::manager.simulateCombat(0,true);
 EXPECT_EQ(forecast->order,R::manager.fixedOrder);
 EXPECT_EQ(forecast->expectedDamage.at(active->unitId()),R::manager.m_armies[0][0].AI_expected_damage);
 EXPECT_EQ(forecast->expectedDamage.at(enemy->unitId()),R::manager.m_armies[1][0].AI_expected_damage);
 EXPECT_GE(forecast->expectedDamage.at(active->unitId()),active->getAvailableHealth());
 const auto hp=active->getAvailableHealth();const auto shots=active->shots.available();
 controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);EXPECT_EQ(callback->action->actionType,EActionType::RETREAT);
 EXPECT_EQ(active->getAvailableHealth(),hp);EXPECT_EQ(active->shots.available(),shots);
 EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),PlayerColor(0),*callback->action));
 ASSERT_EQ(server.battleResults.size(),1u);EXPECT_EQ(server.battleResults.back().result,EBattleResult::ESCAPE);EXPECT_EQ(server.battleResults.back().winner,BattleSide::DEFENDER);
}
TEST_F(HoMM3ForecastRetreat, SurvivingForecastContinuesCombat)
{prepare(true);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(choose());controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);EXPECT_NE(callback->action->actionType,EActionType::RETREAT);}
TEST_F(HoMM3ForecastRetreat, ExperienceThresholdIsNotGuessedFromArtifacts)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());attackerSideHero->exp=1999;EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, NoOwnedTavernCannotWithdraw)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());town->removeBuilding(BuildingID::TAVERN);EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, NormalRandomGateIsUnsupported)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());gameState()->getStartInfo()->difficulty=1;EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, HumanOwnerProtectedEvenWithTwoArgumentInterface)
{prepare(false,true);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(choose());controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);EXPECT_NE(callback->action->actionType,EActionType::RETREAT);}
TEST_F(HoMM3ForecastRetreat, AutocombatPreferencesProtectUserArmy)
{prepare(false,false,true);ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);EXPECT_NE(callback->action->actionType,EActionType::RETREAT);EXPECT_FALSE(choose(false,true));EXPECT_FALSE(choose(true,false));}
TEST_F(HoMM3ForecastRetreat, EquippedShacklesBlockEvenAgainstNeutral)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());giveArtifact(attackerSideHero,ArtifactID(125),ArtifactPosition::MISC1);ASSERT_TRUE(view->battleCanFlee());EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, OwnSpellbookExcludesUnknownCastForecast)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());giveArtifact(attackerSideHero,ArtifactID(ArtifactID::SPELLBOOK),ArtifactPosition::SPELLBOOK);EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, OpposingHeroCannotBeSilentlyTreatedAsNeutral)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());battle()->getSide(BattleSide::DEFENDER).heroID=defenderSideHero->id;EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, MissingLifecycleCannotDriveWithdrawal)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());F::NativeLifecycle late(*view);EXPECT_FALSE(AIIntegration::Ports::HoMM3::forecastAnnihilationRetreat(*view,environment.get(),*active,late,false,false));}
TEST_F(HoMM3ForecastRetreat, NativeNoBookFreeSpellCannotUseForecast)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());
 auto grant=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SPELL,BonusSource::ARTIFACT,0,
  BonusSourceID(ArtifactID(ArtifactID::TITANS_THUNDER)),BonusSubtypeID(SpellID(SpellID::TITANS_LIGHTNING_BOLT)));
 grant->parameters=std::make_shared<BonusParameters>(true);attackerSideHero->addNewBonus(grant);
 ASSERT_FALSE(attackerSideHero->hasSpellbook());ASSERT_TRUE(attackerSideHero->canCastSpells());EXPECT_FALSE(choose());}
TEST_F(HoMM3ForecastRetreat, DepletedShooterCaptureDoesNotAuthorizeRetreat)
{prepare();ASSERT_FALSE(HasFatalFailure());baseline();ASSERT_FALSE(HasFatalFailure());active->shots.use(active->shots.available());EXPECT_FALSE(choose());}

TEST_F(HoMM3ForecastRetreat, OneHitPointSurvivingAllyPreventsWholeArmyWithdrawal)
{
 prepare(false,false,false,true);ASSERT_FALSE(HasFatalFailure());
 gameState()->players.at(PlayerColor(0)).resources[GameResID::GOLD]=0;
 CStack *ally=nullptr;for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER&&u.get()!=active)ally=u.get();
 ASSERT_NE(ally,nullptr);injure(ally,ally->getAvailableHealth()-1);ASSERT_EQ(ally->getAvailableHealth(),1);
 const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);
 const auto input=F::captureNativeOrdinaryFacts(*view,*lifecycle,*policy,F::Side::ATTACKER,true);ASSERT_TRUE(input);
 const auto forecast=F::simulateOrdinaryTwoPass(*input);ASSERT_TRUE(forecast);
 ASSERT_EQ(forecast->expectedDamage.at(ally->unitId()),0);
 EXPECT_GE(forecast->expectedDamage.at(active->unitId()),active->getAvailableHealth());
 EXPECT_FALSE(choose());
}

class GroundRetreatServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient &)> after;
 void applyPack(CPackForClient &p) override {RecordingGameServer::applyPack(p);if(after)after(p);}
};
class HoMM3MixedForecastRetreat : public HoMM3ForecastRetreat
{
protected:
 GroundRetreatServer events;
 std::unique_ptr<F::NativeMixedHistory> history;
 std::minstd_rand random{73};
 bool distantGround=false;
 void prepareGround(bool winning=false,bool mixedEnemy=false,bool distant=false,int enemyCount=40)
 {
  distantGround=distant;startGame();ASSERT_FALSE(HasFatalFailure());gameState()->getStartInfo()->difficulty=2;
  attackerSideHero->exp=2000;if(attackerSideHero->hasSpellbook())attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
  town=const_cast<CGTownInstance *>(findFirst<CGTownInstance>());ASSERT_NE(town,nullptr);
  gameHandler->setOwner(town,PlayerColor(0));town->addBuilding(BuildingID::TAVERN);
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),creatureByName("core:pikeman").toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id,SlotID(0)),winning?200:1,ChangeValueMode::ABSOLUTE));
  gameHandler->createWanderingMonster({9,9,0},creatureByName("core:halberdier"),winning?1:enemyCount);
  auto *neutral=const_cast<CGCreature *>(findFirst<CGCreature>());ASSERT_NE(neutral,nullptr);
  gameState()->getMap().getTile(neutral->visitablePos()).terrainType=ETerrainId::SAND;
  if(mixedEnemy)
  {
   ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(neutral->id,SlotID(1)),creatureByName("core:woodElf").toCreature(),40));
  }
  events.gameState=gameState();gameHandler=std::make_shared<CGameHandler>(events,gameState());
  events.after=[this](CPackForClient &p)
  {
   if(auto *start=dynamic_cast<BattleStart *>(&p))
   {
    battle()->obstacles.clear();battle()->tacticDistance=0;
    for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::ATTACKER){active=u.get();u->setPosition(BattleHex(distantGround?87:92));}else{if(u->unitSlot()==SlotID(0))enemy=u.get();u->setPosition(BattleHex(distantGround?82:(u->unitSlot()==SlotID(0)?93:82)));}
    ASSERT_NE(active,nullptr);ASSERT_NE(enemy,nullptr);ASSERT_EQ(battle()->getRound(),0);
    callback=std::make_shared<ForecastRetreatCallback>(PlayerColor(0));callback->onBattleStarted(battle());view=callback->getBattle(start->battleID);
    environment=std::make_shared<ForecastRetreatEnvironment>(makeCallback(PlayerColor(0)),callback);
    lifecycle=std::make_unique<F::NativeLifecycle>(*view);lifecycle->attach(start->battleID);
    history=std::make_unique<F::NativeMixedHistory>(*view);ASSERT_TRUE(history->fresh(true));
    controller=AIFactory::createBattleAI("HoMM3OriginalTactics");controller->playerID=PlayerColor(0);controller->initBattleInterface(environment,callback);
    controller->battleStartBefore(start->battleID,nullptr,nullptr,int3(),attackerSideHero,nullptr);
    controller->battleStart(start->battleID,nullptr,nullptr,int3(),attackerSideHero,nullptr,BattleSide::ATTACKER,false);
   }
   if(auto *round=dynamic_cast<BattleNextRound *>(&p)){lifecycle->battleNewRound(round->battleID);controller->battleNewRound(round->battleID);}
  };
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),attackerSideHero,neutral);
  gameHandler->battles->startBattle(attackerSideHero,neutral,neutral->visitablePos(),attackerSideHero,nullptr,layout,nullptr);
  ASSERT_FALSE(HasFatalFailure());ASSERT_NE(history,nullptr);ASSERT_TRUE(history->ready());
  battle()->activeStack=active->unitId();lifecycle->ownActivation(battle()->getBattleID());
 }
 void compareGroundOracleAndRandom(bool retreatExpected)
 {
  std::vector<int> factors;auto expectedRandom=random;std::uniform_int_distribution<int> distribution(75,100);
  std::vector<std::minstd_rand> states{random};for(int i=0;i<1024;++i){factors.push_back(distribution(expectedRandom));states.push_back(expectedRandom);}
  const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);
  const auto captured=F::captureNativeOrdinaryMixed(*view,*lifecycle,*policy,*active,factors,history->source(),true);ASSERT_TRUE(captured);
  auto input=captured->state;input.common.requestedSide=F::Side::ATTACKER;input.common.checkingSurrender=true;
  const auto projected=F::simulateOrdinaryGroundTwoPass(input);ASSERT_TRUE(projected);
  namespace R=RetreatGroundPinned;
  R::manager=R::combatManager{};R::game.m_setup.m_difficulty=2;R::manager.IsHuman={false,false};
  R::manager.m_actingSide=0;R::manager.m_currentSide=0;R::manager.secondPhase=R::manager.m_inSecondPhase=false;
  RetreatGroundPinnedPath::manager.setupAdjacencyArray();
  for(int h=0;h<187;++h){R::manager.m_cells[h].blocked=input.map[h].blocked;for(int d=0;d<6;++d)R::manager.m_adjacentCells[h][d]=RetreatGroundPinnedPath::manager.m_adjacentCells[h][d];}
  R::hero ownHero;ownHero.m_id=0;
  for(int side=0;side<2;++side)
  {
   const auto *u=side?enemy:active;auto &a=R::manager.m_armies[side][0];R::manager.m_numArmies[side]=1;
   a.id=u->unitId();a.group=side;a.index=0;a.m_creatureType=side;a.m_numTroops=u->getCount();a.m_monInfo.hitPoints=10;
   ASSERT_EQ(u->getMaxHealth(),10u);a.m_topCreatureDamage=a.m_numTroops*10-u->getAvailableHealth();
   a.m_monInfo.damageLowBound=side?2:1;a.m_monInfo.damageHighBound=3;a.m_monInfo.attackSkill=side?6:4;a.m_monInfo.defenseSkill=5;a.speed=side?5:4;
   ASSERT_EQ(u->getAttack(false),a.m_monInfo.attackSkill);ASSERT_EQ(u->getDefense(false),5);ASSERT_EQ(u->getMovementRange(),static_cast<unsigned>(a.speed));
   if(!side)a.controller=&ownHero;
   auto &trait=R::akCreatureTypeTraits[side];trait.baseFightValue=u->unitType()->getFightValue();trait.attackSkill=a.m_monInfo.attackSkill;trait.defenseSkill=5;
   a.m_gridIndex=u->getPosition().toInt();R::manager.m_cells[a.m_gridIndex].value=&a;
   a.retaliates=u->counterAttacks.available()>0;a.m_retaliationCount=u->counterAttacks.available();
   a.m_facing=*history->source().find(u->unitId())->facing;
   if(u->moved()||u->defended())
    a.flags|=R::creatureDone;
   if(u->waited())
    a.flags|=R::creatureWaiting;
  }
  R::draws=factors;R::drawCursor=0;R::manager.simulateCombat(0,true);
  EXPECT_EQ(projected->order,R::manager.fixedOrder);EXPECT_EQ(projected->drawsConsumed,R::drawCursor);
  EXPECT_EQ(projected->expectedDamage.at(active->unitId()),R::manager.m_armies[0][0].AI_expected_damage);
  EXPECT_EQ(projected->expectedDamage.at(enemy->unitId()),R::manager.m_armies[1][0].AI_expected_damage);
  EXPECT_EQ(bool(mixed()),retreatExpected);EXPECT_EQ(random,states.at(projected->drawsConsumed));
 }
 std::optional<BattleAction> mixed(bool mapKnown=true)
 {return AIIntegration::Ports::HoMM3::forecastMixedAnnihilationRetreat(*view,environment.get(),*active,*lifecycle,false,false,mapKnown,*history,random);}
};
TEST_F(HoMM3MixedForecastRetreat, AdjacentGroundLossDrivesFactoryEscape)
{
 prepareGround();ASSERT_FALSE(HasFatalFailure());const auto before=random;compareGroundOracleAndRandom(true);ASSERT_FALSE(HasFatalFailure());EXPECT_NE(random,before);
 controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);ASSERT_EQ(callback->action->actionType,EActionType::RETREAT);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),PlayerColor(0),*callback->action));
 ASSERT_EQ(events.battleResults.size(),1u);EXPECT_EQ(events.battleResults.back().result,EBattleResult::ESCAPE);
}
TEST_F(HoMM3MixedForecastRetreat, SurvivingGroundPredictionConsumesSourceDrawsWithoutRetreat)
{
 prepareGround(true);ASSERT_FALSE(HasFatalFailure());const auto before=random;
 compareGroundOracleAndRandom(false);ASSERT_FALSE(HasFatalFailure());EXPECT_NE(random,before);
 auto candidate=history->source();F::MoveOrderInput order;const auto snapshot=lifecycle->snapshot();
 order.actingSide=snapshot.actingSide;order.secondPhase=snapshot.secondPhase;
 for(const auto &u:snapshot.units)order.units.push_back(u.order);
 ASSERT_TRUE(candidate.recordSourceOrder(order));ASSERT_TRUE(history->stage(candidate,BattleAction::makeDefend(active)));
 ASSERT_TRUE(history->source().find(active->unitId())->rank);
 history->beforeOwnChoice(environment->game());EXPECT_FALSE(history->source().find(active->unitId())->rank);
 controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);
 EXPECT_NE(callback->action->actionType,EActionType::RETREAT);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),PlayerColor(0),*callback->action));
}
TEST_F(HoMM3MixedForecastRetreat, UnknownMapRollsBackSourceRandom)
{prepareGround();ASSERT_FALSE(HasFatalFailure());const auto before=random;EXPECT_FALSE(mixed(false));EXPECT_EQ(random,before);EXPECT_TRUE(mixed());}
TEST_F(HoMM3MixedForecastRetreat, UnknownHistoryRollsBackSourceRandom)
{prepareGround();ASSERT_FALSE(HasFatalFailure());history->invalidate();const auto before=random;EXPECT_FALSE(mixed());EXPECT_EQ(random,before);}

TEST_F(HoMM3MixedForecastRetreat, MixedRangedAndMeleeOppositionUsesGroundForecastConsumer)
{
 prepareGround(false,true);ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->stacks.size(),3u);
 bool shooter=false,melee=false;for(const auto &u:battle()->stacks)if(u->unitSide()==BattleSide::DEFENDER){shooter|=u->isShooter();melee|=!u->isShooter();}
 ASSERT_TRUE(shooter);ASSERT_TRUE(melee);ASSERT_TRUE(mixed());
 controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);EXPECT_EQ(callback->action->actionType,EActionType::RETREAT);
}

class HoMM3EconomicForecastRetreat : public HoMM3MixedForecastRetreat
{
protected:
 int reserve=0;
 void prepareEconomic(bool winning=false,int enemyCount=40)
 {
  prepareGround(winning,false,true,enemyCount);ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(attackerSideHero->artifactsInBackpack.empty());
  ASSERT_LE(attackerSideHero->artifactsWorn.size(),1u);
  if(!attackerSideHero->artifactsWorn.empty())
  {
   ASSERT_TRUE(attackerSideHero->hasArt(ArtifactID(ArtifactID::CATAPULT),true));
  }
  reserve=active->getCount()*active->creatureCost()/2+2500;
  gameState()->players.at(PlayerColor(0)).resources[GameResID::GOLD]=reserve;
  const auto policy=F::captureNativeShooterPolicy(*view,environment.get());ASSERT_TRUE(policy);
  const auto input=F::captureNativeOrdinaryMixed(*view,*lifecycle,*policy,*active,std::vector<int>(1024,90),history->source(),true);ASSERT_TRUE(input);
  auto forecastInput=input->state;forecastInput.common.requestedSide=F::Side::ATTACKER;forecastInput.common.checkingSurrender=true;
  const auto forecast=F::simulateOrdinaryGroundTwoPass(forecastInput);ASSERT_TRUE(forecast);
  ASSERT_LT(forecast->expectedDamage.at(active->unitId()),active->getAvailableHealth())<<"must exercise economics, not annihilation";
 }
};
TEST_F(HoMM3EconomicForecastRetreat, ExactReserveWithWeakSurvivingArmyDrivesFactoryEscape)
{
 prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());
 controller->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callback->action);ASSERT_EQ(callback->action->actionType,EActionType::RETREAT);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),PlayerColor(0),*callback->action));
 ASSERT_EQ(events.battleResults.size(),1u);EXPECT_EQ(events.battleResults.back().result,EBattleResult::ESCAPE);
}
TEST_F(HoMM3EconomicForecastRetreat, DefaultCatapultSourceValueChangesEconomicThreshold)
{
 prepareEconomic(false,7);ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(attackerSideHero->hasArt(ArtifactID(ArtifactID::CATAPULT),true));ASSERT_TRUE(mixed());
 attackerSideHero->removeArtifact(attackerSideHero->getArtPos(ArtifactID(ArtifactID::CATAPULT)));
 ASSERT_TRUE(attackerSideHero->artifactsWorn.empty());EXPECT_FALSE(mixed());
}
TEST_F(HoMM3EconomicForecastRetreat, OneGoldBelowReserveContinuesCombat)
{prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());gameState()->players.at(PlayerColor(0)).resources[GameResID::GOLD]=reserve-1;EXPECT_FALSE(mixed());}
TEST_F(HoMM3EconomicForecastRetreat, StrongSurvivingArmyDoesNotWithdrawAtExactReserve)
{prepareEconomic(true);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(mixed());}
TEST_F(HoMM3EconomicForecastRetreat, EquippedArtifactValueMustNotBeGuessed)
{prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());giveArtifact(attackerSideHero,ArtifactID(117),ArtifactPosition::MISC1);EXPECT_FALSE(AIIntegration::Ports::HoMM3::boundedEconomicRetreat(*view,environment->game()));EXPECT_FALSE(mixed());}
TEST_F(HoMM3EconomicForecastRetreat, BackpackArtifactValueMustNotBeGuessed)
{prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());giveArtifact(attackerSideHero,ArtifactID(117),ArtifactPosition::BACKPACK_START);EXPECT_FALSE(AIIntegration::Ports::HoMM3::boundedEconomicRetreat(*view,environment->game()));EXPECT_FALSE(mixed());}
TEST_F(HoMM3EconomicForecastRetreat, DiplomacySurrenderDiscountIsUnsupported)
{prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());attackerSideHero->setSecSkillLevel(SecondarySkill::DIPLOMACY,1,ChangeValueMode::ABSOLUTE);EXPECT_FALSE(mixed());}
TEST_F(HoMM3EconomicForecastRetreat, NativeDiscountCannotBeMistakenForSourceBaseCost)
{prepareEconomic();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(mixed());attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SURRENDER_DISCOUNT,BonusSource::OTHER,10,BonusSourceID()));EXPECT_FALSE(mixed());}

TEST(HoMM3RetreatEconomics, LiteralSourceBoundedArtifactArmMatchesGrid)
{
 namespace P=AIIntegration::Ports::HoMM3;
 // Literal bounded aiCheckRetreat economics: source long is signed32, each
 // weighted stack truncates before accumulation; experience division is integer.
 for(int difficulty:{2,3,4})for(int experience:{2000,199999,200000,1000000})
 for(int own:{1,7,23})for(int enemy:{10,100,1000})for(bool done:{false,true})
 for(bool enough:{false,true})for(int artifactValue:{0,500})
 {
  std::vector<P::RetreatEconomicTroop> troops{{0,own,80,60,done},{0,3,7,5,!done},{1,enemy,115,75,!done}};
  int fightValues[2]{};int cost=0;
  for(const auto &t:troops){int value=t.count*t.fightValue;if(!t.done)value=static_cast<int>(value*1.2);fightValues[t.side]+=value;if(!t.side)cost+=t.count*t.goldCost;}
  cost/=2;fightValues[1]=static_cast<int>(fightValues[1]*1.1);
  float threshold=0.16f;if(artifactValue>0)threshold+=0.04f;threshold-=(4-difficulty)*0.015;
  float experienceBonus=experience/200000;if(experienceBonus>0.03)experienceBonus=0.03f;
  threshold+=experienceBonus;threshold-=0.06f;if(threshold>0.16)threshold=0.16f;
  float ratio=static_cast<float>(fightValues[0])/static_cast<float>(fightValues[0]+fightValues[1]);
  const int gold=cost+2500-(enough?0:1);
  const auto actual=P::sourceBoundedRetreatEconomics(troops,difficulty,experience,gold,artifactValue);ASSERT_TRUE(actual);
  EXPECT_EQ(actual->surrenderCost,cost);EXPECT_EQ(actual->fightValues,(std::array<int,2>{fightValues[0],fightValues[1]}));
  EXPECT_FLOAT_EQ(actual->threshold,threshold);EXPECT_FLOAT_EQ(actual->ratio,ratio);EXPECT_EQ(actual->retreat,enough&&ratio<threshold);
 }
}
TEST(HoMM3RetreatEconomics, UnrepresentableAndUnsupportedScalarsFailClosed)
{
 namespace P=AIIntegration::Ports::HoMM3;
 const std::vector<P::RetreatEconomicTroop> valid{{0,1,80,60,false},{1,40,115,75,false}};
 EXPECT_FALSE(P::sourceBoundedRetreatEconomics(valid,1,2000,10000));
 EXPECT_FALSE(P::sourceBoundedRetreatEconomics(valid,2,1999,10000));
 EXPECT_FALSE(P::sourceBoundedRetreatEconomics(valid,2,2000,-1));
 EXPECT_FALSE(P::sourceBoundedRetreatEconomics(valid,2,2000,10000,11));
 auto bad=valid;bad[0].count=INT32_MAX;EXPECT_FALSE(P::sourceBoundedRetreatEconomics(bad,2,2000,INT64_MAX));
 bad=valid;bad[0].side=2;EXPECT_FALSE(P::sourceBoundedRetreatEconomics(bad,2,2000,10000));
}

}
