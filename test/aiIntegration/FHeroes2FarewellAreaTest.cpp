/*
 * FHeroes2FarewellAreaTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include "fheroesStrength/BasicNativeStrengthOracle.h"
#include "server/battles/BattleTestFixture.h"
#include "../../AI/Integration/Ports/FHeroes2SpellPolicy.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include <vcmi/Environment.h>

namespace
{
class FarewellEnvironment final : public Environment
{
	std::shared_ptr<CCallback> gameCallback;
	std::shared_ptr<CBattleCallback> battleCallback;
public:
	FarewellEnvironment(std::shared_ptr<CCallback> game, std::shared_ptr<CBattleCallback> callback)
		: gameCallback(std::move(game)), battleCallback(std::move(callback)) {}
	const Services * services() const override { return LIBRARY; }
	const GameCb * game() const override { return gameCallback.get(); }
	const BattleCb * battle(const BattleID & id) const override { return battleCallback->getBattle(id).get(); }
};
}

class FHeroes2FarewellAreaBattle : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
protected:
	std::shared_ptr<CBattleCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> view;
	std::shared_ptr<FarewellEnvironment> environment;
	CStack * left = nullptr;
	CStack * right = nullptr;
	CStack * alternative = nullptr;
	int64_t raw = 0;
	SpellID spellID() const { return SpellID(GetParam()); }
	void resistance(CStack & unit, int value)
	{
		unit.removeBonuses(Selector::type()(BonusType::MAGIC_RESISTANCE));
		unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE,
			BonusSource::OTHER, value, BonusSourceID()));
	}
	void prepare(int value = 50)
	{
		startGame(); ASSERT_FALSE(HasFatalFailure());
		startBattle(); ASSERT_FALSE(HasFatalFailure());
		for(const auto & spell : LIBRARY->spellh->objects)
			attackerSideHero->removeSpellFromSpellbook(spell->id);
		teachSpell(attackerSideHero, spellID());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 7, ChangeValueMode::ABSOLUTE);
		setMagicMastery(attackerSideHero, MasteryLevel::BASIC);
		callback = std::make_shared<CBattleCallback>(attackerSideHero->getOwner(), nullptr);
		callback->onBattleStarted(battle());
		view = callback->getBattle(battle()->getBattleID());
		environment = std::make_shared<FarewellEnvironment>(makeCallback(attackerSideHero->getOwner()), callback);
		spells::BattleCast cast(view.get(), attackerSideHero, spells::Mode::HERO, spellID().toSpell());
		auto mechanics = spellID().toSpell()->battleMechanics(&cast);
		raw = mechanics->adjustEffectValue(battle()->stacks.front().get());
		ASSERT_GT(raw, 20); ASSERT_LT(raw, 500);
		left = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(74), raw / 10 * 2 / 3);
		right = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(75), raw / 10 * 2 / 3);
		alternative = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(149), (raw + 9) / 10);
		ASSERT_NE(left, nullptr); ASSERT_NE(right, nullptr); ASSERT_NE(alternative, nullptr);
		injure(alternative, alternative->getAvailableHealth() - raw);
		resistance(*left, value); resistance(*right, value);
		auto * reserve = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(156), 1000);
		ASSERT_NE(reserve, nullptr); resistance(*reserve, 100);
		beginCombat(); ASSERT_FALSE(HasFatalFailure());
	}
	std::vector<const CStack *> affected(BattleHex hex)
	{
		spells::BattleCast cast(view.get(), attackerSideHero, spells::Mode::HERO, spellID().toSpell());
		return spellID().toSpell()->battleMechanics(&cast)->getAffectedStacks(spells::Target{battle::Destination(hex)});
	}
	// Pinned damageHeuristic(retreating) and HowManyWillBeKilled, independent
	// of the port helpers. Native affected sets and monster strength are inputs.
	double sourceScore(BattleHex hex, const CStack * active = nullptr)
	{
		spells::BattleCast cast(view.get(), attackerSideHero, spells::Mode::HERO, spellID().toSpell());
		auto mechanics = spellID().toSpell()->battleMechanics(&cast);
		double value = 0;
		for(const auto * unit : affected(hex))
		{
			const auto full = mechanics->adjustEffectValue(unit);
			if(active && active->unitId() == unit->unitId() && full >= unit->getAvailableHealth())
				return -std::numeric_limits<double>::infinity();
			if(unit->isClone()) continue;
			// Fixture damage is <500 and resistance is 0..100; this product cannot wrap.
			const uint32_t damage = static_cast<uint32_t>(full) * (100 - unit->magicResistance()) / 100;
			const auto remaining = std::max<int64_t>(0, unit->getAvailableHealth() - damage);
			const auto hp = unit->getMaxHealth();
			const auto survivors = remaining / hp + (remaining % hp != 0);
			const auto kills = unit->getCount() - survivors;
			value += (view->battleGetOwner(unit) == attackerSideHero->getOwner() ? -1 : 1)
				* FHeroes2StrengthFixture::basicStrength(*unit) / unit->getCount() * kills;
		}
		return value;
	}
	std::optional<BattleAction> choose(const CStack * active = nullptr)
	{
		const auto mana = attackerSideHero->mana;
		const auto packets = server.casts.size();
		std::vector<std::tuple<CStack *, int64_t, int, BattleHex>> snapshot;
		for(const auto & unit : battle()->stacks)
			snapshot.emplace_back(unit.get(), unit->getAvailableHealth(), unit->getCount(), unit->getPosition());
		auto result = AIIntegration::FHeroes2SpellPolicy::choose(view, environment.get(), active, true);
		EXPECT_EQ(attackerSideHero->mana, mana); EXPECT_EQ(server.casts.size(), packets);
		EXPECT_EQ(battle()->stacks.size(), snapshot.size());
		for(const auto & [unit, health, count, position] : snapshot)
		{
			EXPECT_EQ(unit->getAvailableHealth(), health); EXPECT_EQ(unit->getCount(), count);
			EXPECT_EQ(unit->getPosition(), position);
		}
		return result;
	}
	void checkBest(const CStack * active = nullptr)
	{
		ASSERT_TRUE(AIIntegration::Ports::FHeroes2::NativeStrengthModel(*view).usesSourceScale());
		spells::BattleCast cast(view.get(), attackerSideHero, spells::Mode::HERO, spellID().toSpell());
		auto mechanics = spellID().toSpell()->battleMechanics(&cast);
		double best = 0;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex hex(index);
			if(hex.isAvailable() && mechanics->canBeCastAt(spells::Target{battle::Destination(hex)}))
				best = std::max(best, sourceScore(hex, active));
		}
		const auto selected = choose(active);
		ASSERT_EQ(selected.has_value(), best > 0);
		if(selected)
		{
			EXPECT_EQ(selected->spell, spellID());
			ASSERT_EQ(selected->target.size(), 1u);
			EXPECT_DOUBLE_EQ(sourceScore(selected->target.front().hexValue, active), best);
		}
	}
	void isolateLeft()
	{
		for(const auto & unit : battle()->stacks)
			if(unit.get() != left && unit->unitSide() == BattleSide::DEFENDER)
				resistance(*unit, 100);
	}
};

TEST_P(FHeroes2FarewellAreaBattle,discountedKillsRankAgainstUnresistedAlternative)
{
	prepare(); ASSERT_FALSE(HasFatalFailure()); checkBest();
}
TEST_P(FHeroes2FarewellAreaBattle,roundedZeroDamageHasNoFarewellValue)
{
	prepare(99); ASSERT_FALSE(HasFatalFailure());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
	isolateLeft(); checkBest(); EXPECT_FALSE(choose());
}
TEST_P(FHeroes2FarewellAreaBattle,woundedFirstCreatureChangesIntegerKills)
{
	prepare(50); ASSERT_FALSE(HasFatalFailure());
	injure(left, left->getMaxHealth() - 1);
	isolateLeft(); checkBest();
}
TEST_P(FHeroes2FarewellAreaBattle,friendlyCollateralUsesOwnResistance)
{
	prepare(0); ASSERT_FALSE(HasFatalFailure());
	auto * ally = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(73), 10);
	ASSERT_NE(ally, nullptr);
	resistance(*ally, 50); checkBest();
	resistance(*ally, 99); checkBest();
}
TEST_P(FHeroes2FarewellAreaBattle,farewellDoesNotApplyWakePenalty)
{
	prepare(); ASSERT_FALSE(HasFatalFailure()); isolateLeft();
	left->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NOT_ACTIVE,
		BonusSource::OTHER, 1, BonusSourceID()));
	checkBest(); EXPECT_TRUE(choose());
}
TEST_P(FHeroes2FarewellAreaBattle,fullHitActingStackSafetyRemainsConservative)
{
	prepare(0); ASSERT_FALSE(HasFatalFailure());
	auto * active = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(73), 1);
	ASSERT_NE(active, nullptr); resistance(*active, 99);
	checkBest(active);
	const auto selected = choose(active); ASSERT_TRUE(selected);
	const auto units = affected(selected->target.front().hexValue);
	EXPECT_EQ(std::find(units.begin(), units.end(), active), units.end());
}
TEST_P(FHeroes2FarewellAreaBattle,partialCandidatesRejectChangedDamageMetadata)
{
	prepare(); ASSERT_FALSE(HasFatalFailure()); isolateLeft();
	spells::BattleCast cast(view.get(), attackerSideHero, spells::Mode::HERO, spellID().toSpell());
	const auto mechanics = spellID().toSpell()->battleMechanics(&cast);
	auto & effects = const_cast<CSpell::LevelInfo &>(spellID().toSpell()->getLevelInfo(mechanics->getEffectLevel())).battleEffects;
	struct Restore { JsonNode & target; JsonNode value; ~Restore() { target = value; } } restore{effects, effects};
	effects["directDamage"]["killByPercentage"].Bool() = true;
	EXPECT_FALSE(choose());
}
TEST_P(FHeroes2FarewellAreaBattle,partialCandidatesRejectCombatEventAftermath)
{
	prepare(); ASSERT_FALSE(HasFatalFailure()); isolateLeft();
	BonusParametersOnCombatEvent::CombatEffectBonus effect;
	effect.bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID());
	effect.targetEnemy = false;
	BonusParametersOnCombatEvent reaction; reaction.effects.emplace_back(effect);
	auto trigger = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ON_COMBAT_EVENT,
		BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype(static_cast<int>(CombatEventType::SPELL_HIT))));
	trigger->parameters = std::make_shared<BonusParameters>(reaction);
	left->addNewBonus(trigger); EXPECT_FALSE(choose());
}
TEST_P(FHeroes2FarewellAreaBattle,selectedFarewellCastIsAcceptedByServer)
{
	prepare(); ASSERT_FALSE(HasFatalFailure());
	const auto selected = choose(); ASSERT_TRUE(selected); EXPECT_GT(sourceScore(selected->target.front().hexValue), 0);
	std::vector<std::pair<const CStack *, int64_t>> snapshot;
	for(const auto & unit : battle()->stacks) snapshot.emplace_back(unit.get(), unit->getAvailableHealth());
	const auto targets = affected(selected->target.front().hexValue);
	const auto mana = attackerSideHero->mana; const auto packets = server.casts.size();
	ASSERT_TRUE(castAsHero(attackerSideHero, selected->spell, battle::Target{battle::Destination(selected->target.front().hexValue)}));
	EXPECT_LT(attackerSideHero->mana, mana); ASSERT_EQ(server.casts.size(), packets + 1);
	for(const auto & [unit, health] : snapshot)
	{
		const auto loss = health - unit->getAvailableHealth();
		if(std::find(targets.begin(), targets.end(), unit) == targets.end()) EXPECT_EQ(loss, 0);
		else EXPECT_TRUE(loss == 0 || loss == std::min(raw, health));
	}
}

INSTANTIATE_TEST_SUITE_P(StandardArea, FHeroes2FarewellAreaBattle, ::testing::Values(
	int(SpellID::FIREBALL), int(SpellID::FROST_RING), int(SpellID::METEOR_SHOWER), int(SpellID::INFERNO)));
