/* FHeroes2NativeStrengthTest.cpp, part of VCMI. GPL-2.0-or-later. */
#include "StdInc.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2NativeStrength.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <vcmi/Environment.h>

namespace
{
class StrengthEnvironment : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<CBattleCallback> callback;
public:
	StrengthEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<CBattleCallback> cb)
		: gameCallback(std::move(game)), callback(std::move(cb)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & id) const override { return callback->getBattle(id).get(); }
};
}
class FHeroes2NativeStrengthBattle : public BattleTestFixture
{
protected:
	std::shared_ptr<CBattleCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> view;
	std::shared_ptr<StrengthEnvironment> environment;
	void prepare()
	{
		startGame(); ASSERT_FALSE(HasFatalFailure()); startBattle(); ASSERT_FALSE(HasFatalFailure());
		for(const auto & spell : LIBRARY->spellh->objects) attackerSideHero->removeSpellFromSpellbook(spell->id);
		callback = std::make_shared<CBattleCallback>(attackerSideHero->getOwner(), nullptr);
		callback->onBattleStarted(battle()); view = callback->getBattle(battle()->getBattleID());
		environment = std::make_shared<StrengthEnvironment>(makeCallback(attackerSideHero->getOwner()), callback);
	}
};
TEST_F(FHeroes2NativeStrengthBattle,ordinaryRosterUsesSourceScale)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
	ASSERT_TRUE(model.usesSourceScale());
	for(const auto & unit : battle()->stacks)
	{
		const auto * type = unit->unitType();
		const int speed = type->getBaseSpeed() - 4;
		const double special = 1.0 + (speed < 0 ? speed * 0.1 : speed * 0.05);
		const double expected = (1.0 + unit->getAttack(false) * 0.1 + unit->getDefense(false) * 0.05)
			* std::sqrt((type->getBaseDamageMin() + type->getBaseDamageMax()) / 2.0 * type->getBaseHitPoints())
			* special * unit->getCount();
		EXPECT_DOUBLE_EQ(model.value(*unit), expected);
		EXPECT_NE(model.value(*unit), unit->estimateCombatValue());
	}
}
TEST_F(FHeroes2NativeStrengthBattle,unsupportedRosterKeepsWholeScaleNative)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	auto * special = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(74), 12); ASSERT_NE(special,nullptr);
	special->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ON_COMBAT_EVENT, BonusSource::OTHER, 0, BonusSourceID()));
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
	ASSERT_FALSE(model.usesSourceScale());
	for(const auto & unit : battle()->stacks) EXPECT_DOUBLE_EQ(model.value(*unit), unit->estimateCombatValue());
}
TEST_F(FHeroes2NativeStrengthBattle,summoningCensusKeepsWholeScaleNative)
{
	prepare(); ASSERT_FALSE(HasFatalFailure()); teachSpell(attackerSideHero, SpellID::SUMMON_AIR_ELEMENTAL);
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
	ASSERT_FALSE(model.usesSourceScale());
	for(const auto & unit : battle()->stacks) EXPECT_DOUBLE_EQ(model.value(*unit), unit->estimateCombatValue());
}

TEST_F(FHeroes2NativeStrengthBattle,sourceScaleChangesLiveDamageTargetAndServerAccepts)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	for(const auto & unit : battle()->stacks)
	 if(unit->unitSide() == BattleSide::DEFENDER)
	  unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 100, BonusSourceID()));
	auto * archer = addStack(BattleSide::DEFENDER, CreatureID(2), BattleHex(74), 100);
	auto * goblin = addStack(BattleSide::DEFENDER, CreatureID(14), BattleHex(149), 100);
	ASSERT_NE(archer,nullptr); ASSERT_NE(goblin,nullptr);
	teachSpell(attackerSideHero, SpellID::MAGIC_ARROW);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
	beginCombat(); ASSERT_FALSE(HasFatalFailure());
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
	ASSERT_TRUE(model.usesSourceScale());
	const auto expectedSimpleStrength = [](const CStack & unit)
	{
		const auto * type = unit.unitType(); const int speed = type->getBaseSpeed()-4;
		const double special = 1.0 + (type->getBaseShots()>0 ? 0.4 : 0.0) + (speed<0 ? speed*0.1 : speed*0.05);
		return std::sqrt((type->getBaseDamageMin()+type->getBaseDamageMax())/2.0*type->getBaseHitPoints())
			* special * (1.0+unit.getAttack(type->getBaseShots()>0)*0.1+unit.getDefense(false)*0.05)*unit.getCount();
	};
	EXPECT_DOUBLE_EQ(model.value(*archer),expectedSimpleStrength(*archer));
	EXPECT_DOUBLE_EQ(model.value(*goblin),expectedSimpleStrength(*goblin));
	EXPECT_GT(expectedSimpleStrength(*archer)/archer->getAvailableHealth(),expectedSimpleStrength(*goblin)/goblin->getAvailableHealth());
	EXPECT_LT(double(archer->estimateCombatValue())/archer->getAvailableHealth(),double(goblin->estimateCombatValue())/goblin->getAvailableHealth());
	const auto mana = attackerSideHero->mana;
	const auto archerHP = archer->getAvailableHealth(); const auto goblinHP = goblin->getAvailableHealth(); const auto packets = server.casts.size();
	const auto selected = AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());
	ASSERT_TRUE(selected); ASSERT_EQ(selected->target.size(),1u);
	EXPECT_EQ(selected->target.front().hexValue,archer->getPosition());
	EXPECT_EQ(attackerSideHero->mana,mana); EXPECT_EQ(server.casts.size(),packets);
	EXPECT_EQ(archer->getAvailableHealth(),archerHP); EXPECT_EQ(goblin->getAvailableHealth(),goblinHP);
	ASSERT_TRUE(castAsHero(attackerSideHero,selected->spell,battle::Target{battle::Destination(selected->target.front().hexValue)}));
	EXPECT_LT(attackerSideHero->mana,mana); EXPECT_EQ(server.casts.size(),packets+1);
	EXPECT_LT(archer->getAvailableHealth(),archerHP); EXPECT_EQ(goblin->getAvailableHealth(),goblinHP);
}

TEST_F(FHeroes2NativeStrengthBattle,woundsDoNotFractionallyReduceSourceTroopCount)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	auto * unit = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(74), 10); ASSERT_NE(unit,nullptr);
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view); ASSERT_TRUE(model.usesSourceScale());
	const double before = model.value(*unit);
	injure(unit, unit->getMaxHealth()-1);
	ASSERT_EQ(unit->getCount(),10); EXPECT_DOUBLE_EQ(model.value(*unit),before);
	injure(unit,1); ASSERT_EQ(unit->getCount(),9); EXPECT_DOUBLE_EQ(model.value(*unit),before*0.9);
}
TEST_F(FHeroes2NativeStrengthBattle,archerCensusAndNonlethalSpellValueShareScale)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	auto * unit = addStack(BattleSide::DEFENDER, CreatureID(2), BattleHex(74), 12); ASSERT_NE(unit,nullptr);
	const auto * type = unit->unitType(); const int speed=type->getBaseSpeed()-4;
	const double base=std::sqrt((type->getBaseDamageMin()+type->getBaseDamageMax())/2.0*type->getBaseHitPoints())
		*(1.4+(speed<0?speed*0.1:speed*0.05));
	const double expected=base*(1.0+unit->getAttack(true)*0.1+unit->getDefense(false)*0.05)*unit->getCount();
	EXPECT_DOUBLE_EQ(AIIntegration::FHeroes2SpellPolicy::sourceArcherStrength(*view,attackerSideHero->getOwner(),false),expected);
	EXPECT_DOUBLE_EQ(AIIntegration::FHeroes2SpellPolicy::sourceDamageValue(*view,*unit,1),expected/unit->getAvailableHealth());
}
TEST_F(FHeroes2NativeStrengthBattle,liveAttackDefenseChangesUseSourceWeights)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	auto * unit = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(74), 10); ASSERT_NE(unit,nullptr);
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view); ASSERT_TRUE(model.usesSourceScale());
	const double before=model.value(*unit);
	const double base=before/(1.0+unit->getAttack(false)*0.1+unit->getDefense(false)*0.05);
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,5,BonusSourceID(),BonusSubtypeID(PrimarySkill::ATTACK)));
	EXPECT_NEAR(model.value(*unit),before+base*0.5,1e-10);
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::PRIMARY_SKILL,BonusSource::OTHER,4,BonusSourceID(),BonusSubtypeID(PrimarySkill::DEFENSE)));
	EXPECT_NEAR(model.value(*unit),before+base*0.7,1e-10);
}
TEST_F(FHeroes2NativeStrengthBattle,ordinaryDeathKeepsSourceScaleAndZeroTroopStrength)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	auto * unit = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(74), 10); ASSERT_NE(unit,nullptr);
	injure(unit,unit->getAvailableHealth()); ASSERT_FALSE(unit->alive()); ASSERT_EQ(unit->getCount(),0);
	const AIIntegration::Ports::FHeroes2::NativeStrengthModel model(*view);
	ASSERT_TRUE(model.usesSourceScale()); EXPECT_DOUBLE_EQ(model.value(*unit),0);
	for(const auto & survivor : battle()->stacks)
		if(survivor->alive())
		{
			EXPECT_GT(model.value(*survivor),0);
		}
}
TEST_F(FHeroes2NativeStrengthBattle,unsupportedPreviewRestartsWholeChooserInNativeScale)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	for(const auto & unit : battle()->stacks)
		if(unit->unitSide() == BattleSide::DEFENDER)
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,BonusType::MAGIC_RESISTANCE,BonusSource::OTHER,100,BonusSourceID()));
	auto * archer = addStack(BattleSide::DEFENDER,CreatureID(2),BattleHex(74),100);
	auto * goblin = addStack(BattleSide::DEFENDER,CreatureID(14),BattleHex(149),100);
	ASSERT_NE(archer,nullptr); ASSERT_NE(goblin,nullptr);
	teachSpell(attackerSideHero,SpellID::MAGIC_ARROW);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,1,ChangeValueMode::ABSOLUTE);
	beginCombat(); ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
	spells::BattleCast cast(view.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::MAGIC_ARROW).toSpell());
	const auto mechanics=SpellID(SpellID::MAGIC_ARROW).toSpell()->battleMechanics(&cast);
	auto & effects=const_cast<CSpell::LevelInfo &>(SpellID(SpellID::MAGIC_ARROW).toSpell()->getLevelInfo(mechanics->getEffectLevel())).battleEffects;
	struct Restore {JsonNode & target;JsonNode before;~Restore(){target=before;LIBRARY->spellh->afterLoadFinalization();}} restore{effects,effects};
	effects["zunsupported"]["type"].String()="timed";
	auto & bonus=effects["zunsupported"]["bonus"]["unknownEvent"];
	bonus["type"].String()="COMBAT_EVENT_TRIGGER"; bonus["val"].Integer()=0; bonus["duration"].String()="N_TURNS";
	LIBRARY->spellh->afterLoadFinalization();
	auto projected=std::make_shared<HypotheticBattle>(environment.get(),view);
	spells::BattleCast preview(projected.get(),attackerSideHero,spells::Mode::HERO,SpellID(SpellID::MAGIC_ARROW).toSpell());
	preview.castEval(projected->getServerCallback(),spells::Target{battle::Destination(archer)});
	ASSERT_TRUE(projected->battleGetUnitByID(archer->unitId())->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER));

	const auto mana=attackerSideHero->mana;const auto packets=server.casts.size();
	const auto selected=AIIntegration::FHeroes2SpellPolicy::choose(view,environment.get());
	ASSERT_TRUE(selected); ASSERT_EQ(selected->target.size(),1u);
	EXPECT_EQ(selected->target.front().hexValue,goblin->getPosition());
	EXPECT_EQ(attackerSideHero->mana,mana);EXPECT_EQ(server.casts.size(),packets);
	EXPECT_FALSE(archer->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER));
	EXPECT_FALSE(goblin->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER));
	EXPECT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
}
