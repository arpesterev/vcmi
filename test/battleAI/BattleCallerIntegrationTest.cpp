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
TEST_F(BattleCallerIntegrationTest, WaitedPrimaryAttackRecomputesCurrentStateAndOccupiesAttackHex)
{
	Scenario scenario(stateProbeSpecs(10, 10));
	scenario.populateCache();
	auto active = scenario.state->getForUpdate(1);
	scenario.state->makeWait(active.get());
	PotentialTargets targets(active.get(), scenario.cache, scenario.state);
	const auto selected = std::find_if(targets.possibleAttacks.begin(), targets.possibleAttacks.end(),
		[&](const AttackPossibility & attack)
		{
			return attack.attack.defender->unitId() == 3 && attack.from != active->getPosition();
		});
	ASSERT_NE(selected, targets.possibleAttacks.end());
	auto evaluator = scenario.evaluator(1);
	evaluator.updateReachabilityMap(scenario.state);
	const auto exchange = evaluator.getExchangeUnits(*selected, 0, targets, scenario.state);
	ASSERT_FALSE(exchange.units.empty());
	ASSERT_NE(exchange.units.at(0).front()->unitId(), 1u);
	struct PrimaryRecorder : BattleAITestTrace::Sink
	{
		size_t calls = 0;
		BattleHex expectedPosition;
		explicit PrimaryRecorder(BattleHex position) : expectedPosition(position) {}
		void receive(const BattleAITestTrace::Event & event) override
		{
			if(event.kind != BattleAITestTrace::Kind::PrimaryAttack)
				return;
			++calls;
			ASSERT_NE(event.attack, nullptr);
			EXPECT_EQ(event.attack->attack.attacker, event.state->battleGetUnitByID(1));
			EXPECT_EQ(event.attack->attack.defender,
				event.state->battleGetUnitByID(event.target->unitId()));
			EXPECT_EQ(event.attacker->getPosition(), expectedPosition);
			EXPECT_EQ(event.attacker->getAvailableHealth(), event.attack->attackerState->getAvailableHealth());
		}
	} recorder(selected->from);
	const auto before = scenario.snapshot();
	{
		BattleAITestTrace::ScopedSink trace(recorder);
		EXPECT_TRUE(std::isfinite(evaluator.evaluateExchange(*selected, 0, targets, scenario.cache, scenario.state)));
	}
	EXPECT_EQ(recorder.calls, 1u);
	EXPECT_EQ(scenario.snapshot(), before);
}

}
}
#endif

#ifdef VCMI_BATTLE_AI_TEST_TRACE
#include <algorithm>
#include <tuple>

namespace test
{
namespace callerprobe
{
namespace singleeval
{
using UnitState = std::tuple<uint32_t, int64_t, int32_t, bool, int>;

struct SelectionObservation
{
	uint32_t actor = 0;
	std::vector<uint32_t> candidates;
	std::vector<uint32_t> estimates;
	std::vector<float> scores;
	std::vector<UnitState> before;
	size_t legacyCalls = 0;
	std::optional<uint32_t> expectedChoice;
	std::optional<uint32_t> choice;
	bool executionChecked = false;
};

// All score/value expectations run production first strikes on private children.
// The old comparator is intentionally retained only as a test oracle. Its nested
// Estimate trace events must not contribute to the real caller's call count.
struct SelectionRecorder : BattleAITestTrace::Sink
{
	Scenario & scenario;
	std::vector<uint32_t> candidateIds;
	std::set<uint32_t> actors;
	std::vector<SelectionObservation> selections;
	bool insideOracle = false;
	std::optional<size_t> active;
	std::optional<size_t> pendingExecution;
	const HypotheticBattle * selectionState = nullptr;
	const HypotheticBattle * executionState = nullptr;
	bool selectionShooting = false;
	std::vector<UnitState> expectedAfter;

	SelectionRecorder(Scenario & scenario, std::vector<uint32_t> candidates, std::set<uint32_t> actors)
		: scenario(scenario), candidateIds(std::move(candidates)), actors(std::move(actors)) {}

	std::vector<UnitState> snapshot(const HypotheticBattle & state) const
	{
		std::vector<UnitState> result;
		for(const auto & source : scenario.sources)
		{
			const auto * unit = state.battleGetUnitByID(source->state.unitId());
			result.emplace_back(unit->unitId(), unit->getAvailableHealth(), unit->acquireState()->shots.available(),
				unit->ableToRetaliate(), unit->getPosition().toInt());
		}
		return result;
	}

	struct OracleResult
	{
		float score;
		std::vector<UnitState> after;
	};

	OracleResult oracle(const BattleAITestTrace::Event & event, uint32_t target, bool estimate)
	{
		struct Guard
		{
			bool & value;
			explicit Guard(bool & value) : value(value) { value = true; }
			~Guard() { value = false; }
		} guard(insideOracle);
		// This non-owning subject lives only during the synchronous trace callback.
		auto subject = std::shared_ptr<HypotheticBattle>(const_cast<HypotheticBattle *>(event.state),
			[](HypotheticBattle *) {});
		auto child = std::make_shared<HypotheticBattle>(event.state->env, subject);
		auto attacker = child->getForUpdate(event.attacker->unitId());
		auto defender = child->getForUpdate(target);
		DamageCache cache = scenario.cache;
		BattleExchangeVariant firstStrike;
		const bool isOur = child->battleMatchOwner(child->battleGetUnitByID(1), attacker.get(), true);
		const float score = firstStrike.trackAttack(attacker, defender, event.shooting, isOur, cache, child, estimate);
		return {score, snapshot(*child)};
	}

	void receive(const BattleAITestTrace::Event & event) override
	{
		if(insideOracle)
			return;
		if(pendingExecution)
		{
			ASSERT_EQ(event.state, executionState);
			EXPECT_EQ(snapshot(*event.state), expectedAfter) << "REAL_CHOSEN_ACTION_STATE";
			selections[*pendingExecution].executionChecked = true;
			pendingExecution.reset();
		}
		if(event.kind == BattleAITestTrace::Kind::Selection)
		{
			active.reset();
			if(!actors.count(event.attacker->unitId()))
				return;
			selectionState = event.state;
			selectionShooting = event.shooting;
			selections.emplace_back();
			active = selections.size() - 1;
			auto & observation = selections.back();
			observation.actor = event.attacker->unitId();
			observation.candidates = candidateIds;
			observation.before = snapshot(*event.state);
			for(uint32_t id : candidateIds)
			{
				ASSERT_TRUE(event.state->battleGetUnitByID(id)->alive()) << "PROBE_SETUP: candidate died";
				const auto estimate = oracle(event, id, true);
				const auto execution = oracle(event, id, false);
				ASSERT_TRUE(std::isfinite(estimate.score));
				EXPECT_FLOAT_EQ(estimate.score, execution.score) << "FIRST_STRIKE_VALUE_PARITY";
				observation.scores.push_back(execution.score);
			}
			const auto legacy = std::max_element(candidateIds.begin(), candidateIds.end(),
				[&](uint32_t left, uint32_t right)
				{
					const float leftScore = oracle(event, left, true).score;
					const float rightScore = oracle(event, right, true).score;
					observation.legacyCalls += 2;
					return leftScore < rightScore;
				});
			if(legacy != candidateIds.end())
				observation.expectedChoice = *legacy;
			EXPECT_EQ(snapshot(*event.state), observation.before) << "ORACLE_MUST_NOT_CHANGE_EXCHANGE";
		}
		else if(event.kind == BattleAITestTrace::Kind::Estimate && active)
		{
			auto & observation = selections[*active];
			EXPECT_EQ(event.attacker->unitId(), observation.actor);
			EXPECT_EQ(event.state, selectionState);
			EXPECT_EQ(event.shooting, selectionShooting);
			EXPECT_EQ(snapshot(*event.state), observation.before) << "ESTIMATES_MUST_SHARE_CURRENT_STATE";
			observation.estimates.push_back(event.target->unitId());
		}
		else if(event.kind == BattleAITestTrace::Kind::Choice && active)
		{
			auto & observation = selections[*active];
			ASSERT_EQ(event.attacker->unitId(), observation.actor);
			observation.choice = event.target->unitId();
			EXPECT_EQ(observation.choice, observation.expectedChoice) << "LEGACY_CHOICE_PARITY";
			ASSERT_EQ(event.attacker->getTotalAttacks(event.shooting), 1) << "PROBE_SETUP: one first strike";
			const auto actualChoice = oracle(event, *observation.choice, false);
			const auto chosen = std::find(candidateIds.begin(), candidateIds.end(), *observation.choice);
			ASSERT_NE(chosen, candidateIds.end());
			EXPECT_FLOAT_EQ(actualChoice.score, observation.scores[chosen - candidateIds.begin()]);
			expectedAfter = actualChoice.after;
			executionState = event.state;
			pendingExecution = active;
			active.reset();
		}
	}
};

std::vector<UnitSpec> queueSpecs(size_t count, bool tie = false)
{
	std::vector<UnitSpec> result = {
		{1, BattleSide::ATTACKER, BattleHex(5, 5), 30, 100, tie ? 10 : 8, 20, false},
		{3, BattleSide::DEFENDER, BattleHex(6, 5), 30, 100, tie ? 0 : 5, 12, false},
		{4, BattleSide::DEFENDER, BattleHex(12, 3), 30, 100, 7, 15, true}
	};
	const std::vector<BattleHex> positions = {BattleHex(6, 6), BattleHex(4, 5), BattleHex(5, 4)};
	const std::vector<uint32_t> ids = {2, 5, 6};
	for(size_t i = 1; i < count; ++i)
		result.push_back({ids.at(i - 1), BattleSide::ATTACKER, positions.at(i - 1),
			30, 100, tie ? 10 : static_cast<int>(8 + i), static_cast<int>(9 - i), false});
	return result;
}

std::vector<UnitSpec> fallbackSpecs(size_t count, int enemySpeed = 20)
{
	std::vector<UnitSpec> result = {
		{1, BattleSide::ATTACKER, BattleHex(3, 3), 30, 100, 5, 30, true},
		{3, BattleSide::DEFENDER, BattleHex(12, 5), 30, 100, 7, enemySpeed, false}
	};
	if(count > 1)
		result.push_back({2, BattleSide::ATTACKER, BattleHex(3, 6), 30, 100, 5, 10, true});
	if(count > 2)
		result.push_back({5, BattleSide::ATTACKER, BattleHex(3, 9), 30, 100, 5, 9, true});
	return result;
}

class BattleSingleEvalSelectionTest : public Test
{
protected:
	void run(const std::vector<UnitSpec> & specs, int rounds, bool fallback, bool empty,
		std::set<uint32_t> actors, std::vector<SelectionObservation> & observations)
	{
		Scenario scenario(specs);
		scenario.populateCache();
		auto active = scenario.state->getForUpdate(1);
		PotentialTargets targets(active.get(), scenario.cache, scenario.state);
		const auto selected = std::find_if(targets.possibleAttacks.begin(), targets.possibleAttacks.end(),
			[&](const AttackPossibility & attack)
			{
				return attack.attack.defender->unitId() == 3
					&& (fallback ? attack.attack.shooting : attack.from == active->getPosition());
			});
		ASSERT_NE(selected, targets.possibleAttacks.end()) << "PROBE_SETUP: initiating attack";
		auto evaluator = scenario.evaluator(rounds);
		evaluator.updateReachabilityMap(scenario.state);
		const auto exchange = evaluator.getExchangeUnits(*selected, 0, targets, scenario.state);
		ASSERT_FALSE(exchange.units.empty());
		ASSERT_FALSE(exchange.units.at(0).empty());
		ASSERT_EQ(exchange.units.at(0).front()->unitId(), 1u);
		for(uint32_t actor : actors)
			ASSERT_TRUE(std::any_of(exchange.units.at(0).begin(), exchange.units.at(0).end(),
				[actor](const battle::Unit * unit) { return unit->unitId() == actor; }));

		std::vector<uint32_t> candidates;
		if(fallback)
		{
			// All own stacks are remote shooters, so the ordinary queue is filtered
			// out. All units are materialized, and these own IDs are in the
			// same order as HypotheticBattle's ordered unit-state map.
			for(const auto & source : scenario.sources)
				if(source->state.unitSide() == BattleSide::ATTACKER)
				{
					const uint32_t id = source->state.unitId();
					ASSERT_TRUE(std::any_of(exchange.shooters.begin(), exchange.shooters.end(),
						[id](const battle::Unit * unit) { return unit->unitId() == id; }));
					if(!empty)
						candidates.push_back(id);
				}
		}
		else
		{
			// Derive the comparison order from the real turn queue, not unit IDs.
			for(const auto & round : exchange.units)
				for(const auto * unit : round.second)
					if(unit->unitSide() == BattleSide::ATTACKER
						&& std::find(candidates.begin(), candidates.end(), unit->unitId()) == candidates.end())
						candidates.push_back(unit->unitId());
		}
		SelectionRecorder recorder(scenario, candidates, std::move(actors));
		const auto before = recorder.snapshot(*scenario.state);
		{
			BattleAITestTrace::ScopedSink trace(recorder);
			EXPECT_TRUE(std::isfinite(evaluator.evaluateExchange(*selected, 0, targets, scenario.cache, scenario.state)));
		}
		EXPECT_EQ(recorder.snapshot(*scenario.state), before);
		observations = std::move(recorder.selections);
	}

	void checkCountAndParity(const SelectionObservation & observation)
	{
		const size_t count = observation.candidates.size();
		EXPECT_EQ(observation.legacyCalls, count < 2 ? 0 : 2 * count - 2);
		if(count < 2)
			EXPECT_TRUE(observation.estimates.empty());
		else
			EXPECT_EQ(observation.estimates, observation.candidates) << "EXACTLY_ONCE_IN_QUEUE_ORDER";
		EXPECT_EQ(observation.choice, observation.expectedChoice);
	}
};

TEST_F(BattleSingleEvalSelectionTest, QueueScoresThreeAndFourCandidatesOnceInsteadOfTwicePerComparison)
{
	for(size_t count : {3u, 4u})
	{
		SCOPED_TRACE(count);
		std::vector<SelectionObservation> observations;
		ASSERT_NO_FATAL_FAILURE(run(queueSpecs(count), 1, false, false, {4}, observations));
		ASSERT_EQ(observations.size(), 1u);
		ASSERT_EQ(observations.front().candidates.size(), count);
		checkCountAndParity(observations.front());
		EXPECT_EQ(observations.front().estimates.size(), count);
		EXPECT_LT(observations.front().estimates.size(), observations.front().legacyCalls);
		EXPECT_TRUE(observations.front().executionChecked);
	}
}

TEST_F(BattleSingleEvalSelectionTest, QueueKeepsFirstCandidateOnAnExactScoreTie)
{
	std::vector<SelectionObservation> observations;
	ASSERT_NO_FATAL_FAILURE(run(queueSpecs(3, true), 1, false, false, {4}, observations));
	ASSERT_EQ(observations.size(), 1u);
	const auto & selection = observations.front();
	ASSERT_EQ(selection.scores.size(), 3u);
	for(float score : selection.scores)
		EXPECT_FLOAT_EQ(score, selection.scores.front()) << "PROBE_SETUP: genuine production-score tie";
	checkCountAndParity(selection);
	ASSERT_TRUE(selection.choice);
	EXPECT_EQ(*selection.choice, selection.candidates.front());
	EXPECT_TRUE(selection.executionChecked);
}

TEST_F(BattleSingleEvalSelectionTest, ReachableFallbackScoresEachCandidateOnceAndKeepsFirstTie)
{
	std::vector<SelectionObservation> observations;
	ASSERT_NO_FATAL_FAILURE(run(fallbackSpecs(3), 1, true, false, {3}, observations));
	ASSERT_EQ(observations.size(), 1u);
	const auto & selection = observations.front();
	ASSERT_EQ(selection.candidates.size(), 3u);
	ASSERT_EQ(selection.scores.size(), 3u);
	for(float score : selection.scores)
		EXPECT_FLOAT_EQ(score, selection.scores.front()) << "PROBE_SETUP: genuine fallback tie";
	checkCountAndParity(selection);
	ASSERT_TRUE(selection.choice);
	EXPECT_EQ(*selection.choice, selection.candidates.front());
	EXPECT_TRUE(selection.executionChecked);
}

TEST_F(BattleSingleEvalSelectionTest, SingletonQueueAndFallbackChooseWithoutAnyEstimate)
{
	for(bool fallback : {false, true})
	{
		SCOPED_TRACE(fallback);
		std::vector<SelectionObservation> observations;
		ASSERT_NO_FATAL_FAILURE(run(fallback ? fallbackSpecs(1) : queueSpecs(1), 1,
			fallback, false, {fallback ? 3u : 4u}, observations));
		ASSERT_EQ(observations.size(), 1u);
		ASSERT_EQ(observations.front().candidates.size(), 1u);
		checkCountAndParity(observations.front());
		ASSERT_TRUE(observations.front().choice);
		EXPECT_EQ(*observations.front().choice, 1u);
	}
}

TEST_F(BattleSingleEvalSelectionTest, EmptyFallbackNeitherEstimatesNorChooses)
{
	std::vector<SelectionObservation> observations;
	ASSERT_NO_FATAL_FAILURE(run(fallbackSpecs(1, 1), 1, true, true, {3}, observations));
	ASSERT_EQ(observations.size(), 1u) << "PROBE_SETUP: real selector must be entered";
	checkCountAndParity(observations.front());
	EXPECT_TRUE(observations.front().candidates.empty());
	EXPECT_FALSE(observations.front().choice);
}

TEST_F(BattleSingleEvalSelectionTest, EveryActionAndRoundReestimatesTheChangedExchangeState)
{
	std::vector<SelectionObservation> observations;
	ASSERT_NO_FATAL_FAILURE(run(queueSpecs(3), 3, false, false, {3, 4}, observations));
	ASSERT_EQ(observations.size(), 6u) << "PROBE_SETUP: two enemy actors in each of three rounds";
	for(const auto & selection : observations)
	{
		ASSERT_EQ(selection.candidates.size(), 3u);
		checkCountAndParity(selection);
		EXPECT_TRUE(selection.executionChecked);
	}
	ASSERT_EQ(observations[0].actor, 4u);
	ASSERT_EQ(observations[1].actor, 3u);
	EXPECT_NE(observations[0].before, observations[1].before) << "PROBE_SETUP: intervening action changed state";
	std::vector<const SelectionObservation *> sameActor;
	for(const auto & selection : observations)
		if(selection.actor == 3)
			sameActor.push_back(&selection);
	ASSERT_EQ(sameActor.size(), 3u);
	for(size_t round = 1; round < sameActor.size(); ++round)
	{
		EXPECT_EQ(sameActor[round]->candidates, sameActor[round - 1]->candidates);
		EXPECT_NE(sameActor[round]->before, sameActor[round - 1]->before);
		EXPECT_NE(sameActor[round]->scores, sameActor[round - 1]->scores)
			<< "PROBE_SETUP: stale pair estimates would differ from fresh first strikes";
	}
}

}
}
}
#endif
