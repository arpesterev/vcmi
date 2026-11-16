/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
// Keep the original-body oracle private to this TU: native ground tests also include it.
#define PinnedForecastPath OwnHeroPinnedForecastPath
#define PinnedGroundTwoPass OwnHeroPinnedGroundTwoPass
#include "homm3GroundTwoPass/PinnedGroundTwoPass.h"
#undef PinnedGroundTwoPass
#undef PinnedForecastPath
#include "../../lib/bonuses/BonusParameters.h"
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
class OwnHeroEventServer : public RecordingGameServer
{
public:
 std::function<void(CPackForClient &)> before,after;
 unsigned packs=0;
 void applyPack(CPackForClient & pack) override
 {if(before)before(pack);RecordingGameServer::applyPack(pack);++packs;if(after)after(pack);}
};
class OwnHeroDecisionCallback : public CBattleCallback
{
public:
 explicit OwnHeroDecisionCallback(PlayerColor player):CBattleCallback(player,nullptr){}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &,const BattleAction & value) override {action=value;}
};
class OwnHeroEnvironment : public Environment
{
 std::shared_ptr<CCallback> gameCb;std::shared_ptr<OwnHeroDecisionCallback> battleCb;
public:
 OwnHeroEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<OwnHeroDecisionCallback> battle):gameCb(std::move(game)),battleCb(std::move(battle)){}
 const Services * services()const override{return LIBRARY;}
 const GameCb * game()const override{return gameCb.get();}
 const BattleCb * battle(const BattleID & id)const override{return battleCb->getBattle(id).get();}
};
class HoMM3OwnHeroGround : public BattleTestFixture
{
protected:
 OwnHeroEventServer events;
 std::unique_ptr<F::NativeMixedHistory> initialHistory;
 std::array<std::unique_ptr<CPlayerBattleCallback>,2> views;
 std::array<std::unique_ptr<F::NativeLifecycle>,2> lifecycle;
 std::vector<CStack *> units;
 std::array<std::array<std::unique_ptr<AIIntegration::HoMM3TacticsBattleAI>,2>,2> controllers;
 std::array<std::array<std::shared_ptr<OwnHeroDecisionCallback>,2>,2> callbacks;
 std::array<std::shared_ptr<OwnHeroEnvironment>,2> environments;
 std::array<std::minstd_rand,2> expectedRandom;
 int controllerSeed=31;
 int heroAttack=3,heroDefense=2,offence=0,armorer=0;
 void startControllers()
 {
  for(int side=0;side<2;++side)for(int variant=0;variant<2;++variant)
  {
   auto callback=std::make_shared<OwnHeroDecisionCallback>(battle()->sideToPlayer(static_cast<BattleSide>(side)));callback->onBattleStarted(battle());callbacks[side][variant]=callback;
   auto env=std::make_shared<OwnHeroEnvironment>(makeCallback(battle()->sideToPlayer(static_cast<BattleSide>(side))),callback);if(!variant)environments[side]=env;
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

 void prepare(int count=2,bool near=false,bool tied=false,bool trackHistory=false)
 {
  startGame();ASSERT_FALSE(HasFatalFailure());events.gameState=gameState();
  gameHandler=std::make_shared<CGameHandler>(events,gameState());gameHandler->randomizer->setSeed(seed);
  if(attackerSideHero->hasSpellbook())attackerSideHero->removeArtifact(ArtifactPosition::SPELLBOOK);
  ASSERT_FALSE(attackerSideHero->hasSpellbook());ASSERT_FALSE(attackerSideHero->canCastSpells());
  attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK,heroAttack,ChangeValueMode::ABSOLUTE);
  attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE,heroDefense,ChangeValueMode::ABSOLUTE);
  attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,3,ChangeValueMode::ABSOLUTE);
  attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE,5,ChangeValueMode::ABSOLUTE);
  attackerSideHero->setSecSkillLevel(SecondarySkill::OFFENCE,offence,ChangeValueMode::ABSOLUTE);
  attackerSideHero->setSecSkillLevel(SecondarySkill::ARMORER,armorer,ChangeValueMode::ABSOLUTE);
  const auto creature=creatureByName("core:pikeman");
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),creature.toCreature()));
  ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id,SlotID(0)),20,ChangeValueMode::ABSOLUTE));
  gameHandler->createWanderingMonster({9,9,0},creatureByName("core:halberdier"),20);
  auto * neutral=const_cast<CGCreature *>(findFirst<CGCreature>());ASSERT_NE(neutral,nullptr);
  std::array<CArmedInstance *,2> armies{attackerSideHero,neutral};
  for(int side=0;side<2;++side)for(int i=1;i<count;++i)
   ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(armies[side]->id,SlotID(i)),(side?creatureByName("core:halberdier"):creature).toCreature(),20+i));
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),armies[0],armies[1]);
  const std::string fieldName="core:sand_shore";
  BattleField field(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),"battlefield",fieldName));
  BattleStart start;start.info=BattleInfo::setupBattle(gameState().get(),int3(4,4,0),ETerrainId::SAND,field,
   {armies[0],armies[1]},{attackerSideHero,nullptr},layout,nullptr);start.battleID=BattleID(0);gameHandler->sendAndApply(start);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->tacticDistance=0;battle()->obstacles.clear();
  for(const auto & unit:battle()->stacks)
  {
   const int side=static_cast<int>(unit->unitSide());
   unit->setPosition(BattleHex((unit->unitSlot().getNum()+1)*34+(side?(near?8:14):(near?7:2))));
   units.push_back(unit.get());
  }
  for(int side=0;side<2;++side)
  {
   views[side]=std::make_unique<CPlayerBattleCallback>(battle(),battle()->sideToPlayer(static_cast<BattleSide>(side)));
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
  if(trackHistory)
  {
   initialHistory=std::make_unique<F::NativeMixedHistory>(*views[0]);
   ASSERT_TRUE(initialHistory->fresh(true));
  }
  beginCombat();ASSERT_FALSE(HasFatalFailure());
  auto own=std::find_if(units.begin(),units.end(),[&](const auto * unit){return unit->unitSide()==BattleSide::ATTACKER && unit->unitSlot()==SlotID(count-1);});ASSERT_NE(own,units.end());
  BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=(*own)->unitId();activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 }
 std::optional<F::OrdinaryLiveMeleeInput> capture(bool known=true)
 {
  const auto * active=views[0]->battleActiveUnit();const int side=static_cast<int>(active->controllingSide());
  return F::captureNativeOrdinaryGround(*views[side],*lifecycle[side],{2,{true,true}},*active,std::vector<int>(21,100),known);
 }
};


TEST_F(HoMM3OwnHeroGround, OwnNoBookHeroHasExactKnownZeroPressureAndControllerAccepts)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 const auto policy=F::captureNativeShooterPolicy(*views[0],environments[0].get());ASSERT_TRUE(policy);
 const auto input=F::captureNativeOrdinaryGround(*views[0],*lifecycle[0],*policy,*active,std::vector<int>(21,100),true);ASSERT_TRUE(input);
 EXPECT_EQ(input->state.heroRangedValue,(std::array<int32_t,2>{0,0}));
 for(const auto & [id,meta]:input->state.ground){(void)id;EXPECT_EQ(meta.enemyAreaLoss,0);}
 const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[0],*lifecycle[0],*policy,*active,true,expectedRandom[0]);ASSERT_TRUE(expected);
 controllers[0][0]->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callbacks[0][0]->action);
 EXPECT_EQ(callbacks[0][0]->action->toString(),expected->action.toString());EXPECT_TRUE(act(*callbacks[0][0]->action));
}
TEST_F(HoMM3OwnHeroGround, OwnBookRemainsUnsupportedEvenWithCurrentManaZero)
{
 prepare();ASSERT_FALSE(HasFatalFailure());giveArtifact(attackerSideHero,ArtifactID(ArtifactID::SPELLBOOK),ArtifactPosition::SPELLBOOK);attackerSideHero->mana=0;
 ASSERT_TRUE(attackerSideHero->hasSpellbook());EXPECT_FALSE(capture());
}
TEST_F(HoMM3OwnHeroGround, WithoutBookArtifactGrantCannotBeMistakenForNoCasting)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 auto grant=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SPELL,BonusSource::ARTIFACT,0,BonusSourceID(ArtifactID(ArtifactID::TITANS_THUNDER)),BonusSubtypeID(SpellID(SpellID::TITANS_LIGHTNING_BOLT)));
 grant->parameters=std::make_shared<BonusParameters>(true);attackerSideHero->addNewBonus(grant);
 ASSERT_FALSE(attackerSideHero->hasSpellbook());ASSERT_TRUE(attackerSideHero->canCastSpells());EXPECT_FALSE(capture());
}
TEST_F(HoMM3OwnHeroGround, OpposingHeroManaZeroDoesNotGrantHiddenKnowledge)
{
 prepare();ASSERT_FALSE(HasFatalFailure());battle()->getSide(BattleSide::DEFENDER).heroID=defenderSideHero->id;defenderSideHero->mana=0;
 EXPECT_EQ(views[0]->battleGetMySide(),BattleSide::ATTACKER);
 EXPECT_FALSE(capture());
}
class HoMM3OwnHeroOracle : public HoMM3OwnHeroGround, public ::testing::WithParamInterface<int> {};
TEST_P(HoMM3OwnHeroOracle, OwnHeroPrimaryAndPassiveSkillsMatchPinnedSourceChooser)
{
 namespace R=OwnHeroPinnedGroundTwoPass;
 const int scenario=GetParam(),count=2,side=0;
 offence=scenario%4;armorer=scenario/4;heroAttack=3+offence;heroDefense=2+armorer;
 prepare(count,scenario%2);ASSERT_FALSE(HasFatalFailure());
 auto it=std::find_if(units.begin(),units.end(),[&](const auto * u){return static_cast<int>(u->unitSide())==side && u->unitSlot().getNum()==count-1;});ASSERT_NE(it,units.end());
 BattleSetActiveStack activate;activate.battleID=battle()->getBattleID();activate.stack=(*it)->unitId();activate.reason=BattleUnitTurnReason::TURN_QUEUE;gameHandler->sendAndApply(activate);
 const auto policy=F::captureNativeShooterPolicy(*views[0],environments[0].get());ASSERT_TRUE(policy);
 const auto input=F::captureNativeOrdinaryGround(*views[0],*lifecycle[0],*policy,**it,std::vector<int>(21,100),true);ASSERT_TRUE(input);const auto actual=F::chooseOrdinaryLiveMelee(*input);ASSERT_TRUE(actual);
 R::manager=R::combatManager{};R::game.m_setup.m_difficulty=policy->difficulty;
 R::manager.IsHuman=policy->human;R::manager.m_actingSide=side;R::manager.m_currentSide=side;
 R::manager.secondPhase=false;R::manager.m_inSecondPhase=false;
 OwnHeroPinnedForecastPath::manager.setupAdjacencyArray();
 for(int hex=0;hex<187;++hex)for(int direction=0;direction<6;++direction)
  R::manager.m_adjacentCells[hex][direction]=OwnHeroPinnedForecastPath::manager.m_adjacentCells[hex][direction];
 R::hero ownHero;ownHero.m_id=0;ownHero.m_skillLevel[R::eSecSkillOffense]=offence;ownHero.m_skillLevel[R::eSecSkillDefense]=armorer;
 R::army * originalActive=nullptr;
 for(const auto * native:units)
 {
  const int owner=static_cast<int>(native->unitSide()),index=native->unitSlot().getNum();
  auto & original=R::manager.m_armies[owner][index];++R::manager.m_numArmies[owner];
  original.id=native->unitId();original.group=owner;original.index=index;original.m_creatureType=owner;
  original.m_numTroops=native->getCount();original.m_monInfo.hitPoints=10;
  EXPECT_EQ(native->getMaxHealth(),10u);original.m_topCreatureDamage=original.m_numTroops*10-native->getAvailableHealth();
  original.m_monInfo.damageLowBound=native->unitType()->getMinDamage(false);original.m_monInfo.damageHighBound=native->unitType()->getMaxDamage(false);
  original.m_monInfo.attackSkill=owner?6:4+heroAttack;original.m_monInfo.defenseSkill=owner?5:5+heroDefense;original.speed=owner?5:4;
  if(owner==0)original.controller=&ownHero;
  EXPECT_EQ(native->getMovementRange(),static_cast<uint32_t>(original.speed));
  R::akCreatureTypeTraits[owner].baseFightValue=native->unitType()->getFightValue();
  R::akCreatureTypeTraits[owner].attackSkill=owner?6:4;R::akCreatureTypeTraits[owner].defenseSkill=5;
  original.m_gridIndex=native->getPosition().toInt();R::manager.m_cells[original.m_gridIndex].value=&original;
  original.retaliates=native->counterAttacks.available()>0;original.m_retaliationCount=native->counterAttacks.available();
  if(native->moved()||native->defended())original.flags|=R::creatureDone;
  if(native->waited())original.flags|=R::creatureWaiting;
  if(native==*it)originalActive=&original;
 }
 ASSERT_NE(originalActive,nullptr);R::draws=std::vector<int>(21,100);R::drawCursor=0;
 R::manager.chooseMeleeAction(originalActive,false,false,side);ASSERT_FALSE(R::manager.trace.empty());
 const auto & expected=R::manager.trace.back();const auto & chosen=actual->choice;
 EXPECT_EQ(chosen.actor,originalActive->id);EXPECT_EQ(static_cast<int>(chosen.order),expected.order);
 EXPECT_EQ(chosen.target,expected.target);EXPECT_EQ(chosen.value,expected.value);
 EXPECT_EQ(chosen.danger,expected.danger);EXPECT_EQ(chosen.dangerousEnemies,expected.marked);
 if(expected.order==2||expected.order==6) { EXPECT_EQ(chosen.destination,expected.destination); }
 if(expected.order==6) { EXPECT_EQ(chosen.attackHex,expected.hex); }
 EXPECT_EQ(actual->drawsConsumed,R::drawCursor);
 for(const auto & [id,damage]:chosen.expectedDamageAfter){(void)id;EXPECT_EQ(damage,0);}
 std::vector<R::army *> order;R::manager.findMoveOrder(&order);ASSERT_FALSE(order.empty());
 if(count>1) { EXPECT_NE(order.front()->id,chosen.actor); }
 const auto decision=F::chooseNativeOrdinaryLiveMelee(*views[side],*lifecycle[side],*policy,**it,true,expectedRandom[side]);
 ASSERT_TRUE(decision);EXPECT_EQ(decision->action.stackNumber,(*it)->unitId());
 controllers[side][0]->activeStack(battle()->getBattleID(),*it);ASSERT_TRUE(callbacks[side][0]->action);
 EXPECT_EQ(callbacks[side][0]->action->toString(),decision->action.toString());EXPECT_TRUE(act(*callbacks[side][0]->action));
}
INSTANTIATE_TEST_SUITE_P(PassiveMasteries,HoMM3OwnHeroOracle,::testing::Range(0,16));

TEST_F(HoMM3OwnHeroGround, OwnHeroControllerSourceBranchDiffersFromLegacyFallback)
{
 prepare(2,false);ASSERT_FALSE(HasFatalFailure());
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 const auto policy=F::captureNativeShooterPolicy(*views[0],environments[0].get());ASSERT_TRUE(policy);
 int differences=0;
 for(int i=0;i<16;++i)
 {
  const auto expected=F::chooseNativeOrdinaryLiveMelee(*views[0],*lifecycle[0],*policy,*active,true,expectedRandom[0]);ASSERT_TRUE(expected);
  controllers[0][0]->activeStack(battle()->getBattleID(),active);controllers[0][1]->activeStack(battle()->getBattleID(),active);
  ASSERT_TRUE(callbacks[0][0]->action);ASSERT_TRUE(callbacks[0][1]->action);
  EXPECT_EQ(callbacks[0][0]->action->toString(),expected->action.toString());
  differences+=callbacks[0][0]->action->toString()!=callbacks[0][1]->action->toString();
 }
 EXPECT_GT(differences,0);
}

TEST_F(HoMM3OwnHeroGround, NoTownNoncastingHeroPreservesSourceHistory)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());
 const auto * game=environments[0]->game();
 ASSERT_TRUE(game->tryGetPlayerState(attackerSideHero->getOwner())->getTowns().empty());
 ASSERT_NE(initialHistory,nullptr);auto & history=*initialHistory;
 auto candidate=history.source();F::MoveOrderInput input;
 const auto snapshot=lifecycle[0]->snapshot();input.actingSide=snapshot.actingSide;input.secondPhase=snapshot.secondPhase;
 for(const auto & unit:snapshot.units)input.units.push_back(unit.order);
 ASSERT_TRUE(candidate.recordSourceOrder(input));
 const auto * active=views[0]->battleActiveUnit();
 ASSERT_TRUE(history.stage(candidate,BattleAction::makeDefend(active)));
 const auto rank=history.source().find(active->unitId())->rank;ASSERT_TRUE(rank);
 history.beforeOwnChoice(game);
 EXPECT_EQ(history.source().find(active->unitId())->rank,rank);
 history.beforeOwnChoice();
 EXPECT_FALSE(history.source().find(active->unitId())->rank);
}
TEST_F(HoMM3OwnHeroGround, SpellbookCannotPreserveHeroHistoryWithoutForecast)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());
 ASSERT_NE(initialHistory,nullptr);auto & history=*initialHistory;
 auto candidate=history.source();F::MoveOrderInput input;
 const auto snapshot=lifecycle[0]->snapshot();input.actingSide=snapshot.actingSide;input.secondPhase=snapshot.secondPhase;
 for(const auto & unit:snapshot.units)input.units.push_back(unit.order);
 ASSERT_TRUE(candidate.recordSourceOrder(input));const auto * active=views[0]->battleActiveUnit();
 ASSERT_TRUE(history.stage(candidate,BattleAction::makeDefend(active)));
 ASSERT_TRUE(history.source().find(active->unitId())->rank);
 giveArtifact(attackerSideHero,ArtifactID(ArtifactID::SPELLBOOK),ArtifactPosition::SPELLBOOK);
 history.beforeOwnChoice(environments[0]->game());
 EXPECT_FALSE(history.source().find(active->unitId())->rank);
}


TEST_F(HoMM3OwnHeroGround, NoBookSpellGrantCannotPreserveSourceHistory)
{
 prepare(2,false,false,true);ASSERT_FALSE(HasFatalFailure());
 ASSERT_NE(initialHistory,nullptr);auto & history=*initialHistory;
 auto candidate=history.source();F::MoveOrderInput input;
 const auto snapshot=lifecycle[0]->snapshot();input.actingSide=snapshot.actingSide;input.secondPhase=snapshot.secondPhase;
 for(const auto & unit:snapshot.units)input.units.push_back(unit.order);
 ASSERT_TRUE(candidate.recordSourceOrder(input));const auto * active=views[0]->battleActiveUnit();
 ASSERT_TRUE(history.stage(candidate,BattleAction::makeDefend(active)));
 ASSERT_TRUE(history.source().find(active->unitId())->rank);
 auto grant=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SPELL,BonusSource::ARTIFACT,0,BonusSourceID(ArtifactID(ArtifactID::TITANS_THUNDER)),BonusSubtypeID(SpellID(SpellID::TITANS_LIGHTNING_BOLT)));
 grant->parameters=std::make_shared<BonusParameters>(true);attackerSideHero->addNewBonus(grant);
 ASSERT_FALSE(attackerSideHero->hasSpellbook());ASSERT_TRUE(attackerSideHero->canCastSpells());
 history.beforeOwnChoice(environments[0]->game());
 EXPECT_FALSE(history.source().find(active->unitId())->rank);
}

// Town ownership alone does not cause the source pre-AI retreat simulation.
class HoMM3OwnHeroTownHistory : public HoMM3OwnHeroGround,
 public ::testing::WithParamInterface<int> {};
TEST_P(HoMM3OwnHeroTownHistory, NoTavernPreservesRanksAndTavernRevokesCertificate)
{
 prepare(2,GetParam()%2,false,true);ASSERT_FALSE(HasFatalFailure());
 auto * town=const_cast<CGTownInstance *>(findFirst<CGTownInstance>());ASSERT_NE(town,nullptr);
 town->removeBuilding(BuildingID::TAVERN);
 gameHandler->setOwner(town,attackerSideHero->getOwner());
 const auto * game=environments[0]->game();
 ASSERT_EQ(game->tryGetPlayerState(attackerSideHero->getOwner())->getTowns().size(),1u);
 ASSERT_FALSE(town->hasBuilt(BuildingID::TAVERN));
 auto & history=*initialHistory;auto candidate=history.source();F::MoveOrderInput order;
 const auto snapshot=lifecycle[0]->snapshot();order.actingSide=snapshot.actingSide;order.secondPhase=snapshot.secondPhase;
 for(const auto & unit:snapshot.units)order.units.push_back(unit.order);
 ASSERT_TRUE(candidate.recordSourceOrder(order));
 const auto * active=dynamic_cast<const CStack *>(views[0]->battleActiveUnit());ASSERT_NE(active,nullptr);
 ASSERT_TRUE(history.stage(candidate,BattleAction::makeDefend(active)));
 const auto rank=history.source().find(active->unitId())->rank;ASSERT_TRUE(rank);
 history.beforeOwnChoice(game);ASSERT_EQ(history.source().find(active->unitId())->rank,rank);
 const auto policy=F::captureNativeShooterPolicy(*views[0],environments[0].get());ASSERT_TRUE(policy);
 // The admitted live source chooser remains executable, not just a history flag.
 auto random=expectedRandom[0];
 const auto expected=F::chooseNativeOrdinaryLiveMixed(*views[0],*lifecycle[0],*policy,*active,true,history,random);
 ASSERT_TRUE(expected);
 controllers[0][0]->activeStack(battle()->getBattleID(),active);ASSERT_TRUE(callbacks[0][0]->action);
 EXPECT_EQ(callbacks[0][0]->action->toString(),expected->action.toString());
 // Revoke in three independent ways: construction, spellbook, native free spell.
 if(GetParam()>=4)
 {
  EXPECT_TRUE(act(*callbacks[0][0]->action));
  return;
 }
 if(GetParam()<2)town->addBuilding(BuildingID::TAVERN);
 else if(GetParam()==2)giveArtifact(attackerSideHero,ArtifactID(ArtifactID::SPELLBOOK),ArtifactPosition::SPELLBOOK);
 else
 {
  auto grant=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SPELL,BonusSource::ARTIFACT,0,
   BonusSourceID(ArtifactID(ArtifactID::TITANS_THUNDER)),BonusSubtypeID(SpellID(SpellID::TITANS_LIGHTNING_BOLT)));
  grant->parameters=std::make_shared<BonusParameters>(true);attackerSideHero->addNewBonus(grant);
 }
 ASSERT_TRUE(history.source().find(active->unitId())->rank);
 history.beforeOwnChoice(game);EXPECT_FALSE(history.source().find(active->unitId())->rank);
}
INSTANTIATE_TEST_SUITE_P(PublicTownCertificate,HoMM3OwnHeroTownHistory,::testing::Range(0,6));

}
