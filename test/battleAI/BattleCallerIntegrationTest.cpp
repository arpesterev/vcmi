/*
 * BattleCallerIntegrationTest.cpp, part of VCMI experimental tests.
 * GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "BattleCallerFixture.h"
#include "AI/BattleAI/BattleAITestTrace.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

#ifdef VCMI_BATTLE_AI_TEST_TRACE
namespace test
{
namespace callerprobe
{
struct EstimateRecord
{
	uint32_t attacker;
	uint32_t target;
	std::uintptr_t expectedState;
	std::uintptr_t suppliedState;
	bool expectedShooting;
	bool suppliedShooting;
	int64_t expectedAttackerHealth;
	int64_t suppliedAttackerHealth;
};

struct Recorder : BattleAITestTrace::Sink
{
	const HypotheticBattle * expectedState = nullptr;
	bool expectedShooting = false;
	std::vector<EstimateRecord> estimates;
	std::vector<std::pair<uint32_t, uint32_t>> choices;

	void receive(const BattleAITestTrace::Event & event) override
	{
		if(event.kind == BattleAITestTrace::Kind::Selection)
		{
			expectedState = event.state;
			expectedShooting = event.shooting;
		}
		else if(event.kind == BattleAITestTrace::Kind::Estimate && expectedState)
		{
			const auto id = event.attacker->unitId();
			const auto * expected = expectedState->battleGetUnitByID(id);
			const auto * supplied = event.state->battleGetUnitByID(id);
			estimates.push_back({id, event.target->unitId(),
				reinterpret_cast<std::uintptr_t>(expectedState), reinterpret_cast<std::uintptr_t>(event.state),
				expectedShooting, event.shooting,
				expected->getAvailableHealth(), supplied->getAvailableHealth()});
		}
		else if(event.kind == BattleAITestTrace::Kind::Choice)
			choices.emplace_back(event.attacker->unitId(), event.target->unitId());
	}
};

class BattleCallerIntegrationTest : public Test
{
protected:
	void runCallerProbe(bool shootingCase)
	{
		Scenario scenario(shootingCase ? modeProbeSpecs() : stateProbeSpecs(10, 10));
		scenario.populateCache();
		auto active = scenario.state->getForUpdate(1);
		PotentialTargets targets(active.get(), scenario.cache, scenario.state);
		auto selected = std::find_if(targets.possibleAttacks.begin(), targets.possibleAttacks.end(),
			[&](const AttackPossibility & attack)
			{
				return attack.attack.defender->unitId() == 3 && attack.from == active->getPosition();
			});
		ASSERT_NE(selected, targets.possibleAttacks.end()) << "PROBE_SETUP: missing legal stationary A->D attack";
		ASSERT_TRUE(selected->attackerState);
		ASSERT_FALSE(selected->defenderDead);
		auto evaluator = scenario.evaluator(1);
		evaluator.updateReachabilityMap(scenario.state);
		const auto exchange = evaluator.getExchangeUnits(*selected, 0, targets, scenario.state);
		ASSERT_FALSE(exchange.units.empty()) << "PROBE_SETUP: empty real turn queue";
		ASSERT_FALSE(exchange.units.at(0).empty());
		ASSERT_EQ(exchange.units.at(0).front()->unitId(), 1u) << "PROBE_SETUP: A must move first";
		std::set<uint32_t> accessible;
		for(const auto * unit : exchange.melleeAccessible)
			accessible.insert(unit->unitId());
		ASSERT_TRUE(accessible.count(1) && accessible.count(2))
			<< "PROBE_SETUP: A and B must survive inaccessible-shooter filtering";

		const auto before = scenario.snapshot();
		const auto activeBefore = scenario.state->getActiveStackID();
		Recorder recorder;
		float score;
		{
			BattleAITestTrace::ScopedSink trace(recorder);
			score = evaluator.evaluateExchange(*selected, 0, targets, scenario.cache, scenario.state);
		}
		ASSERT_TRUE(std::isfinite(score));
		EXPECT_EQ(scenario.snapshot(), before);
		EXPECT_EQ(scenario.state->getActiveStackID(), activeBefore);

		const uint32_t actor = shootingCase ? 4u : 3u;
		std::set<uint32_t> candidates;
		size_t observations = 0;
		for(const auto & event : recorder.estimates)
		{
			if(event.attacker != actor)
				continue;
			++observations;
			candidates.insert(event.target);
			if(shootingCase)
			{
				ASSERT_TRUE(event.expectedShooting) << "PROBE_SETUP: E must be an unblocked shooter";
				ASSERT_FALSE(scenario.state->battleCanShoot(scenario.state->battleGetUnitByID(event.target)))
					<< "PROBE_SETUP: opposite candidate must not shoot";
				EXPECT_EQ(event.suppliedShooting, event.expectedShooting) << "MODE_ARGUMENT";
			}
			else
			{
				ASSERT_LT(event.expectedAttackerHealth, scenario.state->getForUpdate(actor)->getAvailableHealth())
					<< "PROBE_SETUP: first attack must damage D in exchange state only";
				EXPECT_EQ(event.suppliedState, event.expectedState) << "STATE_ARGUMENT";
				EXPECT_EQ(event.suppliedAttackerHealth, event.expectedAttackerHealth) << "STATE_HEALTH_ARGUMENT";
			}
		}
		ASSERT_GT(observations, 0u) << "PROBE_SETUP: changed selector was not entered";
		EXPECT_TRUE(candidates.count(1) && candidates.count(2)) << "PROBE_SETUP: both candidate IDs required";
		EXPECT_TRUE(std::any_of(recorder.choices.begin(), recorder.choices.end(),
			[actor](const std::pair<uint32_t, uint32_t> & choice) { return choice.first == actor; }))
			<< "PROBE_SETUP: observed estimates must lead to a real target choice";
	}
};

TEST_F(BattleCallerIntegrationTest, PassesActingShootersModeIntoRealSelector)
{
	runCallerProbe(true);
}

TEST_F(BattleCallerIntegrationTest, PassesPostStrikeExchangeStateIntoRealSelector)
{
	runCallerProbe(false);
}

TEST_F(BattleCallerIntegrationTest, PublicFinderSelectsHighestScoredLegalCandidateWithoutWaitingAgain)
{
	Scenario scenario(modeProbeSpecs());
	scenario.populateCache();
	auto active = scenario.state->getForUpdate(1);
	// Suppress the independent wait branch, but exercise the real waited turn order.
	scenario.state->makeWait(active.get());
	PotentialTargets targets(active.get(), scenario.cache, scenario.state);
	ASSERT_GE(targets.possibleAttacks.size(), 2u);
	std::set<uint32_t> enemyIds;
	for(const auto & attack : targets.possibleAttacks)
		enemyIds.insert(attack.attack.defender->unitId());
	ASSERT_GE(enemyIds.size(), 2u) << "PROBE_SETUP: need distinct initial target identities";
	auto evaluator = scenario.evaluator(1);
	evaluator.updateReachabilityMap(scenario.state);
	const auto before = scenario.snapshot();
	const auto activeBefore = scenario.state->getActiveStackID();
	size_t best = 0;
	float bestScore = -std::numeric_limits<float>::infinity();
	for(size_t index = 0; index < targets.possibleAttacks.size(); ++index)
	{
		const float score = evaluator.evaluateExchange(targets.possibleAttacks[index],
			0, targets, scenario.cache, scenario.state);
		ASSERT_TRUE(std::isfinite(score));
		if(score > bestScore)
		{
			best = index;
			bestScore = score;
		}
	}
	EXPECT_EQ(scenario.snapshot(), before);
	EXPECT_EQ(scenario.state->getActiveStackID(), activeBefore);
	const auto result = evaluator.findBestTarget(active.get(), targets, scenario.cache, scenario.state);
	EXPECT_FALSE(result.wait);
	EXPECT_EQ(result.bestAttack.attack.defender->unitId(), targets.possibleAttacks[best].attack.defender->unitId());
	EXPECT_EQ(result.bestAttack.from, targets.possibleAttacks[best].from);
	EXPECT_EQ(result.bestAttack.attack.shooting, targets.possibleAttacks[best].attack.shooting);
	EXPECT_FLOAT_EQ(result.score, bestScore);
	EXPECT_EQ(scenario.snapshot(), before);
	EXPECT_EQ(scenario.state->getActiveStackID(), activeBefore);
}
}
}
#endif

