/*
 * BattleExchangeVariantTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later.
 * Full text of license available in license.txt file, in main folder.
 */
#include "StdInc.h"

#include "../mock/BattleFake.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_Environment.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "AI/BattleAI/BattleExchangeVariant.h"
#include "lib/CCreatureHandler.h"
#include "lib/json/JsonNode.h"

#include <limits>

namespace test
{
namespace
{
using namespace ::testing;

// All mutable combat state is production CUnitState, not UnitMock.
struct ExchangeTestUnit
{
	NiceMock<UnitInfoMock> info;
	BonusBearerMock bonuses;
	NiceMock<UnitEnvironmentMock> environment;
	::battle::CUnitStateDetached state;

	ExchangeTestUnit(uint32_t id, BattleSide side, BattleHex position, int damage)
		: state(&info, &bonuses)
	{
		ON_CALL(info, unitId()).WillByDefault(Return(id));
		ON_CALL(info, unitSide()).WillByDefault(Return(side));
		ON_CALL(info, unitOwner()).WillByDefault(Return(PlayerColor(side == BattleSide::ATTACKER ? 0 : 1)));
		ON_CALL(info, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(0).toCreature()));
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(10));
		ON_CALL(environment, unitHasAmmoCart(_)).WillByDefault(Return(false));
		addBonus(BonusType::STACK_HEALTH, 100);
		addBonus(BonusType::CREATURE_DAMAGE, damage, BonusCustomSubtype::creatureDamageBoth);
		addBonus(BonusType::STACKS_SPEED, 5);
		// Eliminate the ordinary shooter's melee penalty in these mode controls.
		addBonus(BonusType::NO_MELEE_PENALTY, 1);
		if(side == BattleSide::ATTACKER)
		{
			addBonus(BonusType::SHOOTER, 1);
			addBonus(BonusType::SHOTS, 12);
		}
		state.localInit(&environment);
		state.position = position;
	}

	void addBonus(BonusType type, int value, BonusSubtypeID subtype = BonusSubtypeID())
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, type, BonusSource::CREATURE_ABILITY,
			value, BonusSourceID(), subtype));
	}
};

class BattleExchangeEstimateTest : public Test
{
protected:
	NiceMock<EnvironmentMock> environment;
	BonusBearerMock battleBonuses;
	std::vector<std::unique_ptr<ExchangeTestUnit>> sourceUnits;
	std::shared_ptr<NiceMock<test::battle::BattleFake>> realBattle;
	std::shared_ptr<HypotheticBattle> battle;
	std::shared_ptr<StackWithBonuses> attacker;
	std::shared_ptr<StackWithBonuses> defender;
	std::shared_ptr<StackWithBonuses> alternative;
	DamageCache originalCache;
	DamageCache cache{&originalCache};

	void SetUp() override
	{
#if SCRIPTING_ENABLED
		realBattle = std::make_shared<NiceMock<test::battle::BattleFake>>(
			std::shared_ptr<scripting::PoolMock>());
#else
		realBattle = std::make_shared<NiceMock<test::battle::BattleFake>>();
#endif
		sourceUnits.emplace_back(new ExchangeTestUnit(1, BattleSide::ATTACKER, BattleHex(5, 5), 10));
		sourceUnits.emplace_back(new ExchangeTestUnit(2, BattleSide::DEFENDER, BattleHex(6, 5), 10));
		sourceUnits.emplace_back(new ExchangeTestUnit(3, BattleSide::DEFENDER, BattleHex(5, 6), 9));

		ON_CALL(*realBattle, getUnitsIf(_)).WillByDefault(Invoke(
			[this](const ::battle::UnitFilter & predicate)
			{
				::battle::Units units;
				for(const auto & unit : sourceUnits)
					if(predicate(&unit->state))
						units.push_back(&unit->state);
				return units;
			}));
		ON_CALL(*realBattle, getActiveStackID()).WillByDefault(Return(1));
		ON_CALL(*realBattle, getBonusBearer()).WillByDefault(Return(&battleBonuses));
		ON_CALL(*realBattle, getSidePlayer(_)).WillByDefault(Invoke(
			[](BattleSide side) { return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); }));
		realBattle->setupEmptyBattlefield();

		battle = std::make_shared<HypotheticBattle>(&environment, realBattle);
		attacker = battle->getForUpdate(1);
		defender = battle->getForUpdate(2);
		alternative = battle->getForUpdate(3);
		for(const auto & target : {defender, alternative})
		{
			originalCache.cacheDamage(attacker.get(), target.get(), battle);
			originalCache.cacheDamage(target.get(), attacker.get(), battle);
			cache.cacheDamage(attacker.get(), target.get(), battle);
			cache.cacheDamage(target.get(), attacker.get(), battle);
		}
		ASSERT_EQ(attacker->getCount(), 10);
		ASSERT_EQ(defender->getCount(), 10);
		ASSERT_EQ(cache.getDamage(attacker.get(), defender.get(), battle), 100);
		ASSERT_TRUE(defender->ableToRetaliate());
	}

	float estimate(const std::shared_ptr<StackWithBonuses> & target, bool shooting = false)
	{
		BattleExchangeVariant evaluator;
		const auto result = evaluator.trackAttack(attacker, target, shooting, true, cache, battle, true);
		EXPECT_FLOAT_EQ(evaluator.getScore().ourDamageReduce, 0);
		EXPECT_FLOAT_EQ(evaluator.getScore().enemyDamageReduce, 0);
		return result;
	}

	struct Execution
	{
		float score;
		int64_t attackerHealth;
		int64_t defenderHealth;
		int32_t attackerShots;
	};

	Execution executePrivately(const std::shared_ptr<StackWithBonuses> & target, bool shooting = false)
	{
		auto copy = std::make_shared<HypotheticBattle>(&environment, battle);
		auto a = copy->getForUpdate(attacker->unitId());
		auto d = copy->getForUpdate(target->unitId());
		DamageCache copyCache = cache;
		BattleExchangeVariant executor;
		const auto score = executor.trackAttack(a, d, shooting, true, copyCache, copy, false);
		return {score, a->getAvailableHealth(), d->getAvailableHealth(), a->shots.available()};
	}

	float inflictedValue(const std::shared_ptr<StackWithBonuses> & target)
	{
		return AttackPossibility::calculateDamageReduce(attacker.get(), target.get(),
			cache.getDamage(attacker.get(), target.get(), battle), cache, battle);
	}

	std::vector<JsonNode> snapshots()
	{
		std::vector<JsonNode> result;
		for(const auto & source : sourceUnits)
		{
			JsonNode data;
			source->state.save(data);
			result.push_back(data);
		}
		for(const auto & state : {attacker, defender, alternative})
		{
			JsonNode data;
			state->save(data);
			result.push_back(data);
		}
		return result;
	}
};

TEST_F(BattleExchangeEstimateTest, AccountsForPostHitRetaliationWithoutMutatingParents)
{
	const auto before = snapshots();
	const auto execution = executePrivately(defender);
	ASSERT_LT(execution.attackerHealth, attacker->getAvailableHealth());
	ASSERT_LT(execution.defenderHealth, defender->getAvailableHealth());
	EXPECT_LT(execution.score, inflictedValue(defender));
	EXPECT_FLOAT_EQ(estimate(defender), execution.score);
	EXPECT_EQ(snapshots(), before);
}

TEST_F(BattleExchangeEstimateTest, RanksSafeTargetAboveHigherGrossDamageTarget)
{
	alternative->afterAttack(false, true);
	ASSERT_FALSE(alternative->ableToRetaliate());
	ASSERT_GT(inflictedValue(defender), inflictedValue(alternative));
	ASSERT_LT(executePrivately(defender).score, executePrivately(alternative).score);
	EXPECT_GT(estimate(alternative), estimate(defender));
}

TEST_F(BattleExchangeEstimateTest, RangedAttackSpendsOnlyPrivateAmmoAndDoesNotRetaliate)
{
	const auto before = snapshots();
	const auto execution = executePrivately(defender, true);
	EXPECT_EQ(execution.attackerHealth, attacker->getAvailableHealth());
	EXPECT_EQ(execution.attackerShots, attacker->shots.available() - 1);
	EXPECT_FLOAT_EQ(estimate(defender, true), inflictedValue(defender));
	EXPECT_FLOAT_EQ(estimate(defender, true), execution.score);
	// Same shooter acting in melee must instead include retaliation.
	EXPECT_LT(estimate(defender, false), estimate(defender, true));
	EXPECT_EQ(snapshots(), before);
}

TEST_F(BattleExchangeEstimateTest, InheritsParentLocalBlocksRetaliationBonus)
{
	const auto realBefore = sourceUnits[0]->state.hasBonusOfType(BonusType::BLOCKS_RETALIATION);
	Bonus bonus(BonusDuration::PERMANENT, BonusType::BLOCKS_RETALIATION,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID());
	attacker->addUnitBonus(std::vector<Bonus>{bonus});
	ASSERT_TRUE(attacker->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
	const auto before = snapshots();
	EXPECT_FLOAT_EQ(estimate(defender), inflictedValue(defender));
	EXPECT_FLOAT_EQ(estimate(defender), executePrivately(defender).score);
	EXPECT_EQ(snapshots(), before);
	EXPECT_EQ(sourceUnits[0]->state.hasBonusOfType(BonusType::BLOCKS_RETALIATION), realBefore);
	EXPECT_TRUE(attacker->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
}

TEST_F(BattleExchangeEstimateTest, SpentRetaliationIsPreservedInChildState)
{
	defender->afterAttack(false, true);
	ASSERT_FALSE(defender->ableToRetaliate());
	ASSERT_TRUE(sourceUnits[1]->state.ableToRetaliate());
	const auto before = snapshots();
	EXPECT_FLOAT_EQ(estimate(defender), inflictedValue(defender));
	EXPECT_FLOAT_EQ(estimate(defender), executePrivately(defender).score);
	EXPECT_EQ(snapshots(), before);
}

TEST_F(BattleExchangeEstimateTest, PartialCasualtiesReduceRetaliationAndLethalHitPreventsIt)
{
	// Exactly one defender is killed: 9 survivors retaliate for 90, not 100.
	const auto partial = executePrivately(defender);
	EXPECT_EQ(partial.defenderHealth, 900);
	EXPECT_EQ(partial.attackerHealth, 910);
	EXPECT_FLOAT_EQ(estimate(defender), partial.score);

	int64_t damage = defender->getAvailableHealth() - 50;
	defender->damage(damage);
	ASSERT_EQ(defender->getAvailableHealth(), 50);
	const auto before = snapshots();
	const auto lethal = executePrivately(defender);
	EXPECT_EQ(lethal.defenderHealth, 0);
	EXPECT_EQ(lethal.attackerHealth, attacker->getAvailableHealth());
	EXPECT_FLOAT_EQ(estimate(defender), inflictedValue(defender));
	EXPECT_FLOAT_EQ(estimate(defender), lethal.score);
	EXPECT_EQ(snapshots(), before);
}

TEST_F(BattleExchangeEstimateTest, RejectsDeadAndNullEstimateInputs)
{
	int64_t damage = defender->getAvailableHealth();
	defender->damage(damage);
	ASSERT_FALSE(defender->alive());
	const auto before = snapshots();
	EXPECT_EQ(estimate(defender), std::numeric_limits<float>::lowest());
	BattleExchangeVariant evaluator;
	EXPECT_EQ(evaluator.trackAttack(nullptr, alternative, false, true, cache, battle, true),
		std::numeric_limits<float>::lowest());
	EXPECT_EQ(evaluator.trackAttack(attacker, nullptr, false, true, cache, battle, true),
		std::numeric_limits<float>::lowest());
	EXPECT_EQ(evaluator.trackAttack(attacker, alternative, false, true, cache, nullptr, true),
		std::numeric_limits<float>::lowest());
	EXPECT_EQ(snapshots(), before);
}

TEST_F(BattleExchangeEstimateTest, RepeatedCandidatesDoNotContaminateSiblingsOrParentScores)
{
	battle->makeWait(attacker.get());
	int64_t damage = 37;
	attacker->damage(damage);
	const auto before = snapshots();
	const auto activeBefore = battle->getActiveStackID();
	const auto first = estimate(defender);
	const auto second = estimate(alternative);
	EXPECT_FLOAT_EQ(estimate(alternative), second);
	EXPECT_FLOAT_EQ(estimate(defender), first);
	EXPECT_FLOAT_EQ(first, executePrivately(defender).score);
	EXPECT_EQ(snapshots(), before);
	EXPECT_EQ(battle->getActiveStackID(), activeBefore);
	EXPECT_TRUE(attacker->waitedThisTurn);
	EXPECT_EQ(sourceUnits[0]->state.getAvailableHealth(), 1000);
}
}
}

