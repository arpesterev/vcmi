/* GPL-2.0-or-later; see license.txt. Cooperative control tests, not battle calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/Engine/Experimental/DecisionRiskBudget.h"
#include <future>
#include <stdexcept>

using NK2AI::experimental::DecisionRiskBudget;
namespace
{
struct DecisionClock
{
	std::atomic<int64_t> milliseconds{0};
	DecisionRiskBudget::Clock::time_point now() const
	{
		return DecisionRiskBudget::Clock::time_point(std::chrono::milliseconds(milliseconds.load()));
	}
};
}

TEST(DecisionRiskBudget, ClampsConfigurationAndReservesReportedCpuHeadroom)
{
	DecisionClock clock;
	DecisionRiskBudget::Settings settings;
	settings.wallTime = std::chrono::seconds(20);
	settings.maximumDraws = 100000;
	settings.requestedWorkers = 100;
	settings.availableThreads = 12;
	DecisionRiskBudget budget(settings, {}, [&] { return clock.now(); });
	EXPECT_EQ(budget.deadline() - budget.startedAt(), std::chrono::milliseconds(2000));
	EXPECT_EQ(budget.maximumDraws(), 1792u);
	EXPECT_EQ(budget.workerLimit(), 2u);
	for(unsigned available = 0; available <= 3; ++available)
	{
		settings.availableThreads = available;
		DecisionRiskBudget limited(settings, {}, [&] { return clock.now(); });
		EXPECT_EQ(limited.workerLimit(), 1u);
	}
}

TEST(DecisionRiskBudget, CopiesShareAnAbsoluteDeadlineAndDrawClaims)
{
	DecisionClock clock;
	DecisionRiskBudget::Settings settings;
	settings.wallTime = std::chrono::milliseconds(100);
	settings.maximumDraws = 3;
	DecisionRiskBudget first(settings, {}, [&] { return clock.now(); });
	clock.milliseconds = 90;
	auto second = first;
	EXPECT_EQ(first.deadline(), second.deadline());
	EXPECT_TRUE(first.tryStartDraw());
	EXPECT_TRUE(second.tryStartDraw());
	EXPECT_TRUE(first.tryStartDraw());
	EXPECT_FALSE(second.tryStartDraw());
	EXPECT_EQ(first.remainingDraws(), 0u);
	// Exhausted admission must not cancel the final in-flight draw.
	EXPECT_FALSE(first.shouldStop());
	clock.milliseconds = 100;
	EXPECT_TRUE(second.shouldStop());
	clock.milliseconds = 0; // A regression-only clock rollback cannot revive it.
	EXPECT_TRUE(first.shouldStop());
}

TEST(DecisionRiskBudget, ExplicitCancellationAndGenerationProbesAreShared)
{
	DecisionClock clock;
	std::atomic<uint64_t> generation{7};
	DecisionRiskBudget budget({}, [&] { return generation.load() != 7; }, [&] { return clock.now(); });
	auto worker = budget;
	EXPECT_FALSE(worker.shouldStop());
	generation = 8;
	EXPECT_TRUE(worker.shouldStop());
	EXPECT_FALSE(budget.tryStartDraw());
	DecisionRiskBudget another({}, {}, [&] { return clock.now(); });
	auto copied = another;
	copied.cancel();
	EXPECT_TRUE(another.shouldStop());
}

TEST(DecisionRiskBudget, ConcurrentClaimsNeverExceedCollectiveDecisionAllowance)
{
	DecisionClock clock;
	DecisionRiskBudget::Settings settings;
	settings.maximumDraws = 73;
	DecisionRiskBudget budget(settings, {}, [&] { return clock.now(); });
	std::vector<std::future<unsigned>> workers;
	for(unsigned index = 0; index < 8; ++index)
		workers.push_back(std::async(std::launch::async, [budget]
		{
			unsigned started = 0;
			while(budget.tryStartDraw()) ++started;
			return started;
		}));
	unsigned started = 0;
	for(auto & worker : workers) started += worker.get();
	EXPECT_EQ(started, 73u);
	EXPECT_EQ(budget.drawsStarted(), started);
	EXPECT_EQ(budget.remainingDraws(), 0u);
}

TEST(DecisionRiskBudget, ZeroTimeAndThrowingControlProbesFailClosed)
{
	DecisionClock clock;
	DecisionRiskBudget::Settings settings;
	settings.wallTime = std::chrono::milliseconds::zero();
	DecisionRiskBudget expired(settings, {}, [&] { return clock.now(); });
	EXPECT_FALSE(expired.tryStartDraw());
	DecisionRiskBudget failed({}, []() -> bool { throw std::runtime_error("probe failure"); }, [&] { return clock.now(); });
	EXPECT_TRUE(failed.shouldStop());
	EXPECT_FALSE(failed.tryStartDraw());
}
