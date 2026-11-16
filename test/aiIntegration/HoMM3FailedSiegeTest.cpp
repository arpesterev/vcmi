/* HoMM3 failed-siege live withdrawal tests. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "mock/TinyH3MBuilder.h"
#include "../../AI/Integration/Ports/HoMM3TacticsBattleAI.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/callback/AIFactory.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../lib/battle/BattleLayout.h"
#include <vcmi/Environment.h>
namespace
{
class SiegeDecision final : public CBattleCallback
{
public:
 explicit SiegeDecision(PlayerColor p):CBattleCallback(p,nullptr){}
 std::optional<BattleAction> action;
 void battleMakeUnitAction(const BattleID &,const BattleAction & a) override {ASSERT_FALSE(action);action=a;}
 void battleMakeSpellAction(const BattleID &,const BattleAction & a) override {ASSERT_FALSE(action);action=a;}
};
class SiegeEnvironment final : public Environment
{
 std::shared_ptr<CCallback> gameCallback;std::shared_ptr<SiegeDecision> callback;
public:
 SiegeEnvironment(std::shared_ptr<CCallback> g,std::shared_ptr<SiegeDecision> c):gameCallback(g),callback(c){}
 const Services * services() const override {return LIBRARY;}
 const GameCb * game() const override {return gameCallback.get();}
 const BattleCb * battle(const BattleID & id) const override {return callback->getBattle(id).get();}
};
class HoMM3FailedSiege : public BattleTestFixture
{
protected:
 bool humanPlayer=false;
 void configurePlayer(PlayerSettings & settings) const override
 {if(settings.color==PlayerColor(0) && !humanPlayer)settings.connectedPlayerIDs.clear();}
 CGTownInstance * ownTown=nullptr;
 CStack * active=nullptr;
 CStack * defender=nullptr;
 std::shared_ptr<SiegeDecision> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<SiegeEnvironment> environment;
 void prepare(bool retainCatapult=false,bool humanControlled=false)
 {
  humanPlayer=humanControlled;
  TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
  builder.size(36,false).name("Failed siege")
   .playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
   .hero({5,5,0},HeroTypeID(0),PlayerColor(0)).heroGarrison({{CreatureID(0),20}})
   .hero({7,7,0},HeroTypeID(1),PlayerColor(1)).heroGarrison({{CreatureID(0),20}})
   .town({12,12,0},FactionID::CASTLE,PlayerColor(1))
   .town({22,22,0},FactionID::CASTLE,PlayerColor(0));
  startWithMap(std::move(builder),EMapDifficulty::HARD);ASSERT_FALSE(HasFatalFailure());
  ASSERT_EQ(gameState()->getPlayerState(PlayerColor(0))->isHuman(),humanPlayer);
  server.gameState=gameState();gameHandler=std::make_shared<CGameHandler>(server,gameState());
  gameHandler->randomizer->setSeed(seed);
  attackerSideHero=findHeroByOwner(PlayerColor(0));defenderSideHero=findHeroByOwner(PlayerColor(1));
  ASSERT_NE(attackerSideHero,nullptr);ASSERT_NE(defenderSideHero,nullptr);
  for(auto * town:findAll<CGTownInstance>())if(town->getOwner()==PlayerColor(0))ownTown=town;
  ASSERT_NE(ownTown,nullptr);ownTown->addBuilding(BuildingID::TAVERN);
  CGTownInstance * besieged=nullptr;
  for(auto * town:findAll<CGTownInstance>())if(town->getOwner()==PlayerColor(1))besieged=town;
  ASSERT_NE(besieged,nullptr);
  ASSERT_TRUE(gameHandler->insertNewStack(StackLocation(besieged->id,SlotID(0)),CreatureID(0).toCreature(),20));
  const auto layout=BattleLayout::createDefaultLayout(*gameState(),attackerSideHero,besieged);
  gameHandler->battles->startBattle(attackerSideHero,besieged,besieged->visitablePos(),
   attackerSideHero,nullptr,layout,besieged);
  ASSERT_EQ(gameState()->currentBattles.size(),1u);battle()->obstacles.clear();
  ASSERT_EQ(battle()->tacticDistance,0);
  int catapults=0;
  for(const auto & unit:battle()->stacks)if(unit->unitSide()==BattleSide::ATTACKER)
  {
   if(unit->isCatapult()){++catapults;if(!retainCatapult)injure(unit.get(),unit->getAvailableHealth());}
   else if(unit->creatureId()==CreatureID(0))active=unit.get();
  }
  if(retainCatapult)ASSERT_GT(catapults,0);
  ASSERT_NE(active,nullptr);
  for(const auto & unit:battle()->stacks)
   if(unit->unitSide()==BattleSide::DEFENDER && unit->creatureId()==CreatureID(0))defender=unit.get();
  ASSERT_NE(defender,nullptr);ASSERT_FALSE(defender->isSummoned());defender->setPosition(BattleHex(98));
  battle()->activeStack=active->unitId();
  callback=std::make_shared<SiegeDecision>(attackerSideHero->getOwner());callback->onBattleStarted(battle());
  view=callback->getBattle(battle()->getBattleID());
  environment=std::make_shared<SiegeEnvironment>(makeCallback(attackerSideHero->getOwner()),callback);
  ASSERT_TRUE(view->battleCanFlee());
  ASSERT_FALSE(view->battleIsInsideWalls(active->getPosition()));
  ASSERT_TRUE(view->battleIsInsideWalls(defender->getPosition()));
  if(!retainCatapult && !humanControlled)ASSERT_TRUE(choose());

 }
 std::optional<BattleAction> choose(bool human=false,bool autocombat=false)
 {return AIIntegration::Ports::HoMM3::failedSiegeRetreat(*view,environment->game(),*active,human,autocombat);}
};
TEST_F(HoMM3FailedSiege, controllerRetreatsAndServerAccepts)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(choose());
 auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");ASSERT_NE(policy,nullptr);policy->playerID=attackerSideHero->getOwner();
 policy->initBattleInterface(environment,callback);
 const auto id=battle()->getBattleID();
 policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),
  battle()->getLocation(),attackerSideHero,nullptr,BattleSide::ATTACKER,false);
 policy->activeStack(id,active);
 ASSERT_TRUE(callback->action);EXPECT_EQ(callback->action->actionType,EActionType::RETREAT);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(id,attackerSideHero->getOwner(),*callback->action));
 ASSERT_FALSE(server.battleResults.empty());EXPECT_EQ(server.battleResults.back().result,EBattleResult::ESCAPE);
 EXPECT_EQ(server.battleResults.back().winner,BattleSide::DEFENDER);
}
TEST_F(HoMM3FailedSiege, livingCatapultCanStillBreakWalls)
{prepare(true);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, livingShooterPreventsRetreat)
{prepare();ASSERT_FALSE(HasFatalFailure());active->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOOTER,BonusSource::OTHER,1,BonusSourceID()));active->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOTS,BonusSource::OTHER,12,BonusSourceID()));ASSERT_FALSE(active->isSummoned());ASSERT_TRUE(active->isShooter());EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, livingFlyerPreventsRetreat)
{prepare();ASSERT_FALSE(HasFatalFailure());active->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::FLYING,BonusSource::OTHER,1,BonusSourceID()));ASSERT_FALSE(active->isSummoned());ASSERT_TRUE(active->hasBonusOfType(BonusType::FLYING));EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, openGateProvidesAnEntry)
{prepare();ASSERT_FALSE(HasFatalFailure());battle()->si.gateState=EGateState::OPENED;EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, breachedWallProvidesAnEntry)
{prepare();ASSERT_FALSE(HasFatalFailure());battle()->si.wallState[EWallPart::OVER_GATE]=EWallState::DESTROYED;EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, defendersOutsideCanBeEngaged)
{prepare();ASSERT_FALSE(HasFatalFailure());defender->setPosition(BattleHex(87));EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, townWithoutTavernCannotRehire)
{prepare();ASSERT_FALSE(HasFatalFailure());ownTown->removeBuilding(BuildingID::TAVERN);EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, noOwnedTownCannotRehire)
{prepare();ASSERT_FALSE(HasFatalFailure());gameHandler->setOwner(ownTown,PlayerColor(1));EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, customVictoryIsNotReinterpreted)
{
 prepare();ASSERT_FALSE(HasFatalFailure());TriggeredEvent event;event.effect.type=EventEffect::VICTORY;
 event.trigger=EventExpression(EventCondition(EventCondition::DESTROY));
 gameState()->getMap().triggeredEvents.push_back(event);EXPECT_FALSE(choose());
}
TEST_F(HoMM3FailedSiege, normalDifficultyCoinFlipRemainsUnavailable)
{prepare();ASSERT_FALSE(HasFatalFailure());gameState()->getStartInfo()->difficulty=static_cast<uint8_t>(EMapDifficulty::NORMAL);EXPECT_FALSE(choose());}
TEST_F(HoMM3FailedSiege, humanAndAutocombatNeverWithdraw)
{prepare();ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(choose(true,false));EXPECT_FALSE(choose(false,true));}
TEST_F(HoMM3FailedSiege, nativeFleeBanIsAuthoritative)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 battle()->getSide(BattleSide::DEFENDER).heroID=defenderSideHero->id;
 ASSERT_TRUE(view->battleCanFlee());
 attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::BATTLE_CAN_FLEE,BonusSource::OTHER,-1,BonusSourceID()));
 ASSERT_FALSE(view->battleCanFlee());EXPECT_FALSE(choose());
}
TEST_F(HoMM3FailedSiege, factoryAutocombatPreferencesProtectHumanArmy)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(choose());
 auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");ASSERT_NE(policy,nullptr);
 policy->playerID=attackerSideHero->getOwner();AutocombatPreferences preferences;preferences.enableSpellsUsage=false;
 policy->initBattleInterface(environment,callback,preferences);
 const auto id=battle()->getBattleID();
 policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),
  battle()->getLocation(),attackerSideHero,nullptr,BattleSide::ATTACKER,false);
 policy->activeStack(id,active);
 ASSERT_TRUE(callback->action);EXPECT_NE(callback->action->actionType,EActionType::RETREAT);
}

TEST_F(HoMM3FailedSiege, authoritativeHumanPlayerProtectedWithTwoArgumentInit)
{
 prepare(false,true);ASSERT_FALSE(HasFatalFailure());EXPECT_FALSE(choose());
 auto policy=AIFactory::createBattleAI("HoMM3OriginalTactics");ASSERT_NE(policy,nullptr);
 policy->playerID=attackerSideHero->getOwner();
 policy->initBattleInterface(environment,callback);
 const auto id=battle()->getBattleID();
 policy->battleStart(id,battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),
  battle()->getLocation(),attackerSideHero,nullptr,BattleSide::ATTACKER,false);
 policy->activeStack(id,active);
 ASSERT_TRUE(callback->action);EXPECT_NE(callback->action->actionType,EActionType::RETREAT);
}

TEST_F(HoMM3FailedSiege, ownEquippedShacklesBlockSourceEvenWithoutEnemyHero)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 ASSERT_FALSE(view->battleGetHeroInfo(BattleSide::DEFENDER).details);
 giveArtifact(attackerSideHero,ArtifactID(125),ArtifactPosition::MISC1);
 ASSERT_TRUE(attackerSideHero->hasArt(ArtifactID(125),true));
 ASSERT_TRUE(view->battleCanFlee());
 EXPECT_FALSE(choose());
}
TEST_F(HoMM3FailedSiege, backpackShacklesDoNotBlockSource)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 giveArtifact(attackerSideHero,ArtifactID(125),ArtifactPosition::BACKPACK_START);
 ASSERT_TRUE(attackerSideHero->hasArt(ArtifactID(125)));
 ASSERT_FALSE(attackerSideHero->hasArt(ArtifactID(125),true));
 EXPECT_TRUE(choose());
}

}
