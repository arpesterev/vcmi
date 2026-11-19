/* Standalone native performance observation; GPL-2.0-or-later. */
#include "StdInc.h"
#include <chrono>
#define FHeroes2BattleAI FHeroes2DoubleProbeBattleAI
#define FHeroes2SpellPolicy FHeroes2DoubleProbeSpellPolicy
#define FHeroes2TacticsContext FHeroes2DoubleProbeTacticsContext
#define NativeStrengthModel DoubleProbeStrengthModel
#define nativeStrengthFacts doubleProbeStrengthFacts
#define ordinaryNativeThreatUnit doubleProbeThreatUnit
#define ordinaryPursuitThreats doubleProbePursuitThreats
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
class DoubleProbeCallback final : public CBattleCallback
{
public:
	explicit DoubleProbeCallback(PlayerColor player) : CBattleCallback(player,nullptr) {}
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
class DoubleProbeEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<DoubleProbeCallback> callback;
public:
	DoubleProbeEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<DoubleProbeCallback> cb):gameCallback(std::move(game)),callback(std::move(cb)){}
	const Services * services() const override {return LIBRARY;}
	const GameCb * game() const override {return gameCallback.get();}
	const BattleCb * battle(const BattleID & id) const override {return callback->getBattle(id).get();}
};
}


namespace
{
class DoubleRecorder final : public RecordingGameServer
{
public:
 std::vector<BattleAttack> attacks;
 void applyPack(CPackForClient & pack) override
 {
  if(const auto * attack=dynamic_cast<const BattleAttack *>(&pack))attacks.push_back(*attack);
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
class FHeroes2DoubleAttackBattle : public BattleTestFixture
{
protected:
 DoubleRecorder recorder;
 CStack * crusader=nullptr;
 CStack * ordinary=nullptr;
 std::shared_ptr<DoubleProbeCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<DoubleProbeEnvironment> environment;
 void prepare()
 {
  startGame();ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(7).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
  attackerSideHero->setStackCount(SlotID(0),6);defenderSideHero->setStackCount(SlotID(0),6);
  recorder.gameState=gameState();gameHandler=std::make_shared<CGameHandler>(recorder,gameState());gameHandler->randomizer->setSeed(seed);
  startBattle();ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)(unit->unitSide()==BattleSide::ATTACKER?crusader:ordinary)=unit.get();
  ASSERT_NE(crusader,nullptr);ASSERT_NE(ordinary,nullptr);
  const auto player=crusader->unitOwner();callback=std::make_shared<DoubleProbeCallback>(player);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<DoubleProbeEnvironment>(makeCallback(player),callback);
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
 void rejectWhole()
 {
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*crusader));
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);EXPECT_FALSE(model.usesSourceScale());
  for(const auto & unit:battle()->stacks)EXPECT_DOUBLE_EQ(model.value(*unit),unit->estimateCombatValue());
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{crusader}));
 }
};
TEST_F(FHeroes2DoubleAttackBattle,canonicalRecordAndWholeSourceStrength)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 const auto records=crusader->unitType()->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::ADDITIONAL_ATTACK));ASSERT_EQ(records->size(),1u);const auto & b=(*records)[0];
 EXPECT_EQ(b->val,1);EXPECT_EQ(b->valType,BonusValueType::ADDITIVE_VALUE);EXPECT_EQ(b->source,BonusSource::CREATURE_ABILITY);EXPECT_EQ(b->sid,BonusSourceID(CreatureID(7)));EXPECT_EQ(b->duration,BonusDuration::PERMANENT);EXPECT_EQ(b->effectRange,BonusLimitEffect::NO_LIMIT);EXPECT_FALSE(crusader->doubleWide());EXPECT_EQ(crusader->getTotalAttacks(false),2);
 const auto mapped=AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*crusader);ASSERT_TRUE(mapped);EXPECT_TRUE(mapped->doubleMelee);EXPECT_FALSE(mapped->doubleDamageUndead);
 const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);ASSERT_TRUE(model.usesSourceScale());
 EXPECT_DOUBLE_EQ(model.value(*crusader),oracleStrength(*crusader,true));EXPECT_DOUBLE_EQ(model.value(*ordinary),oracleStrength(*ordinary,false));
 EXPECT_NEAR(oracleStrength(*crusader,true)/oracleStrength(*crusader,false),std::sqrt(1.75),1e-12);
 EXPECT_NE(model.value(*ordinary),ordinary->estimateCombatValue());
}
TEST_F(FHeroes2DoubleAttackBattle,controllerExecutesTwoRealStrikesAndOneCounter)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 ordinary->setPosition(BattleHex(crusader->getPosition().toInt()+5));
 beginCombat();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->battleActiveUnit(),crusader);
 ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 AIIntegration::FHeroes2DoubleProbeBattleAI policy;policy.playerID=crusader->unitOwner();AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy.initBattleInterface(environment,callback,prefs);
 policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);
 const auto hp=ordinary->getAvailableHealth();policy.activeStack(battle()->getBattleID(),crusader);ASSERT_TRUE(callback->action);ASSERT_EQ(callback->action->actionType,EActionType::WALK_AND_ATTACK);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),crusader->unitOwner(),*callback->action));
 EXPECT_LT(ordinary->getAvailableHealth(),hp);ASSERT_EQ(recorder.attacks.size(),3u);
 EXPECT_EQ(recorder.attacks[0].stackAttacking,crusader->unitId());EXPECT_FALSE(recorder.attacks[0].counter());
 EXPECT_EQ(recorder.attacks[1].stackAttacking,ordinary->unitId());EXPECT_TRUE(recorder.attacks[1].counter());
 EXPECT_EQ(recorder.attacks[2].stackAttacking,crusader->unitId());EXPECT_FALSE(recorder.attacks[2].counter());
}
TEST_F(FHeroes2DoubleAttackBattle,secondStrikeThreatUsesWoundedAndSpentSourceCounter)
{
 prepare();ASSERT_FALSE(HasFatalFailure());injure(ordinary,ordinary->getMaxHealth()-1);
 auto expected=FHeroesThreatFixtureOracle::value(facts(*crusader),facts(*ordinary));
 auto values=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{crusader});ASSERT_TRUE(values);
 EXPECT_EQ(values->at(crusader->unitId()).potential,expected.potential);EXPECT_EQ(values->at(crusader->unitId()).retaliation,expected.retaliation);
 AIIntegration::Ports::FHeroes2::ThreatProjection projection;projection.damage=expected.potential;projection.retaliationDamage=expected.retaliation;projection.attackerHealth=crusader->getAvailableHealth();projection.attacks=2;
 EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::projectRelativeThreat(projection),expected.potential*(2-std::min(double(expected.retaliation)/crusader->getAvailableHealth(),1.0)));
 ordinary->counterAttacks.use();ASSERT_FALSE(ordinary->ableToRetaliate());
 values=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{crusader});ASSERT_TRUE(values);EXPECT_EQ(values->at(crusader->unitId()).retaliation,0u);
 ordinary->counterAttacks.reset();ASSERT_TRUE(ordinary->ableToRetaliate());injure(ordinary,ordinary->getAvailableHealth()-1);
 values=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{crusader});ASSERT_TRUE(values);EXPECT_GE(values->at(crusader->unitId()).potential,ordinary->getAvailableHealth());EXPECT_EQ(values->at(crusader->unitId()).retaliation,0u);
}
TEST_F(FHeroes2DoubleAttackBattle,limitedInnateRecordRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());auto * type=const_cast<CCreature *>(CreatureID(7).toCreature());AbilityMutation mutation(type,[&](Bonus& b){b.limiter=std::make_shared<CCreatureTypeLimiter>(*type,false);});rejectWhole();
}
TEST_F(FHeroes2DoubleAttackBattle,percentInnateRecordRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());AbilityMutation mutation(const_cast<CCreature *>(CreatureID(7).toCreature()),[](Bonus& b){b.valType=BonusValueType::PERCENT_TO_ALL;});rejectWhole();
}
TEST_F(FHeroes2DoubleAttackBattle,dynamicExtraRecordRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());crusader->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ADDITIONAL_ATTACK,BonusSource::OTHER,1,BonusSourceID()));ASSERT_EQ(crusader->getTotalAttacks(false),3);rejectWhole();
}
TEST_F(FHeroes2DoubleAttackBattle,duplicateInnateRecordRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());auto * type=const_cast<CCreature *>(CreatureID(7).toCreature());auto extra=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ADDITIONAL_ATTACK,BonusSource::CREATURE_ABILITY,1,BonusSourceID(CreatureID(7)));
 struct Restore {CCreature * type;std::shared_ptr<Bonus> extra;~Restore(){type->removeBonus(extra);}} restore{type,extra};type->addNewBonus(extra);rejectWhole();
}
TEST_F(FHeroes2DoubleAttackBattle,canonicalRangedDefenderAdmittedWithSourceStrengthAndThreat)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 for(const auto id:{3,19}) {auto * unit=addStack(BattleSide::DEFENDER,CreatureID(id),BattleHex(70+id%3),2);ASSERT_NE(unit,nullptr);EXPECT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*unit));EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());EXPECT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*unit,{crusader}));injure(unit,unit->getAvailableHealth());}
}
TEST_F(FHeroes2DoubleAttackBattle,sourceSecondStrikeChangesPursuitTarget)
{
 startGame();ASSERT_FALSE(HasFatalFailure());attackerSideHero->setStackCount(SlotID(0),20);defenderSideHero->setStackCount(SlotID(0),4);
 ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(7).toCreature()));
 startBattle();ASSERT_FALSE(HasFatalFailure());CStack * active=nullptr;CStack * enemy=nullptr;
 for(const auto & unit:battle()->stacks)
  (unit->unitSide()==BattleSide::ATTACKER?active:enemy)=unit.get();
 ASSERT_NE(active,nullptr);ASSERT_NE(enemy,nullptr);
 for(auto * u:{active,enemy}){BattleStackMoved move;move.battleID=battle()->getBattleID();move.stack=u->unitId();move.teleporting=true;move.tilesToMove.insert(BattleHex(u==active?87:48));server.applyPack(move);}
 auto * alternative=addStack(BattleSide::DEFENDER,CreatureID(0),BattleHex(150),70);ASSERT_NE(alternative,nullptr);
 ASSERT_NE(addStack(BattleSide::ATTACKER,CreatureID(0),BattleHex(19),10000),nullptr);
 const auto player=active->unitOwner();auto cb=std::make_shared<DoubleProbeCallback>(player);cb->onBattleStarted(battle());const auto localView=cb->getBattle(battle()->getBattleID());auto env=std::make_shared<DoubleProbeEnvironment>(makeCallback(player),cb);
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*localView).usesSourceScale());
 const auto sourceValue=[&](const CStack & unit){const auto d=FHeroesThreatFixtureOracle::value(facts(unit),facts(*active));const auto distance=BattleHex::getDistance(unit.getPosition(),active->getPosition());const double penalty=distance>unit.getMovementRange()+1?1.5*distance/unit.getMovementRange():1;return d.potential*(unit.getTotalAttacks(false)>1?2-std::min(double(d.retaliation)/unit.getAvailableHealth(),1.0):1)/penalty;};
 const auto nativeValue=[&](const CStack & unit){const auto distance=BattleHex::getDistance(unit.getPosition(),active->getPosition());DamageEstimation counter;const auto d=localView->battleEstimateDamage(BattleAttackInfo(&unit,active,distance,false),&counter);const auto average=[](const DamageEstimation& d){return (double(d.damage.min)+d.damage.max)/2;};const double penalty=distance>unit.getMovementRange()+1?1.5*distance/unit.getMovementRange():1;return average(d)*(unit.getTotalAttacks(false)>1?2-std::min(average(counter)/unit.getAvailableHealth(),1.0):1)/penalty;};
 ASSERT_GT(sourceValue(*enemy),sourceValue(*alternative));ASSERT_LT(nativeValue(*enemy),nativeValue(*alternative));
 const auto source=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*localView,*active,{enemy,alternative});EXPECT_TRUE(source);if(source){EXPECT_GT(source->at(enemy->unitId()).retaliation,0u);EXPECT_LT(source->at(enemy->unitId()).retaliation,enemy->getAvailableHealth());}
 beginCombat();ASSERT_FALSE(HasFatalFailure());battle()->activeStack=active->unitId();
 AIIntegration::FHeroes2DoubleProbeBattleAI policy;policy.playerID=player;AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy.initBattleInterface(env,cb,prefs);
 policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);policy.activeStack(battle()->getBattleID(),active);ASSERT_TRUE(cb->action);ASSERT_EQ(cb->action->target.size(),1u);
 EXPECT_EQ(cb->action->target.front().hexValue,BattleHex(38));
 std::cout<<"DOUBLE_PURSUIT source="<<sourceValue(*enemy)<<","<<sourceValue(*alternative)<<" native="<<nativeValue(*enemy)<<","<<nativeValue(*alternative)<<" action="<<cb->action->toString()<<"\n";
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),player,*cb->action));
}
TEST_F(FHeroes2DoubleAttackBattle,noncanonicalProvenanceAndDurationRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());auto * type=const_cast<CCreature *>(CreatureID(7).toCreature());
 for(int mutation=0;mutation<4;++mutation)
 {
  SCOPED_TRACE(mutation);
  AbilityMutation change(type,[&](Bonus& b){if(mutation==0)b.sid=BonusSourceID(CreatureID(0));if(mutation==1)b.source=BonusSource::OTHER;if(mutation==2)b.duration=BonusDuration::ONE_BATTLE;if(mutation==3)b.stacking="conditional";});rejectWhole();
 }
}

TEST_F(FHeroes2DoubleAttackBattle,dynamicShooterConversionRejected)
{
 prepare();ASSERT_FALSE(HasFatalFailure());
 crusader->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOOTER,BonusSource::OTHER,1,BonusSourceID()));
 crusader->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOTS,BonusSource::OTHER,12,BonusSourceID()));
 ASSERT_TRUE(crusader->isShooter());rejectWhole();
}
