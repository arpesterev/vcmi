/* Standalone native performance observation; GPL-2.0-or-later. */
#include "StdInc.h"
#include <chrono>
#define FHeroes2BattleAI FHeroes2DoubleShotProbeBattleAI
#define FHeroes2SpellPolicy FHeroes2DoubleShotProbeSpellPolicy
#define FHeroes2TacticsContext FHeroes2DoubleShotProbeTacticsContext
#define NativeStrengthModel DoubleShotProbeStrengthModel
#define nativeStrengthFacts shooterProbeStrengthFacts
#define ordinaryNativeThreatUnit shooterProbeThreatUnit
#define ordinaryPursuitThreats shooterProbePursuitThreats
#include <iostream>
#include "server/battles/BattleTestFixture.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/callback/CBattleGameInterface.h"
#include "lib/callback/AIFactory.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/gameState/CGameState.h"
#include "lib/networkPacks/PacksForClientBattle.h"
#include "server/CGameHandler.h"
#include "server/battles/BattleProcessor.h"
#include "AI/Integration/Ports/FHeroes2NativeStrength.h"
#include "AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include <vcmi/Environment.h>
#include "AI/Integration/Ports/FHeroes2BattleAI.h"
#include "AI/Integration/Ports/FHeroes2NativeThreat.h"
#include "fheroesThreat/PinnedThreatFixtureOracle.h"
#include "fheroesStrength/BasicNativeStrengthOracle.h"
#include "lib/bonuses/Limiters.h"
#include "lib/CCreatureHandler.h"
#include "lib/networkPacks/StackLocation.h"
#include "lib/callback/GameRandomizer.h"

namespace
{
class DoubleShotProbeCallback final : public CBattleCallback
{
public:
	explicit DoubleShotProbeCallback(PlayerColor player) : CBattleCallback(player,nullptr) {}
	std::optional<BattleAction> action;
	void battleMakeUnitAction(const BattleID &,const BattleAction & value) override
	{
		if(action) throw std::logic_error("Duplicate probe action");
		action=value;
	}
	void battleMakeSpellAction(const BattleID &,const BattleAction & value) override
	{
		if(action) throw std::logic_error("Duplicate probe spell");
		action=value;
	}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,const BattleStateInfoForRetreat &) override {return std::nullopt;}
};
class DoubleShotProbeEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<DoubleShotProbeCallback> callback;
public:
	DoubleShotProbeEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<DoubleShotProbeCallback> cb):gameCallback(std::move(game)),callback(std::move(cb)){}
	const Services * services() const override {return LIBRARY;}
	const GameCb * game() const override {return gameCallback.get();}
	const BattleCb * battle(const BattleID & id) const override {return callback->getBattle(id).get();}
};
}


namespace
{
class DoubleShotRecorder final : public RecordingGameServer
{
public:
 std::vector<BattleAttack> attacks;
 std::vector<BattleStackMoved> moves;
 void applyPack(CPackForClient & pack) override
 {
  if(const auto * attack=dynamic_cast<const BattleAttack *>(&pack))attacks.push_back(*attack);
  if(const auto * move=dynamic_cast<const BattleStackMoved *>(&pack))moves.push_back(*move);
  RecordingGameServer::applyPack(pack);
 }
};
class AbilityMutation
{
 CCreature * creature;
 std::shared_ptr<Bonus> original;
 std::shared_ptr<Bonus> replacement;
public:
 AbilityMutation(CCreature * type,const std::function<void(Bonus&)> & change):creature(type)
 {
  const auto list=creature->getAllBonuses(Selector::type()(BonusType::ADDITIONAL_ATTACK));
  if(list->size()!=1)throw std::logic_error("Expected canonical single extra attack");
  original=(*list)[0];replacement=std::make_shared<Bonus>(*original);change(*replacement);
  creature->removeBonus(original);creature->addNewBonus(replacement);
 }
 ~AbilityMutation(){creature->removeBonus(replacement);creature->addNewBonus(original);}
};
}
class FHeroes2DoubleShotBattle : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
protected:
 DoubleShotRecorder recorder;
 CStack * shooter=nullptr;
 CStack * ordinary=nullptr;
 std::shared_ptr<DoubleShotProbeCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<DoubleShotProbeEnvironment> environment;
 void prepare(int creature)
 {
  startGame();ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(creature).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
  attackerSideHero->setStackCount(SlotID(0),30);defenderSideHero->setStackCount(SlotID(0),10);
  recorder.gameState=gameState();gameHandler=std::make_shared<CGameHandler>(recorder,gameState());gameHandler->randomizer->setSeed(seed);
  startBattle();ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)(unit->unitSide()==BattleSide::ATTACKER?shooter:ordinary)=unit.get();
  ASSERT_NE(shooter,nullptr);ASSERT_NE(ordinary,nullptr);
  const auto player=shooter->unitOwner();callback=std::make_shared<DoubleShotProbeCallback>(player);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<DoubleShotProbeEnvironment>(makeCallback(player),callback);
 }
 static double oracleStrength(const CStack & unit,bool doubleAttack)
 {
  using namespace FHeroes2StrengthFixture;
  fheroes2::MonsterData data{};auto & s=data.battleStats;const auto * type=unit.unitType();
  s.hp=type->getBaseHitPoints();s.damageMin=type->getBaseDamageMin();s.damageMax=type->getBaseDamageMax();s.speed=type->getBaseSpeed();s.shots=0;
  if(doubleAttack)s.abilities.push_back({fheroes2::MonsterAbilityType::DOUBLE_MELEE_ATTACK});
  s.monsterBaseStrength=getMonsterBaseStrength(data);fheroes2::currentData=data;
  Troop troop;troop.attack=unit.getAttack(false);troop.defense=unit.getDefense(false);troop._count=unit.getCount();return troop.GetStrength();
 }
 static AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit facts(const CStack & unit)
 {
  const auto * t=unit.unitType();return {uint32_t(unit.getCount()),uint32_t(unit.getAvailableHealth()),uint32_t(t->getBaseHitPoints()),uint32_t(t->getBaseDamageMin()),uint32_t(t->getBaseDamageMax()),unit.getAttack(false),unit.getDefense(false),unit.ableToRetaliate()};
 }
 void chooseAndExecute(int ammo)
 {
  prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
  shooter->shots.use(shooter->shots.available()-ammo);
  beginCombat(); ASSERT_FALSE(HasFatalFailure());
  battle()->activeStack=shooter->unitId();
  ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
  AIIntegration::FHeroes2DoubleShotProbeBattleAI policy;
  policy.playerID=shooter->unitOwner(); AutocombatPreferences prefs; prefs.enableSpellsUsage=false;
  policy.initBattleInterface(environment,callback,prefs);
  policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy.activeStack(battle()->getBattleID(),shooter); ASSERT_TRUE(callback->action);
  if(ammo) ASSERT_EQ(callback->action->actionType,EActionType::SHOOT);
  else ASSERT_NE(callback->action->actionType,EActionType::SHOOT);
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),shooter->unitOwner(),*callback->action));
  if(ammo)
  {
   ASSERT_EQ(recorder.attacks.size(),size_t(ammo));
   for(const auto & attack:recorder.attacks) { EXPECT_EQ(attack.stackAttacking,shooter->unitId()); EXPECT_FALSE(attack.counter()); }
   EXPECT_EQ(shooter->shots.available(),0);
  }
  else EXPECT_EQ(shooter->shots.available(),0);
 }
 void rejectWhole()
 {
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*shooter));
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);EXPECT_FALSE(model.usesSourceScale());
  for(const auto & unit:battle()->stacks)EXPECT_DOUBLE_EQ(model.value(*unit),unit->estimateCombatValue());
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}));
 }
};

TEST_P(FHeroes2DoubleShotBattle,ammoDoesNotChangeSourceStrength)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 const auto before=AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).value(*shooter);
 for(const int ammo : {2,1,0})
 {
  shooter->shots.use(shooter->shots.available()-ammo);
  const auto facts=AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*shooter);
  ASSERT_TRUE(facts); EXPECT_TRUE(facts->doubleShooting); EXPECT_FALSE(facts->doubleMelee);
  EXPECT_EQ(shooter->getTotalAttacks(true),2); EXPECT_EQ(shooter->getTotalAttacks(false),1);
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
  ASSERT_TRUE(model.usesSourceScale()); EXPECT_DOUBLE_EQ(model.value(*shooter),before);
 }
}
TEST_P(FHeroes2DoubleShotBattle,additionalLiveAttackRejectsWholeScale)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*shooter));
 auto extra=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ADDITIONAL_ATTACK,BonusSource::OTHER,1,BonusSourceID());
 shooter->addNewBonus(extra); rejectWhole(); shooter->removeBonus(extra);
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*shooter));
}
TEST_P(FHeroes2DoubleShotBattle,malformedCreatureMetadataRejectsWholeScale)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 auto * type=const_cast<CCreature *>(CreatureID(GetParam()).toCreature());
 for(int mutation=0;mutation<4;++mutation)
 {
  SCOPED_TRACE(mutation);
  AbilityMutation change(type,[&](Bonus& b){
   if(mutation==0)b.sid=BonusSourceID(CreatureID(7));
   if(mutation==1)b.effectRange=BonusLimitEffect::NO_LIMIT;
   if(mutation==2)b.valType=BonusValueType::PERCENT_TO_ALL;
   if(mutation==3)b.stacking="noncanonical";
  }); rejectWhole();
 }
}
TEST_P(FHeroes2DoubleShotBattle,controllerExecutesTwoShotsWithTwoAmmo) { chooseAndExecute(2); }
TEST_P(FHeroes2DoubleShotBattle,controllerExecutesOneShotWithOneAmmo) { chooseAndExecute(1); }
TEST_P(FHeroes2DoubleShotBattle,controllerCannotShootWithoutAmmo) { chooseAndExecute(0); }
TEST_P(FHeroes2DoubleShotBattle,meleeHasOneStrikeAndOneCounter)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 ordinary->setPosition(BattleHex(shooter->getPosition().toInt()+1));
 beginCombat(); ASSERT_FALSE(HasFatalFailure()); battle()->activeStack=shooter->unitId();
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 const auto ammo=shooter->shots.available();
 const auto action=BattleAction::makeMeleeAttack(shooter,ordinary,shooter->getPosition());
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),shooter->unitOwner(),action));
 ASSERT_EQ(recorder.attacks.size(),2u);
 EXPECT_EQ(recorder.attacks[0].stackAttacking,shooter->unitId()); EXPECT_FALSE(recorder.attacks[0].counter());
 EXPECT_EQ(recorder.attacks[1].stackAttacking,ordinary->unitId()); EXPECT_TRUE(recorder.attacks[1].counter());
 EXPECT_EQ(shooter->shots.available(),ammo);
}
TEST_P(FHeroes2DoubleShotBattle,unblockedRangedThreatMatchesPinnedBody)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(94));
 for(const int ammo : {2,1})
 {
  shooter->shots.use(shooter->shots.available()-ammo);
  EXPECT_EQ(AIIntegration::Ports::FHeroes2::nativeThreatAttackCount(*shooter,true),ammo);
  ASSERT_TRUE(view->battleCanShoot(shooter)); ASSERT_FALSE(view->battleHasShootingPenalty(shooter,ordinary->getPosition()));
  const auto actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}); ASSERT_TRUE(actual);
  const auto expected=FHeroesThreatFixtureOracle::rangedValue(facts(*shooter),facts(*ordinary));
  EXPECT_EQ(actual->at(shooter->unitId()).potential,expected.potential); EXPECT_EQ(actual->at(shooter->unitId()).retaliation,0u);
 }
 shooter->shots.use(1);
 const auto depleted=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}); ASSERT_TRUE(depleted);
 const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*shooter),facts(*ordinary),false);
 EXPECT_EQ(depleted->at(shooter->unitId()).potential,expected.potential);
 EXPECT_EQ(depleted->at(shooter->unitId()).retaliation,expected.retaliation);
}
TEST_P(FHeroes2DoubleShotBattle,blockedAndLongRangeSourceThreatMatchPinnedBody)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(88));
 const auto blocked=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}); ASSERT_TRUE(blocked);
 const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*shooter),facts(*ordinary),true);
 EXPECT_EQ(blocked->at(shooter->unitId()).potential,expected.potential);
 EXPECT_EQ(blocked->at(shooter->unitId()).retaliation,expected.retaliation);
 ordinary->setPosition(BattleHex(100));
 ASSERT_TRUE(view->battleHasShootingPenalty(shooter,ordinary->getPosition()));
 const auto distant=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter});ASSERT_TRUE(distant);
 const auto ranged=FHeroesThreatFixtureOracle::rangedValue(facts(*shooter),facts(*ordinary));
 EXPECT_EQ(distant->at(shooter->unitId()).potential,ranged.potential);EXPECT_EQ(distant->at(shooter->unitId()).retaliation,0u);
}
TEST_P(FHeroes2DoubleShotBattle,rangedEffectRejectsWholeComparison)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(94));
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}));
 shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}));
}
TEST_P(FHeroes2DoubleShotBattle,ammoCartPreservesSecondThreatShot)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->shots.use(shooter->shots.available());
 ASSERT_FALSE(shooter->shots.canUse(2));
 EXPECT_EQ(AIIntegration::Ports::FHeroes2::nativeThreatAttackCount(*shooter,true),1);
 ASSERT_NE(addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(1,9),1),nullptr);
 ASSERT_TRUE(shooter->shots.canUse(2));
 EXPECT_EQ(AIIntegration::Ports::FHeroes2::nativeThreatAttackCount(*shooter,true),2);
}
TEST_P(FHeroes2DoubleShotBattle,rangedOnlyDefenseRejectsWholeComparison)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(94));
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}));
 auto defense=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,10,BonusSourceID());
 defense->subtype=BonusSubtypeID(PrimarySkill::DEFENSE); defense->effectRange=BonusLimitEffect::ONLY_DISTANCE_FIGHT;
 ordinary->addNewBonus(defense);
 ASSERT_NE(ordinary->getDefense(true),ordinary->getDefense(false));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}));
}
TEST_P(FHeroes2DoubleShotBattle,blockedAndDepletedEndpointsMatchPinnedSource)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(88));
 injure(shooter,27*shooter->getMaxHealth()); ASSERT_EQ(shooter->getCount(),3);
 const auto attack=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,1,BonusSourceID());
 attack->subtype=BonusSubtypeID(PrimarySkill::ATTACK); shooter->addNewBonus(attack);
 const auto nativeBefore=view->battleEstimateDamage(BattleAttackInfo(shooter,ordinary,1,false));
 uint32_t blockedPotential=0;
 for(const bool hasAmmo : {true,false})
 {
  if(!hasAmmo) shooter->shots.use(shooter->shots.available());
  ASSERT_FALSE(view->battleCanShoot(shooter));
  const auto actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{shooter}); ASSERT_TRUE(actual);
  const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*shooter),facts(*ordinary),hasAmmo);
  EXPECT_EQ(actual->at(shooter->unitId()).potential,expected.potential);
  EXPECT_EQ(actual->at(shooter->unitId()).retaliation,expected.retaliation);
  EXPECT_EQ(AIIntegration::Ports::FHeroes2::nativeThreatAttackCount(*shooter,false),1);
  const auto nativeAfter=view->battleEstimateDamage(BattleAttackInfo(shooter,ordinary,1,false));
  EXPECT_EQ(nativeAfter.damage.min,nativeBefore.damage.min);
  EXPECT_EQ(nativeAfter.damage.max,nativeBefore.damage.max);
  if(hasAmmo) blockedPotential=actual->at(shooter->unitId()).potential;
  else EXPECT_GT(actual->at(shooter->unitId()).potential,blockedPotential);
 }
}
TEST_P(FHeroes2DoubleShotBattle,blockedArcherSourceDistanceDiffersFromDepleted)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(88));
 using namespace AIIntegration::Ports::FHeroes2;
 ASSERT_FALSE(view->battleCanShoot(shooter));
 ThreatProjection p{120,0,100,12,4,1};
 p.ranged=nativeThreatRangedDistance(*shooter,false,true);
 ASSERT_TRUE(p.ranged); EXPECT_DOUBLE_EQ(projectRelativeThreat(p),120);
 EXPECT_FALSE(nativeThreatRangedDistance(*shooter,false,false));
 shooter->shots.use(shooter->shots.available());
 p.ranged=nativeThreatRangedDistance(*shooter,false,true);
 ASSERT_FALSE(p.ranged); EXPECT_DOUBLE_EQ(projectRelativeThreat(p),120/4.5);
 ASSERT_NE(addStack(BattleSide::ATTACKER,CreatureID::AMMO_CART,BattleHex(1,9),1),nullptr);
 EXPECT_TRUE(nativeThreatRangedDistance(*shooter,false,true));
}
TEST(FHeroes2ArcherMeleeSource,oddEndpointsAndAttackDefenseCapsMatchPinnedBody)
{
 using namespace AIIntegration::Ports::FHeroes2;
 for(const unsigned count : {1u,3u,7u})
  for(const int attack : {0,5,20,40})
   for(const int defense : {0,5,30})
   {
    const OrdinaryThreatUnit a{count,count*10,10,3,5,attack,5,true};
    const OrdinaryThreatUnit d{100,1000,10,2,3,5,defense,true};
    for(const bool hasAmmo : {true,false})
    {
     const auto expected=FHeroesThreatFixtureOracle::archerMeleeValue(a,d,hasAmmo);
     const auto actual=ordinaryThreatDamage(a,d,hasAmmo); ASSERT_TRUE(actual);
     EXPECT_EQ(actual->potential,expected.potential); EXPECT_EQ(actual->retaliation,expected.retaliation);
    }
   }
}
TEST_P(FHeroes2DoubleShotBattle,archerDefenderRetaliationMatchesPinnedBody)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(94));
 injure(shooter,1);
 for(const bool hasAmmo : {true,false})
 {
  if(!hasAmmo) shooter->shots.use(shooter->shots.available());
  const auto actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*shooter,{ordinary}); ASSERT_TRUE(actual);
  const auto expected=FHeroesThreatFixtureOracle::archerDefenderValue(facts(*ordinary),facts(*shooter),hasAmmo);
  EXPECT_EQ(actual->at(ordinary->unitId()).potential,expected.potential);
  EXPECT_EQ(actual->at(ordinary->unitId()).retaliation,expected.retaliation); EXPECT_GT(expected.retaliation,0u);
 }
 shooter->counterAttacks.use(); ASSERT_FALSE(shooter->ableToRetaliate());
 const auto spent=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*shooter,{ordinary}); ASSERT_TRUE(spent);
 EXPECT_EQ(spent->at(ordinary->unitId()).retaliation,0u);
}
TEST_P(FHeroes2DoubleShotBattle,archerDefenderRangedThreatSuppressesRetaliation)
{
 prepare(GetParam()); ASSERT_FALSE(HasFatalFailure());
 shooter->setPosition(BattleHex(87)); ordinary->setPosition(BattleHex(150));
 auto * enemy=addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(94),10); ASSERT_NE(enemy,nullptr);
 const auto actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*shooter,{ordinary,enemy}); ASSERT_TRUE(actual);
 const auto expected=FHeroesThreatFixtureOracle::archerDefenderValue(facts(*enemy),facts(*shooter),true,true);
 EXPECT_EQ(actual->at(enemy->unitId()).potential,expected.potential); EXPECT_EQ(actual->at(enemy->unitId()).retaliation,0u);
 shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*shooter,{ordinary,enemy}));
}
TEST_P(FHeroes2DoubleShotBattle,activeArcherSiegeWithMeleeEnemyStaysNative)
{
 startGame(); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(GetParam()).toCreature()));
 startSiege(); ASSERT_FALSE(HasFatalFailure());
 CStack * active=nullptr;
 for(const auto & unit:battle()->stacks) if(unit->unitSide()==BattleSide::ATTACKER && unit->creatureId()==CreatureID(GetParam())) active=unit.get();
 ASSERT_NE(active,nullptr);
 auto * enemy=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(5,5),10); ASSERT_NE(enemy,nullptr);
 const auto cb=std::make_shared<DoubleShotProbeCallback>(active->unitOwner()); cb->onBattleStarted(battle());
 const auto siegeView=cb->getBattle(battle()->getBattleID()); ASSERT_NE(siegeView->battleGetDefendedTown(),nullptr);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryNativeThreatUnit(*active,true));
 ASSERT_EQ(active->unitOwner(),siegeView->battleGetOwner(active));
 // An empty comparison isolates the active-archer siege gate from any
 // town-inherited effects on the defending stack. Without that guard it
 // returns an admitted empty map; enemy rejection cannot mask the test.
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*siegeView,*active,{}));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*siegeView,*active,{enemy}));
}
INSTANTIATE_TEST_SUITE_P(Canonical,FHeroes2DoubleShotBattle,::testing::Values(3,19));
