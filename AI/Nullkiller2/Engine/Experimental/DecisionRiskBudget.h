/* GPL-2.0-or-later; see license.txt. Experimental, not production-enabled. */
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace NK2AI::experimental
{
// One capability is created before candidate discovery for one decision. Copies
// share its deadline, cancellation and draw counter; no candidate or retry may
// create a replacement allowance. This bounds cooperative work, not OS scheduling
// or the mandatory reacquisition of the real game-state lock.
class DecisionRiskBudget
{
public:
	using Clock = std::chrono::steady_clock;
	using Now = std::function<Clock::time_point()>;
	using Stop = std::function<bool()>;
	static constexpr unsigned HARD_MAX_DRAWS = 7 * 2 * 128;
	static constexpr unsigned HARD_MAX_WORKERS = 2;
	static constexpr std::chrono::milliseconds HARD_MAX_WALL_TIME{2000};
	struct Settings
	{
		std::chrono::milliseconds wallTime{1500};
		unsigned maximumDraws = HARD_MAX_DRAWS;
		unsigned requestedWorkers = 1;
		unsigned availableThreads = 1;
	};

private:
	struct State
	{
		const Now now;
		const Stop stop;
		const Clock::time_point started;
		const Clock::time_point deadline;
		const unsigned maximumDraws;
		const unsigned workerLimit;
		std::atomic<unsigned> drawsStarted{0};
		std::atomic<bool> cancelled{false};
		State(const Settings & settings, Stop stop, Now now)
			: now(std::move(now)), stop(std::move(stop)), started(this->now()),
			deadline(started + std::clamp(settings.wallTime,
				std::chrono::milliseconds::zero(), HARD_MAX_WALL_TIME)),
			maximumDraws(std::min(settings.maximumDraws, HARD_MAX_DRAWS)),
			workerLimit(std::min({std::max(1u, settings.requestedWorkers), HARD_MAX_WORKERS,
				settings.availableThreads > 2 ? settings.availableThreads - 2 : 1u}))
		{}
	};
	std::shared_ptr<State> state;

public:
	// Stop and Now must be safe for concurrent calls and must not read live game
	// objects, callbacks, path storage or AI state. Production Now is monotonic;
	// injecting it is only a deterministic regression seam. Exceptions fail closed.
	explicit DecisionRiskBudget(const Settings & settings, Stop stop = {},
		Now now = [] { return Clock::now(); })
		: state(std::make_shared<State>(settings, std::move(stop), std::move(now)))
	{}
	bool shouldStop() const
	{
		if(state->cancelled.load(std::memory_order_acquire))
			return true;
		try
		{
			if((state->stop && state->stop()) || state->now() >= state->deadline)
			{
				cancel();
				return true;
			}
		}
		catch(...)
		{
			cancel();
			return true;
		}
		return false;
	}
	void cancel() const { state->cancelled.store(true, std::memory_order_release); }
	unsigned maximumDraws() const { return state->maximumDraws; }
	unsigned workerLimit() const { return state->workerLimit; }
	unsigned drawsStarted() const { return state->drawsStarted.load(std::memory_order_acquire); }
	unsigned remainingDraws() const { return maximumDraws() - drawsStarted(); }
	Clock::time_point startedAt() const { return state->started; }
	Clock::time_point deadline() const { return state->deadline; }
	// Claim once immediately before starting a draw. Failed/aborted draws stay
	// charged; spent work is never refunded after stale publication or exceptions.
	bool tryStartDraw() const
	{
		if(shouldStop())
			return false;
		auto used = state->drawsStarted.load(std::memory_order_acquire);
		while(used < maximumDraws())
		{
			if(state->drawsStarted.compare_exchange_weak(used, used + 1,
				std::memory_order_acq_rel, std::memory_order_acquire))
				return !shouldStop();
		}
		return false;
	}
};
}
