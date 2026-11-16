/* Standalone native performance observation; GPL-2.0-or-later. */
#include "StdInc.h"
#include <chrono>
#define FHeroes2BattleAI FHeroes2ThreatCandidateBattleAI
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
#include "lib/networkPacks/StackLocation.h"
#include "server/battles/BattleProcessor.h"
#include "AI/Integration/Ports/FHeroes2NativeStrength.h"
#include "AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include <vcmi/Environment.h>
#ifdef FHER_THREAT_BASELINE
#include "../../../evidence/BaselineFHeroes2BattleAI.h"
#else
#include "AI/Integration/Ports/FHeroes2BattleAI.h"
#endif
#include "AI/Integration/Ports/FHeroes2NativeThreat.h"
#include "fheroesThreat/PinnedThreatFixtureOracle.h"

namespace
{
class ProbeCallback final : public CBattleCallback
{
public:
	explicit ProbeCallback(PlayerColor player) : CBattleCallback(player,nullptr) {}
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
class ProbeEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<ProbeCallback> callback;
public:
	ProbeEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<ProbeCallback> cb):gameCallback(std::move(game)),callback(std::move(cb)){}
	const Services * services() const override {return LIBRARY;}
	const GameCb * game() const override {return gameCallback.get();}
	const BattleCb * battle(const BattleID & id) const override {return callback->getBattle(id).get();}
};
}


class FHeroes2NativeThreatBattle : public BattleTestFixture
{
protected:
 CStack * active=nullptr;
 CStack * strong=nullptr;
 CStack * ordinary=nullptr;
 std::shared_ptr<ProbeCallback> cb;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<ProbeEnvironment> env;
 void prepare(bool ranged=false,bool activeRanged=false)
 {
  startGame(); ASSERT_FALSE(HasFatalFailure());
  if(activeRanged) ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(2).toCreature()));
  if(ranged) ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(2).toCreature()));
  attackerSideHero->setStackCount(SlotID(0),1000);defenderSideHero->setStackCount(SlotID(0),10);
  startBattle(); ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)
   (unit->unitSide()==BattleSide::ATTACKER?active:strong)=unit.get();
  ASSERT_NE(active,nullptr);ASSERT_NE(strong,nullptr);
  for(auto * u:{active,strong}) {BattleStackMoved p;p.battleID=battle()->getBattleID();p.stack=u->unitId();p.teleporting=true;p.tilesToMove.insert(BattleHex(ranged?(u==active?93:36):(u==active?87:48)));server.applyPack(p);}
  ordinary=addStack(BattleSide::DEFENDER,CreatureID(ranged?2:0),BattleHex(ranged?138:150),23);ASSERT_NE(ordinary,nullptr);
  auto bonus=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,20,BonusSourceID());bonus->subtype=BonusSubtypeID(PrimarySkill::ATTACK);strong->addNewBonus(bonus);
  const auto player=active->unitOwner();cb=std::make_shared<ProbeCallback>(player);cb->onBattleStarted(battle());view=cb->getBattle(battle()->getBattleID());env=std::make_shared<ProbeEnvironment>(makeCallback(player),cb);
 }
 static AIIntegration::Ports::FHeroes2::OrdinaryThreatUnit facts(const CStack & unit)
 {
  const auto * type=unit.unitType();return {static_cast<uint32_t>(unit.getCount()),static_cast<uint32_t>(unit.getAvailableHealth()),static_cast<uint32_t>(type->getBaseHitPoints()),static_cast<uint32_t>(type->getBaseDamageMin()),static_cast<uint32_t>(type->getBaseDamageMax()),unit.getAttack(false),unit.getDefense(false),unit.ableToRetaliate()};
 }
 void archerMeleePursuit(bool blocked,bool fallback)
 {
  prepare(true); ASSERT_FALSE(HasFatalFailure());
  if(blocked)
  {
   ASSERT_NE(addStack(BattleSide::ATTACKER,CreatureID(0),BattleHex(37),1),nullptr);
   ASSERT_NE(addStack(BattleSide::ATTACKER,CreatureID(0),BattleHex(139),1),nullptr);
  }
  else for(auto * unit : {strong,ordinary}) unit->shots.use(unit->shots.available());
  ASSERT_FALSE(view->battleCanShoot(strong)); ASSERT_FALSE(view->battleCanShoot(ordinary));
  const auto a=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*strong),facts(*active),blocked);
  const auto b=FHeroesThreatFixtureOracle::archerMeleeValue(facts(*ordinary),facts(*active),blocked);
  ASSERT_GT(a.potential,b.potential);
  const auto native=[&](const CStack * u){const auto d=view->battleEstimateDamage(BattleAttackInfo(u,active,7,false));return (double(d.damage.min)+d.damage.max)/2;};
  ASSERT_LT(native(strong),native(ordinary));
  const auto routes=view->getReachability(active);
  const auto shortest=[&](const CStack * unit){int best=999;for(const auto & hex:unit->getAttackableHexes(active))if(hex.isValid()&&view->isMeleeAttackPossible(active,unit,hex))best=std::min(best,int(routes.distances[hex.toInt()]));return best;};
  ASSERT_EQ(shortest(strong),shortest(ordinary));
  if(fallback) ordinary->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary}); ASSERT_EQ(bool(source),!fallback);
  if(source){EXPECT_EQ(source->at(strong->unitId()).potential,a.potential);EXPECT_EQ(source->at(ordinary->unitId()).potential,b.potential);}
  beginCombat(); ASSERT_FALSE(HasFatalFailure()); battle()->activeStack=active->unitId();
  AIIntegration::FHeroes2ThreatCandidateBattleAI policy; policy.playerID=active->unitOwner(); AutocombatPreferences prefs; prefs.enableSpellsUsage=false;policy.initBattleInterface(env,cb,prefs);
  policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy.activeStack(battle()->getBattleID(),active);ASSERT_TRUE(cb->action);ASSERT_EQ(cb->action->target.size(),1u);
  const auto destination=cb->action->target.front().hexValue;
  EXPECT_LT(BattleHex::getDistance(destination,(fallback?ordinary:strong)->getPosition()),BattleHex::getDistance(destination,(fallback?strong:ordinary)->getPosition()));
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),active->unitOwner(),*cb->action));
 }
 void depletedActiveArcherPursuit(bool fallback)
 {
  prepare(false,true); ASSERT_FALSE(HasFatalFailure());
  // Keep the first legal walk steps distinct: the farther original goals
  // can share a horizontal path prefix despite different chosen targets.
  active->setPosition(BattleHex(93)); strong->setPosition(BattleHex(36)); ordinary->setPosition(BattleHex(138));
  active->shots.use(active->shots.available()); ASSERT_FALSE(view->battleCanShoot(active));
  const auto a=FHeroesThreatFixtureOracle::archerDefenderValue(facts(*strong),facts(*active),false);
  const auto b=FHeroesThreatFixtureOracle::archerDefenderValue(facts(*ordinary),facts(*active),false);
  ASSERT_GT(a.potential,b.potential);
  const auto native=[&](const CStack * u){const auto d=view->battleEstimateDamage(BattleAttackInfo(u,active,12,false));return (double(d.damage.min)+d.damage.max)/2;};
  ASSERT_LT(native(strong),native(ordinary));
  const auto routes=view->getReachability(active);
  const auto shortest=[&](const CStack * unit){int best=999;for(const auto & hex:unit->getAttackableHexes(active))if(hex.isValid()&&view->isMeleeAttackPossible(active,unit,hex))best=std::min(best,int(routes.distances[hex.toInt()]));return best;};
  ASSERT_LT(shortest(strong),GameConstants::BFIELD_SIZE); ASSERT_EQ(shortest(strong),shortest(ordinary));
  ASSERT_GT(shortest(strong),active->getMovementRange());
  if(fallback) active->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary}); ASSERT_EQ(bool(source),!fallback);
  if(source){EXPECT_EQ(source->at(strong->unitId()).potential,a.potential);EXPECT_EQ(source->at(ordinary->unitId()).potential,b.potential);}
  const auto stance=AIIntegration::FHeroes2TacticsContext::tactics(*view,*active,{strong,ordinary});
  ASSERT_FALSE(stance.defensive);
  // All opponents are melee: source enemy shooter fraction is zero, so
  // approach uses its cautious path-step selector, not another policy.
  ASSERT_TRUE(stance.cautiousOffensive);
  ASSERT_FALSE(AIIntegration::fheroes2IsArcher(*active)); // archerRetreat cannot run
  beginCombat(); ASSERT_FALSE(HasFatalFailure()); battle()->activeStack=active->unitId();
  AIIntegration::FHeroes2ThreatCandidateBattleAI policy;policy.playerID=active->unitOwner();AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy.initBattleInterface(env,cb,prefs);
  policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy.activeStack(battle()->getBattleID(),active);ASSERT_TRUE(cb->action);
  std::cout<<"ARCHER_DEFENDER fallback="<<fallback<<" source="<<a.potential<<","<<b.potential<<" native="<<native(strong)<<","<<native(ordinary)<<" route="<<shortest(strong)<<" action="<<cb->action->toString()<<"\n";
  ASSERT_EQ(cb->action->actionType,EActionType::WALK);ASSERT_EQ(cb->action->target.size(),1u);
  const auto destination=cb->action->target.front().hexValue;
  EXPECT_LT(BattleHex::getDistance(destination,(fallback?ordinary:strong)->getPosition()),BattleHex::getDistance(destination,(fallback?strong:ordinary)->getPosition()));
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),active->unitOwner(),*cb->action));
 }
 void pursuit(int unsupported)
 {
  prepare();ASSERT_FALSE(HasFatalFailure());
  const auto sourceStrong=FHeroesThreatFixtureOracle::value(facts(*strong),facts(*active));
  const auto sourceOrdinary=FHeroesThreatFixtureOracle::value(facts(*ordinary),facts(*active));
  const auto nativeDamage=[&](const CStack * unit){const auto d=view->battleEstimateDamage(BattleAttackInfo(unit,active,12,false));return (double(d.damage.min)+d.damage.max)/2;};
  ASSERT_GT(sourceStrong.potential,sourceOrdinary.potential);
  ASSERT_LT(nativeDamage(strong),nativeDamage(ordinary));
  ASSERT_EQ(BattleHex::getDistance(strong->getPosition(),active->getPosition()),BattleHex::getDistance(ordinary->getPosition(),active->getPosition()));
  ASSERT_EQ(strong->getMovementRange(),ordinary->getMovementRange());
  const auto routes=view->getReachability(active);
  const auto shortest=[&](const CStack * unit){int best=999;for(const auto & hex:unit->getAttackableHexes(active))if(hex.isValid()&&view->isMeleeAttackPossible(active,unit,hex))best=std::min(best,int(routes.distances[hex.toInt()]));return best;};
  ASSERT_EQ(shortest(strong),shortest(ordinary));
  if(unsupported)
   (unsupported==1?strong:active)->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
  const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary});
  ASSERT_EQ(source.has_value(),unsupported==0);
  if(source) {EXPECT_EQ(source->at(strong->unitId()).potential,sourceStrong.potential);EXPECT_EQ(source->at(ordinary->unitId()).potential,sourceOrdinary.potential);}
  beginCombat();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->battleActiveUnit(),active);
  AIIntegration::FHeroes2ThreatCandidateBattleAI policy;policy.playerID=active->unitOwner();AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy.initBattleInterface(env,cb,prefs);
  policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
  policy.activeStack(battle()->getBattleID(),active);ASSERT_TRUE(cb->action);
  ASSERT_EQ(cb->action->target.size(),1u);
  EXPECT_EQ(cb->action->target.front().hexValue,BattleHex(unsupported?91:38));
  std::cout<<"PURSUIT source="<<bool(source)<<" sourceThreats="<<sourceStrong.potential<<","<<sourceOrdinary.potential<<" nativeThreats="<<nativeDamage(strong)<<","<<nativeDamage(ordinary)<<" action="<<cb->action->toString()<<"\n";
  ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),active->unitOwner(),*cb->action));
 }
};
TEST_F(FHeroes2NativeThreatBattle,ordinarySourceChangesPursuitAndServerAccepts){pursuit(0);}
TEST_F(FHeroes2NativeThreatBattle,unsupportedCompetitorFallsBackForWholePursuit){pursuit(1);}
TEST_F(FHeroes2NativeThreatBattle,unsupportedActiveFallsBackForWholePursuit){pursuit(2);}

TEST_F(FHeroes2NativeThreatBattle,woundedAndSpentRetaliation)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 injure(active,9);ASSERT_EQ(active->getCount(),1000);
 auto actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong});ASSERT_TRUE(actual);
 const auto expected=FHeroesThreatFixtureOracle::value(facts(*strong),facts(*active));
 EXPECT_EQ(actual->at(strong->unitId()).potential,expected.potential);EXPECT_EQ(actual->at(strong->unitId()).retaliation,expected.retaliation);EXPECT_GT(expected.retaliation,0u);
 active->counterAttacks.use();ASSERT_FALSE(active->ableToRetaliate());
 actual=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong});ASSERT_TRUE(actual);EXPECT_EQ(actual->at(strong->unitId()).retaliation,0u);
}
TEST_F(FHeroes2NativeThreatBattle,standardAdditiveDoubleAttackUsesSourceThreat)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 auto * crusader=addStack(BattleSide::DEFENDER,CreatureID(7),BattleHex(70),2);ASSERT_NE(crusader,nullptr);
 ASSERT_EQ(crusader->getTotalAttacks(false),2);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*crusader));
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*crusader)->doubleMelee);
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,crusader}));
}

TEST_F(FHeroes2NativeThreatBattle,rangedSourceChangesPursuitAndServerAccepts)
{
 prepare(true); ASSERT_FALSE(HasFatalFailure());
 const auto a=FHeroesThreatFixtureOracle::rangedValue(facts(*strong),facts(*active));
 const auto b=FHeroesThreatFixtureOracle::rangedValue(facts(*ordinary),facts(*active));
 ASSERT_GT(a.potential,b.potential);
 const auto native=[&](const CStack * u){ const auto d=view->battleEstimateDamage(BattleAttackInfo(u,active,7,true)); return (double(d.damage.min)+d.damage.max)/2; };
 ASSERT_LT(native(strong),native(ordinary));
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary}); ASSERT_TRUE(source);
 EXPECT_EQ(source->at(strong->unitId()).potential,a.potential); EXPECT_EQ(source->at(ordinary->unitId()).potential,b.potential);
 ASSERT_EQ(BattleHex::getDistance(active->getPosition(),strong->getPosition()),BattleHex::getDistance(active->getPosition(),ordinary->getPosition()));
 beginCombat(); ASSERT_FALSE(HasFatalFailure()); battle()->activeStack=active->unitId();
 AIIntegration::FHeroes2ThreatCandidateBattleAI policy; policy.playerID=active->unitOwner(); AutocombatPreferences prefs; prefs.enableSpellsUsage=false; policy.initBattleInterface(env,cb,prefs);
 policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 policy.activeStack(battle()->getBattleID(),active); ASSERT_TRUE(cb->action); ASSERT_EQ(cb->action->target.size(),1u);
 const auto destination=cb->action->target.front().hexValue;
 EXPECT_LT(BattleHex::getDistance(destination,strong->getPosition()),BattleHex::getDistance(destination,ordinary->getPosition()));
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),active->unitOwner(),*cb->action));
}
TEST_F(FHeroes2NativeThreatBattle,unknownRangedCompetitorRejectsWholeComparison)
{
 prepare(true); ASSERT_FALSE(HasFatalFailure());
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary}));
 ordinary->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_MIRROR,BonusSource::OTHER,1,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*active,{strong,ordinary}));
}

TEST_F(FHeroes2NativeThreatBattle,depletedArcherSourcePursuitAndServerMove){archerMeleePursuit(false,false);}
TEST_F(FHeroes2NativeThreatBattle,depletedArcherNativeFallbackChoosesOtherTarget){archerMeleePursuit(false,true);}
TEST_F(FHeroes2NativeThreatBattle,blockedArcherSourcePursuitAndServerMove){archerMeleePursuit(true,false);}
TEST_F(FHeroes2NativeThreatBattle,blockedArcherNativeFallbackChoosesOtherTarget){archerMeleePursuit(true,true);}

TEST_F(FHeroes2NativeThreatBattle,depletedActiveArcherSourcePursuesAndServerAccepts){depletedActiveArcherPursuit(false);}
TEST_F(FHeroes2NativeThreatBattle,unsupportedActiveArcherFallsBackAndPursuesOtherTarget){depletedActiveArcherPursuit(true);}
