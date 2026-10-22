/*
 * BattleCallerBenchmark.cpp, bounded opt-in VCMI experiment.
 * GPL-2.0-or-later; see license.txt.
 */
#include "StdInc.h"
#include "BattleCallerFixture.h"
#include "AI/BattleAI/BattleAITestTrace.h"
#include "lib/logging/CLogger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace test
{
namespace callerprobe
{
// vcmitest initializes GoogleTest, not GoogleMock: do not rely on CLI verbosity.
// VCMI 1.7.5 has no OFF level. Suppress routine AI output, retain error diagnostics.
struct ScopedBenchmarkLogging
{
	std::string previousMockVerbosity = ::testing::GMOCK_FLAG(verbose);
	CLogger * ai = dynamic_cast<CLogger *>(logAi);
	ELogLevel::ELogLevel previousAiLevel = ELogLevel::NOT_SET;

	ScopedBenchmarkLogging()
	{
		::testing::GMOCK_FLAG(verbose) = "error";
		if(ai)
		{
			previousAiLevel = ai->getLevel();
			ai->setLevel(ELogLevel::ERROR);
		}
	}
	~ScopedBenchmarkLogging()
	{
		if(ai)
			ai->setLevel(previousAiLevel);
		::testing::GMOCK_FLAG(verbose) = previousMockVerbosity;
	}
	ScopedBenchmarkLogging(const ScopedBenchmarkLogging &) = delete;
	ScopedBenchmarkLogging & operator=(const ScopedBenchmarkLogging &) = delete;
};

using Clock = std::chrono::steady_clock;
inline double milliseconds(Clock::duration elapsed)
{
	return std::chrono::duration<double, std::milli>(elapsed).count();
}
inline double quantile(std::vector<double> values, double fraction)
{
	std::sort(values.begin(), values.end());
	return values.at(static_cast<size_t>(std::ceil(fraction * values.size())) - 1);
}

#ifdef VCMI_BATTLE_AI_TEST_TRACE
struct CountingSink : BattleAITestTrace::Sink
{
	size_t estimates = 0;
	size_t selections = 0;
	void receive(const BattleAITestTrace::Event & event) override
	{
		if(event.kind == BattleAITestTrace::Kind::Estimate)
			++estimates;
		if(event.kind == BattleAITestTrace::Kind::Selection)
			++selections;
	}
};
#endif

// Not included in the normal test run. Census is separate from latency measurements.
TEST(BattleCallerBenchmark, DISABLED_TraceCensus)
{
#ifndef VCMI_BATTLE_AI_TEST_TRACE
	GTEST_SKIP() << "Build this census only with VCMI_BATTLE_AI_TEST_TRACE enabled.";
#else
	for(const int stacks : {6, 14, 28})
		for(const bool mixed : {false, true})
			for(const int rounds : {1, 2})
				for(const bool finder : {false, true})
				{
					Scenario scene(benchmarkSpecs(stacks, mixed));
					scene.populateCache();
					auto active = scene.state->getForUpdate(1);
					scene.state->makeWait(active.get());
					PotentialTargets targets(active.get(), scene.cache, scene.state);
					ASSERT_FALSE(targets.possibleAttacks.empty()) << "PROBE_SETUP";
					auto evaluator = scene.evaluator(rounds);
					if(!finder)
						evaluator.updateReachabilityMap(scene.state);
					CountingSink counter;
					{
						BattleAITestTrace::ScopedSink trace(counter);
						if(finder)
							evaluator.findBestTarget(active.get(), targets, scene.cache, scene.state);
						else
							evaluator.evaluateExchange(targets.possibleAttacks.front(), 0, targets, scene.cache, scene.state);
					}
					std::cout << "CENSUS\t" << stacks << '\t' << mixed << '\t' << rounds << '\t'
						<< (finder ? "finder" : "exchange") << '\t' << targets.possibleAttacks.size()
						<< '\t' << scene.populatedPairs << '\t' << counter.selections
						<< '\t' << counter.estimates << "\tobstacle_entries=0\n";
				}
#endif
}

// Run in a separate Release executable built WITHOUT the trace definition.
TEST(BattleCallerBenchmark, DISABLED_BoundedLatency)
{
#ifdef VCMI_BATTLE_AI_TEST_TRACE
	GTEST_SKIP() << "Trace affects timing: use a separate no-trace Release build.";
#else
	ScopedBenchmarkLogging logging;
	ASSERT_NE(logging.ai, nullptr) << "PROBE_SETUP: expected production CLogger for logAi";
	ASSERT_FALSE(logAi->isTraceEnabled());
	ASSERT_FALSE(logAi->isDebugEnabled());
	ASSERT_EQ(::testing::GMOCK_FLAG(verbose), "error");
	std::cout << "BENCH_CONFIG\tai_level=error\tgmock_verbose=error\ttrace=off\n";
	const auto wholeStart = Clock::now();
	constexpr int warmups = 5;
	constexpr int samples = 30;
	constexpr double cellCapMs = 15000;
	constexpr double totalCapMs = 600000;
	for(const int stacks : {6, 14, 28})
		for(const bool mixed : {false, true})
			for(const int rounds : {1, 2})
				for(const bool finder : {false, true})
				{
					const auto cellStart = Clock::now();
					std::vector<double> elapsed, cacheElapsed, targetElapsed, reachElapsed;
					size_t candidateCount = 0, populatedPairs = 0;
					float signatureScore = 0;
					uint32_t signatureTarget = 0;
					bool signatureWait = false;
					bool capped = false;
					for(int sample = -warmups; sample < samples; ++sample)
					{
						if(milliseconds(Clock::now() - wholeStart) > totalCapMs)
						{
							std::cout << "TOTAL_CAP\t600000\n";
							return; // explicitly inconclusive, never called a passing performance gate
						}
						if(milliseconds(Clock::now() - cellStart) > cellCapMs)
						{
							capped = true;
							break;
						}
						Scenario scene(benchmarkSpecs(stacks, mixed));
						auto begin = Clock::now();
						scene.populateCache();
						const auto cacheMs = milliseconds(Clock::now() - begin);
						auto active = scene.state->getForUpdate(1);
						scene.state->makeWait(active.get());
						begin = Clock::now();
						PotentialTargets targets(active.get(), scene.cache, scene.state);
						const auto targetMs = milliseconds(Clock::now() - begin);
						ASSERT_FALSE(targets.possibleAttacks.empty()) << "PROBE_SETUP";
						candidateCount = targets.possibleAttacks.size();
						populatedPairs = scene.populatedPairs;
						auto evaluator = scene.evaluator(rounds);
						begin = Clock::now();
						if(!finder)
							evaluator.updateReachabilityMap(scene.state);
						const auto reachMs = milliseconds(Clock::now() - begin);
						const auto before = scene.snapshot();
						const auto activeBefore = scene.state->getActiveStackID();
						float score;
						uint32_t target;
						bool wait;
						begin = Clock::now();
						if(finder)
						{
							const auto result = evaluator.findBestTarget(active.get(), targets, scene.cache, scene.state);
							score = result.score;
							target = result.bestAttack.attack.defender->unitId();
							wait = result.wait;
						}
						else
						{
							score = evaluator.evaluateExchange(targets.possibleAttacks.front(), 0, targets, scene.cache, scene.state);
							target = targets.possibleAttacks.front().attack.defender->unitId();
							wait = false;
						}
						const auto durationMs = milliseconds(Clock::now() - begin);
						ASSERT_TRUE(std::isfinite(score));
						ASSERT_EQ(scene.snapshot(), before);
						ASSERT_EQ(scene.state->getActiveStackID(), activeBefore);
						if(sample == -warmups)
						{
							signatureScore = score;
							signatureTarget = target;
							signatureWait = wait;
						}
						else
						{
							ASSERT_FLOAT_EQ(score, signatureScore);
							ASSERT_EQ(target, signatureTarget);
							ASSERT_EQ(wait, signatureWait);
						}
						if(sample >= 0)
						{
							elapsed.push_back(durationMs);
							cacheElapsed.push_back(cacheMs);
							targetElapsed.push_back(targetMs);
							reachElapsed.push_back(reachMs);
							std::cout << "SAMPLE\t" << stacks << '\t' << mixed << '\t' << rounds << '\t'
								<< (finder ? "finder" : "exchange") << '\t' << sample << '\t'
								<< durationMs << '\t' << cacheMs << '\t' << targetMs << '\t' << reachMs << '\n';
						}
						if(milliseconds(Clock::now() - cellStart) > cellCapMs)
						{
							capped = true;
							break;
						}
					}
					std::cout << "LATENCY\t" << stacks << '\t' << mixed << '\t' << rounds << '\t'
						<< (finder ? "finder" : "exchange") << '\t' << elapsed.size()
						<< '\t' << (capped ? "CAPPED" : "COMPLETE")
						<< '\t' << candidateCount << '\t' << populatedPairs;
					if(!elapsed.empty())
						std::cout << '\t' << quantile(elapsed, 0.5) << '\t' << quantile(elapsed, 0.95)
							<< '\t' << quantile(cacheElapsed, 0.5) << '\t' << quantile(targetElapsed, 0.5)
							<< '\t' << quantile(reachElapsed, 0.5);
					std::cout << "\tsignature=" << signatureTarget << ',' << signatureWait << ',' << signatureScore
						<< "\tallocations=unmeasured\tobstacle_entries=0\n";
				}
#endif
}
}
}

