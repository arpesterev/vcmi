/*
 * BattleStateRegressionTest.cpp, part of VCMI engine.
 * GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "BattleCallerFixture.h"
#include "AI/BattleAI/BattleEvaluator.h"
#include "lib/battle/CObstacleInstance.h"

namespace test
{
namespace callerprobe
{
TEST(BattleStateRegression, ReachabilityRebuildUsesMovedStateNotOriginalCallback)
{
	Scenario scenario({
		{1, BattleSide::ATTACKER, BattleHex(3, 2), 10, 100, 10, 2, false},
		{2, BattleSide::DEFENDER, BattleHex(14, 7), 10, 100, 10, 2, false}});
	auto evaluator = scenario.evaluator(1);
	auto containsActor = [](const ::battle::Units & units)
	{
		return std::any_of(units.begin(), units.end(), [](const ::battle::Unit * unit)
		{
			return unit->unitId() == 1;
		});
	};
	evaluator.updateReachabilityMap(scenario.state);
	ASSERT_TRUE(containsActor(evaluator.getOneTurnReachableUnits(0, BattleHex(3, 3))));
	auto moved = std::make_shared<HypotheticBattle>(scenario.environment.get(), scenario.state);
	moved->moveUnit(1, BattleHex(13, 8));
	evaluator.updateReachabilityMap(moved);
	EXPECT_FALSE(containsActor(evaluator.getOneTurnReachableUnits(0, BattleHex(3, 3))));
	EXPECT_TRUE(containsActor(evaluator.getOneTurnReachableUnits(0, BattleHex(12, 8))));
	EXPECT_EQ(scenario.state->battleGetUnitByID(1)->getPosition(), BattleHex(3, 2));
}

TEST(BattleStateRegression, SpellGeometryDetectsTeleportAndDeathButNotDamageAlone)
{
	Scenario scenario(stateProbeSpecs(10, 10));
	auto changed = std::make_shared<HypotheticBattle>(scenario.environment.get(), scenario.state);
	EXPECT_FALSE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	int64_t damage = 1;
	changed->getForUpdate(1)->damage(damage);
	EXPECT_FALSE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	changed->moveUnit(1, BattleHex(3, 3));
	EXPECT_TRUE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	changed->moveUnit(1, scenario.state->battleGetUnitByID(1)->getPosition());
	damage = 100000;
	changed->getForUpdate(3)->damage(damage);
	EXPECT_TRUE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
}

TEST(BattleStateRegression, HypotheticalObstacleChangesAreVisibleAndIsolated)
{
	Scenario scenario(stateProbeSpecs(10, 10));
	SpellCreatedObstacle obstacle;
	obstacle.uniqueID = 41;
	obstacle.pos = BattleHex(8, 5);
	obstacle.hidden = false;
	obstacle.revealed = false;
	obstacle.passable = false;
	obstacle.customSize.insert(obstacle.pos);
	ObstacleChanges add(41, BattleChanges::EOperation::ADD);
	obstacle.toInfo(add);
	scenario.state->addObstacle(add);
	ASSERT_EQ(scenario.state->getAllObstacles().size(), 1u);
	auto changed = std::make_shared<HypotheticBattle>(scenario.environment.get(), scenario.state);
	ASSERT_FALSE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	obstacle.revealed = true;
	ObstacleChanges reveal(41, BattleChanges::EOperation::UPDATE);
	obstacle.toInfo(reveal, BattleChanges::EOperation::UPDATE);
	changed->updateObstacle(reveal);
	ASSERT_EQ(changed->getAllObstacles().size(), 1u);
	EXPECT_TRUE(dynamic_cast<const SpellCreatedObstacle *>(changed->getAllObstacles().front().get())->revealed);
	EXPECT_FALSE(dynamic_cast<const SpellCreatedObstacle *>(scenario.state->getAllObstacles().front().get())->revealed);
	EXPECT_TRUE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	changed->removeObstacle(41);
	EXPECT_TRUE(changed->getAllObstacles().empty());
	EXPECT_EQ(scenario.state->getAllObstacles().size(), 1u);
	EXPECT_TRUE(BattleEvaluator::spellChangesGeometry(*scenario.state, *changed));
	EXPECT_TRUE(scenario.callback->battleGetAllObstacles().empty());
}

TEST(BattleStateRegression, EnemySummonLossIsNotDiscardedAsOurSummonGain)
{
	EXPECT_TRUE(BattleEvaluator::isRedundantSummonHealthGain(1, 1, true));
	EXPECT_FALSE(BattleEvaluator::isRedundantSummonHealthGain(-1, -1, true));
	EXPECT_FALSE(BattleEvaluator::isRedundantSummonHealthGain(1, -1, true));
	EXPECT_FALSE(BattleEvaluator::isRedundantSummonHealthGain(-1, 1, true));
	EXPECT_FALSE(BattleEvaluator::isRedundantSummonHealthGain(1, 1, false));
}
TEST(BattleStateRegression, ApplyingEvaluatedAttackPreservesFullUnitState)
{
	Scenario scenario(stateProbeSpecs(10, 10));
	scenario.populateCache();
	auto active = scenario.state->getForUpdate(1);
	PotentialTargets targets(active.get(), scenario.cache, scenario.state);
	const auto selected = std::find_if(targets.possibleAttacks.begin(), targets.possibleAttacks.end(),
		[&](const AttackPossibility & attack)
		{
			return attack.attack.defender->unitId() == 3 && attack.from != active->getPosition();
		});
	ASSERT_NE(selected, targets.possibleAttacks.end());
	auto applied = std::make_shared<HypotheticBattle>(scenario.environment.get(), scenario.state);
	const auto before = scenario.snapshot();
	BattleExchangeVariant variant;
	variant.trackAttack(*selected, applied, scenario.cache);
	JsonNode expected, actual;
	selected->attackerState->save(expected);
	applied->getForUpdate(1)->save(actual);
	EXPECT_EQ(actual, expected);
	for(const auto & affected : selected->affectedUnits)
	{
		affected->save(expected);
		applied->getForUpdate(affected->unitId())->save(actual);
		EXPECT_EQ(actual, expected);
	}
	EXPECT_EQ(scenario.snapshot(), before);
}

TEST(BattleStateRegression, NegativeSpellDoesNotBeatMissingOrLosingAttack)
{
	EXPECT_FALSE(BattleEvaluator::isSpellWorthCasting(-1, EvaluationResult::INEFFECTIVE_SCORE));
	EXPECT_FALSE(BattleEvaluator::isSpellWorthCasting(-1, -10));
	EXPECT_FALSE(BattleEvaluator::isSpellWorthCasting(0, -10));
	EXPECT_TRUE(BattleEvaluator::isSpellWorthCasting(1, -10));
	EXPECT_FALSE(BattleEvaluator::isSpellWorthCasting(5, 10));
	EXPECT_FALSE(BattleEvaluator::isSpellWorthCasting(10, 10));
	EXPECT_TRUE(BattleEvaluator::isSpellWorthCasting(11, 10));
}

}
}
