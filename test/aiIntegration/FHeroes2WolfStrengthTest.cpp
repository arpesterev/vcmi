/* Standalone native performance observation; GPL-2.0-or-later. */
#include "StdInc.h"
#include <chrono>
#define FHeroes2BattleAI FHeroes2WolfProbeBattleAI
#define FHeroes2SpellPolicy FHeroes2WolfProbeSpellPolicy
#define FHeroes2TacticsContext FHeroes2WolfProbeTacticsContext
#define NativeStrengthModel WolfProbeStrengthModel
#define nativeStrengthFacts wolfProbeStrengthFacts
#define ordinaryNativeThreatUnit wolfProbeThreatUnit
#define ordinaryPursuitThreats wolfProbePursuitThreats
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
class WolfProbeCallback final : public CBattleCallback
{
public:
	explicit WolfProbeCallback(PlayerColor player) : CBattleCallback(player,nullptr) {}
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
class WolfProbeEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<WolfProbeCallback> callback;
public:
	WolfProbeEnvironment(std::shared_ptr<CCallback> game,std::shared_ptr<WolfProbeCallback> cb):gameCallback(std::move(game)),callback(std::move(cb)){}
	const Services * services() const override {return LIBRARY;}
	const GameCb * game() const override {return gameCallback.get();}
	const BattleCb * battle(const BattleID & id) const override {return callback->getBattle(id).get();}
};
}


namespace
{
class WolfRecorder final : public RecordingGameServer
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
class FHeroes2WolfBattle : public BattleTestFixture
{
protected:
 WolfRecorder recorder;
 CStack * wolf=nullptr;
 CStack * ordinary=nullptr;
 std::shared_ptr<WolfProbeCallback> callback;
 std::shared_ptr<CPlayerBattleCallback> view;
 std::shared_ptr<WolfProbeEnvironment> environment;
 void prepare()
 {
  startGame();ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id,SlotID(0)),CreatureID(87).toCreature()));
  ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id,SlotID(0)),CreatureID(6).toCreature()));
  attackerSideHero->setStackCount(SlotID(0),30);defenderSideHero->setStackCount(SlotID(0),10);
  recorder.gameState=gameState();gameHandler=std::make_shared<CGameHandler>(recorder,gameState());gameHandler->randomizer->setSeed(seed);
  startBattle();ASSERT_FALSE(HasFatalFailure());
  for(const auto & unit:battle()->stacks)(unit->unitSide()==BattleSide::ATTACKER?wolf:ordinary)=unit.get();
  ASSERT_NE(wolf,nullptr);ASSERT_NE(ordinary,nullptr);
  const auto player=wolf->unitOwner();callback=std::make_shared<WolfProbeCallback>(player);callback->onBattleStarted(battle());view=callback->getBattle(battle()->getBattleID());environment=std::make_shared<WolfProbeEnvironment>(makeCallback(player),callback);
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
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*wolf));
  const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);EXPECT_FALSE(model.usesSourceScale());
  for(const auto & unit:battle()->stacks)EXPECT_DOUBLE_EQ(model.value(*unit),unit->estimateCombatValue());
  EXPECT_FALSE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{wolf}));
 }
};
TEST_F(FHeroes2WolfBattle,canonicalWideDoubleUsesWholeSourceScale)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(wolf->doubleWide());ASSERT_EQ(wolf->getHexes().size(),2u);ASSERT_EQ(wolf->getTotalAttacks(false),2);
 const auto list=wolf->unitType()->getBonusBearer()->getAllBonuses(Selector::type()(BonusType::ADDITIONAL_ATTACK));ASSERT_EQ(list->size(),1u);const auto& b=(*list)[0];
 EXPECT_EQ(b->val,1);EXPECT_EQ(b->valType,BonusValueType::ADDITIVE_VALUE);EXPECT_EQ(b->source,BonusSource::CREATURE_ABILITY);EXPECT_EQ(b->sid,BonusSourceID(CreatureID(87)));EXPECT_EQ(b->duration,BonusDuration::PERMANENT);EXPECT_EQ(b->effectRange,BonusLimitEffect::NO_LIMIT);
 const auto mapped=AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*wolf);ASSERT_TRUE(mapped);EXPECT_TRUE(mapped->doubleMelee);EXPECT_FALSE(mapped->archer);
 const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);ASSERT_TRUE(model.usesSourceScale());
 EXPECT_DOUBLE_EQ(model.value(*wolf),oracleStrength(*wolf,true));EXPECT_DOUBLE_EQ(model.value(*ordinary),oracleStrength(*ordinary,false));EXPECT_NEAR(model.value(*wolf)/oracleStrength(*wolf,false),std::sqrt(1.75),1e-12);
}
TEST_F(FHeroes2WolfBattle,controllerWideMoveAndTwoAttacksAreNativeLegal)
{
 prepare();ASSERT_FALSE(HasFatalFailure());wolf->setPosition(BattleHex(88));ordinary->setPosition(BattleHex(95));
 beginCombat();ASSERT_FALSE(HasFatalFailure());ASSERT_EQ(battle()->battleActiveUnit(),wolf);
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 EXPECT_TRUE(AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*wolf,{ordinary}));
 const auto oldPosition=wolf->getPosition();const auto oldHP=ordinary->getAvailableHealth();
 AIIntegration::FHeroes2WolfProbeBattleAI policy;policy.playerID=wolf->unitOwner();AutocombatPreferences prefs;prefs.enableSpellsUsage=false;policy.initBattleInterface(environment,callback,prefs);
 policy.battleStart(battle()->getBattleID(),battle()->getSideArmy(BattleSide::ATTACKER),battle()->getSideArmy(BattleSide::DEFENDER),battle()->getLocation(),attackerSideHero,defenderSideHero,BattleSide::ATTACKER,false);policy.activeStack(battle()->getBattleID(),wolf);ASSERT_TRUE(callback->action);ASSERT_EQ(callback->action->actionType,EActionType::WALK_AND_ATTACK);
 ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(),wolf->unitOwner(),*callback->action));
 EXPECT_NE(wolf->getPosition(),oldPosition);ASSERT_EQ(wolf->getHexes().size(),2u);EXPECT_FALSE(recorder.moves.empty());
 for(const auto hex:wolf->getHexes()){EXPECT_TRUE(hex.isAvailable());EXPECT_EQ(battle()->battleGetUnitByPos(hex),wolf);}
 EXPECT_EQ(BattleHex::getDistance(wolf->getHexes()[0],wolf->getHexes()[1]),1);EXPECT_LT(ordinary->getAvailableHealth(),oldHP);
 ASSERT_EQ(recorder.attacks.size(),3u);EXPECT_EQ(recorder.attacks[0].stackAttacking,wolf->unitId());EXPECT_FALSE(recorder.attacks[0].counter());EXPECT_EQ(recorder.attacks[1].stackAttacking,ordinary->unitId());EXPECT_TRUE(recorder.attacks[1].counter());EXPECT_EQ(recorder.attacks[2].stackAttacking,wolf->unitId());EXPECT_FALSE(recorder.attacks[2].counter());
}
TEST_F(FHeroes2WolfBattle,wideThreatMatchesPinnedDamageForEitherRole)
{
 prepare();ASSERT_FALSE(HasFatalFailure());EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
 const auto attacking=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*ordinary,{wolf});ASSERT_TRUE(attacking);
 const auto defending=AIIntegration::Ports::FHeroes2::ordinaryPursuitThreats(*view,*wolf,{ordinary});ASSERT_TRUE(defending);
 const auto a=FHeroesThreatFixtureOracle::value(facts(*wolf),facts(*ordinary));
 const auto d=FHeroesThreatFixtureOracle::value(facts(*ordinary),facts(*wolf));
 EXPECT_EQ(attacking->at(wolf->unitId()).potential,a.potential);EXPECT_EQ(attacking->at(wolf->unitId()).retaliation,a.retaliation);
 EXPECT_EQ(defending->at(ordinary->unitId()).potential,d.potential);EXPECT_EQ(defending->at(ordinary->unitId()).retaliation,d.retaliation);
}
TEST_F(FHeroes2WolfBattle,woundsAndRetainedDeadWolfKeepSourceCountScale)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());const auto count=wolf->getCount();const double before=AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).value(*wolf);
 injure(wolf,wolf->getMaxHealth()-1);EXPECT_EQ(wolf->getCount(),count);EXPECT_DOUBLE_EQ(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).value(*wolf),before);
 injure(wolf,wolf->getAvailableHealth());ASSERT_FALSE(wolf->alive());const AIIntegration::Ports::FHeroes2::NativeStrengthModel after(*view);EXPECT_TRUE(after.usesSourceScale());EXPECT_DOUBLE_EQ(after.value(*wolf),0);EXPECT_DOUBLE_EQ(after.value(*ordinary),oracleStrength(*ordinary,false));
}
TEST_F(FHeroes2WolfBattle,unsupportedOtherUnitForcesWholeRosterNative)
{
 prepare();ASSERT_FALSE(HasFatalFailure());ASSERT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*wolf));auto * archer=addStack(BattleSide::DEFENDER,CreatureID(3),BattleHex(70),2);ASSERT_NE(archer,nullptr); archer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ON_COMBAT_EVENT,BonusSource::OTHER,0,BonusSourceID()));
 EXPECT_FALSE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*archer));const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);EXPECT_FALSE(model.usesSourceScale());for(const auto& unit:battle()->stacks)EXPECT_DOUBLE_EQ(model.value(*unit),unit->estimateCombatValue());
}
TEST_F(FHeroes2WolfBattle,wideCanonicalGuardsRejectMetadataAndLiveChanges)
{
 prepare();ASSERT_FALSE(HasFatalFailure());auto * type=const_cast<CCreature *>(CreatureID(87).toCreature());
 for(int mutation=0;mutation<4;++mutation)
 {
  SCOPED_TRACE(mutation);AbilityMutation change(type,[&](Bonus& b){if(mutation==0)b.limiter=std::make_shared<CCreatureTypeLimiter>(*type,false);if(mutation==1)b.sid=BonusSourceID(CreatureID(7));if(mutation==2)b.valType=BonusValueType::PERCENT_TO_ALL;if(mutation==3)b.stacking="noncanonical";});rejectWhole();
 }
 auto extra=std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::ADDITIONAL_ATTACK,BonusSource::OTHER,1,BonusSourceID());wolf->addNewBonus(extra);rejectWhole();wolf->removeBonus(extra);ASSERT_TRUE(AIIntegration::Ports::FHeroes2::nativeStrengthFacts(*wolf));
 wolf->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOOTER,BonusSource::OTHER,1,BonusSourceID()));wolf->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::SHOTS,BonusSource::OTHER,12,BonusSourceID()));ASSERT_TRUE(wolf->isShooter());rejectWhole();
}
