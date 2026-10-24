/* GPL-2.0-or-later; see license.txt. Scheduler protocol tests, NOT combat calibration. */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CTypeList.h"
#include <future>
#include <stdexcept>

VCMI_LIB_USING_NAMESPACE

namespace NK2AI
{
class PlanningRiskSchedulerRegressionAccess
{
public:
	using Capture = AIGateway::RiskCapture;
	using Build = AIGateway::RiskBuild;
	static bool enabled() { return AIGateway::ENABLE_STAGED_RISK_PREPARATION; }
	static void locked(AIGateway & gateway,
		const std::function<void(std::shared_lock<std::shared_mutex> &)> & body)
	{
		gateway.withPlanningTurnLocks(body, generation(gateway));
	}
	static void lockedWithContentionObserver(AIGateway & gateway,
		const std::function<void(std::shared_lock<std::shared_mutex> &)> & body,
		const std::function<void(bool)> & observer)
	{
		gateway.withPlanningTurnLocks(body, generation(gateway), observer);
	}
	static void scoped(AIGateway & gateway, std::shared_lock<std::shared_mutex> & lock,
		const std::function<void()> & body)
	{
		gateway.withPlanningRiskPreparation(lock, body);
	}
	static void batch(AIGateway & gateway, std::shared_lock<std::shared_mutex> & lock,
		uint64_t turn, std::vector<experimental::PlanningRiskCandidate> candidates,
		const Capture & capture, const std::function<bool()> & eligible,
		const Build & build = experimental::PlanningRiskTable::build)
	{
		gateway.preparePlanningRiskBatch(lock, turn, std::move(candidates), capture, eligible, build);
	}
	static auto publication(const Nullkiller & ai)
	{
		return ai.preparedPlanningRisk.load(std::memory_order_acquire);
	}
	static auto memo(const AIGateway & gateway) { return gateway.planningRiskMemo; }
	static void boundedBatch(AIGateway & gateway, std::shared_lock<std::shared_mutex> & lock,
		uint64_t turn, std::vector<experimental::PlanningRiskCandidate> candidates,
		const Capture & capture, const std::function<bool()> & eligible,
		const experimental::DecisionRiskBudget & budget,
		const Build & build = experimental::PlanningRiskTable::build)
	{
		gateway.preparePlanningRiskBatch(lock, turn, std::move(candidates), capture, eligible, build, &budget);
	}
	static auto decision(const Nullkiller & ai)
	{
		if(!bound(ai)) throw std::logic_error("Decision lookup requires its owning scope");
		return Nullkiller::planningTurnLocks->riskDecision;
	}
	static void finishDecision(Nullkiller & ai) { ai.finishPlanningRiskDecision(); }
	static void advanceRiskEpochOnly(Nullkiller & ai)
	{
		ai.planningRiskEpoch.fetch_add(1, std::memory_order_acq_rel);
	}
	static auto discover(AIGateway & gateway, std::shared_lock<std::shared_mutex> & lock,
		const experimental::DecisionRiskBudget & budget)
	{
		return gateway.discoverPlanningRiskCandidates(lock, budget);
	}
	static uint64_t generation(const AIGateway & gateway)
	{
		return gateway.planningTurnGeneration.load(std::memory_order_acquire);
	}
	static void advanceGeneration(AIGateway & gateway)
	{
		gateway.planningTurnGeneration.fetch_add(1, std::memory_order_acq_rel);
	}
	static void lockedGeneration(AIGateway & gateway, uint64_t generation,
		const std::function<void(std::shared_lock<std::shared_mutex> &)> & body)
	{
		gateway.withPlanningTurnLocks(body, generation);
	}
	static bool bound(const Nullkiller & ai)
	{
		return Nullkiller::planningTurnLocks && Nullkiller::planningTurnLocks->planner == &ai;
	}
	static bool hooked(const Nullkiller & ai)
	{
		return bound(ai) && bool(Nullkiller::planningTurnLocks->prepareRisk);
	}
	static void prepareStandalone(Nullkiller & ai) { ai.preparePlanningRiskForStateUpdate(); }
	static void updateWithObserver(Nullkiller & ai, std::function<void()> observer)
	{
		if(!bound(ai)) throw std::logic_error("Test observer requires a bound planner");
		auto * capability = Nullkiller::planningTurnLocks;
		capability->prepareRisk = std::move(observer);
		auto restore = vstd::makeScopeGuard([capability] { capability->prepareRisk = {}; });
		ai.updateState();
	}
	static void setPreparation(Nullkiller & ai, std::function<void()> preparation)
	{
		if(!bound(ai)) throw std::logic_error("Test preparation requires a bound planner");
		Nullkiller::planningTurnLocks->prepareRisk = std::move(preparation);
	}
	static void installReadiness(AIGateway & gateway, std::shared_lock<std::shared_mutex> & lock,
		std::function<void()> beforeCheck = {})
	{
		const auto generation = Nullkiller::planningTurnLocks->generation;
		Nullkiller::planningTurnLocks->ensureReady = [&gateway, &lock, generation, beforeCheck]
		{
			if(beforeCheck) beforeCheck();
			gateway.ensurePlanningTurnReady(lock, generation);
		};
	}
	static void bestEffort(AIGateway & gateway, const std::function<void()> & prepare)
	{
		gateway.runPlanningRiskBestEffort(prepare);
	}
};
}

namespace
{
using Access = NK2AI::PlanningRiskSchedulerRegressionAccess;
using namespace NK2AI::experimental;
using NK2AI::AIGateway;
using NK2AI::Nullkiller;
const auto eligible = [] { return true; };

std::vector<PlanningRiskCandidate> onePair()
{
	return {{ObjectInstanceID(1), ObjectInstanceID(100), 1}};
}

std::optional<CapturedRiskRequest> captureUnknown(const PlanningRiskCandidate & candidate,
	uint64_t epoch, const std::string & source = "unchanged-visible-source")
{
	CapturedRiskRequest result;
	result.key = {epoch, candidate.hero, candidate.target, source};
	return result; // Deliberately no battle variants: exercises no combat physics.
}

auto defaultCapture()
{
	return [](const PlanningRiskCandidate & pair, uint64_t epoch) { return captureUnknown(pair, epoch); };
}

struct StopBeforeStateAnalyzers : std::exception {};
}

TEST(PlanningRiskScheduler, RemainsInactiveAndStandaloneCallsDoNotInventLockOwnership)
{
	EXPECT_FALSE(Access::enabled());
	Nullkiller ai;
	EXPECT_FALSE(Access::bound(ai));
	EXPECT_FALSE(Access::hooked(ai));
	// An unbound preparation step is a no-op even if no game lock exists.
	EXPECT_NO_THROW(Access::prepareStandalone(ai));
	EXPECT_FALSE(Access::publication(ai));
	EXPECT_FALSE(ai.hasPlanningRisk());
	// Real makeTurn cannot acquire global storage under an unknown caller lock.
	EXPECT_THROW(ai.makeTurn(), std::logic_error);
}

TEST(PlanningRiskScheduler, ActualUpdateStatePreparesBeforeTakingAiStateMutex)
{
	AIGateway gateway;
	auto & ai = *gateway.nullkiller;
	bool observed = false;
	Access::locked(gateway, [&](auto &)
	{
		EXPECT_THROW(Access::updateWithObserver(ai, [&]
		{
			observed = true;
			const auto free = std::async(std::launch::async, [&]
			{
				std::unique_lock lock(ai.aiStateMutex, std::try_to_lock);
				return lock.owns_lock();
			}).get();
			EXPECT_TRUE(free);
			throw StopBeforeStateAnalyzers();
		}), StopBeforeStateAnalyzers);
	});
	EXPECT_TRUE(observed);
	EXPECT_FALSE(Access::hooked(ai));
}

TEST(PlanningRiskScheduler, CapabilityCannotBeInventedFromAnotherOrUnownedLock)
{
	AIGateway gateway;
	std::shared_mutex unrelated;
	std::shared_lock wrong(unrelated);
	EXPECT_THROW(Access::scoped(gateway, wrong, [] {}), std::logic_error);
	std::shared_lock absent(CGameState::mutex, std::defer_lock);
	EXPECT_THROW(Access::scoped(gateway, absent, [] {}), std::logic_error);
	EXPECT_FALSE(Access::bound(*gateway.nullkiller));
	EXPECT_FALSE(Access::hooked(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, NestedScopesRejectButConcurrentSameGatewayWorkersQueue)
{
	AIGateway gateway;
	std::future<void> queued;
	std::promise<bool> contended;
	auto contention = contended.get_future();
	bool ran = false;
	Access::locked(gateway, [&](auto &)
	{
		EXPECT_THROW(Access::locked(gateway, [](auto &) {}), std::logic_error);
		queued = std::async(std::launch::async, [&]
		{
			Access::lockedWithContentionObserver(gateway, [&](auto &) { ran = true; },
				[&](bool ownsGameLock) { contended.set_value(ownsGameLock); });
		});
		const auto observed = contention.wait_for(std::chrono::seconds(2));
		EXPECT_EQ(observed, std::future_status::ready);
		if(observed == std::future_status::ready)
		{
			EXPECT_FALSE(contention.get());
		}
	});
	queued.get();
	EXPECT_TRUE(ran);
}

TEST(PlanningRiskScheduler, ScopeIsClearedOnFailureAndForeignThreadHasNoCapability)
{
	AIGateway gateway;
	EXPECT_THROW(Access::locked(gateway, [&](auto & gameLock)
	{
		Access::scoped(gateway, gameLock, [&]
		{
			EXPECT_TRUE(Access::bound(*gateway.nullkiller));
			const auto unbound = std::async(std::launch::async, [&]
			{
				EXPECT_FALSE(Access::bound(*gateway.nullkiller));
				EXPECT_FALSE(Access::hooked(*gateway.nullkiller));
				EXPECT_NO_THROW(Access::prepareStandalone(*gateway.nullkiller));
				try { gateway.nullkiller->makeTurn(); }
				catch(const std::logic_error &) { return true; }
				return false;
			}).get();
			EXPECT_TRUE(unbound);
			throw StopBeforeStateAnalyzers();
		});
	}), StopBeforeStateAnalyzers);
	EXPECT_FALSE(Access::bound(*gateway.nullkiller));
	EXPECT_FALSE(Access::hooked(*gateway.nullkiller));
	std::unique_lock gameLock(CGameState::mutex, std::try_to_lock);
	std::unique_lock storageLock(NK2AI::AISharedStorage::locker, std::try_to_lock);
	EXPECT_TRUE(gameLock.owns_lock());
	EXPECT_TRUE(storageLock.owns_lock());
}

TEST(PlanningRiskScheduler, CaptureAndRecaptureAreBoundedAndBuildHasNoGameOrAiStateLock)
{
	AIGateway gateway;
	std::vector<PlanningRiskCandidate> candidates;
	for(int hero = 0; hero < 10; ++hero)
		for(int target = 0; target < 3; ++target)
			candidates.push_back({ObjectInstanceID(hero), ObjectInstanceID(100 + target), uint32_t(target)});
	std::vector<ObjectInstanceID> captured;
	Access::locked(gateway, [&](auto & gameLock)
	{
		Access::batch(gateway, gameLock, 1, candidates,
			[&](const auto & pair, uint64_t epoch)
			{
				EXPECT_TRUE(gameLock.owns_lock());
				captured.push_back(pair.hero);
				return captureUnknown(pair, epoch);
			}, eligible,
			[&](auto requests, uint64_t turn, unsigned budget, auto memo)
			{
				EXPECT_FALSE(gameLock.owns_lock());
				EXPECT_EQ(requests.size(), PlanningRiskTable::MAX_REQUESTS);
				const auto free = std::async(std::launch::async, [&]
				{
					std::unique_lock writer(CGameState::mutex, std::try_to_lock);
					std::unique_lock aiState(gateway.nullkiller->aiStateMutex, std::try_to_lock);
					return writer.owns_lock() && aiState.owns_lock();
				}).get();
				EXPECT_TRUE(free);
				return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
			});
		EXPECT_TRUE(gameLock.owns_lock());
		const auto publication = Access::publication(*gateway.nullkiller);
		ASSERT_TRUE(publication);
		EXPECT_EQ(publication->keys.size(), PlanningRiskTable::MAX_REQUESTS);
	});
	ASSERT_EQ(captured.size(), 2 * PlanningRiskTable::MAX_REQUESTS);
	for(size_t index = 0; index < PlanningRiskTable::MAX_REQUESTS; ++index)
	{
		EXPECT_EQ(captured[index], ObjectInstanceID(index + 1));
		EXPECT_EQ(captured[index], captured[index + PlanningRiskTable::MAX_REQUESTS]);
	}
	gateway.nullkiller->invalidatePathfinderData();
	EXPECT_FALSE(Access::publication(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, ChangedSourceVisibilityEligibilityOrEpochRejectsEntirePublication)
{
	for(int change = 0; change < 4; ++change)
	{
		AIGateway gateway;
		std::string source = "original";
		bool visible = true;
		bool currentTurn = true;
		Access::locked(gateway, [&](auto & gameLock)
		{
			Access::batch(gateway, gameLock, 1, onePair(),
				[&](const auto & pair, uint64_t epoch) -> std::optional<CapturedRiskRequest>
				{
					if(!visible) return std::nullopt;
					return captureUnknown(pair, epoch, source);
				}, [&] { return currentTurn; },
				[&](auto requests, uint64_t turn, unsigned budget, auto memo)
				{
					std::unique_lock writer(CGameState::mutex);
					if(change == 0) source = "changed mana, army, bonuses or terrain";
					if(change == 1) visible = false;
					if(change == 2) currentTurn = false;
					if(change == 3) gateway.nullkiller->invalidatePathfinderData();
					auto accounted = std::make_shared<PlanningRiskMemo>();
					accounted->turn = turn;
					accounted->consumedScenarios = 6; // Synthetic accounting, not a combat result.
					return PlanningRiskTable::build(std::move(requests), turn, budget, accounted);
				});
			EXPECT_TRUE(gameLock.owns_lock());
			EXPECT_FALSE(Access::publication(*gateway.nullkiller));
			ASSERT_TRUE(Access::memo(gateway));
			EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 6u);
			visible = true;
			currentTurn = true;
			Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
				[&](auto requests, uint64_t turn, unsigned budget, auto memo)
				{
					EXPECT_EQ(memo->consumedScenarios, 6u);
					return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
				});
		});
	}
}

TEST(PlanningRiskScheduler, BuilderExceptionRelocksAndConsumesUnknownSpentBudget)
{
	AIGateway gateway;
	Access::locked(gateway, [&](auto & gameLock)
	{
		EXPECT_THROW(Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
			[](auto, uint64_t, unsigned, auto) -> std::shared_ptr<const PlanningRiskTable>
			{
				throw StopBeforeStateAnalyzers();
			}), StopBeforeStateAnalyzers);
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
		ASSERT_TRUE(Access::memo(gateway));
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, PlanningRiskTable::MAX_SCENARIOS_PER_TURN);
		Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
			[](auto requests, uint64_t turn, unsigned budget, auto memo)
			{
				EXPECT_EQ(memo->consumedScenarios, PlanningRiskTable::MAX_SCENARIOS_PER_TURN);
				return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
			});
		Access::batch(gateway, gameLock, 2, onePair(), defaultCapture(), eligible);
		EXPECT_EQ(Access::memo(gateway)->turn, 2u);
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 0u);
	});
}

TEST(PlanningRiskScheduler, TwoAiContendersDoNotRetainReadersAheadOfServerWriter)
{
	AIGateway first;
	AIGateway second;
	std::future<void> secondTurn;
	std::promise<bool> secondContended;
	auto secondContention = secondContended.get_future();
	bool writerSucceeded = false;
	Access::locked(first, [&](auto & gameLock)
	{
		Access::batch(first, gameLock, 1, onePair(), defaultCapture(), eligible,
			[&](auto requests, uint64_t turn, unsigned budget, auto memo)
			{
				secondTurn = std::async(std::launch::async, [&]
				{
					Access::lockedWithContentionObserver(second,
						[](auto & secondGameLock) { EXPECT_TRUE(secondGameLock.owns_lock()); },
						[&](bool ownsGameLock) { secondContended.set_value(ownsGameLock); });
				});
				// Wait for the real helper's failed storage try_lock, not for a
				// pre-entry thread signal or arbitrary scheduling delay. An inverted
				// helper would report an owned game reader and block the writer.
				const auto observed = secondContention.wait_for(std::chrono::seconds(2));
				EXPECT_EQ(observed, std::future_status::ready);
				if(observed == std::future_status::ready)
				{
					EXPECT_FALSE(secondContention.get());
				}
				writerSucceeded = std::async(std::launch::async, [&]
				{
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					do
					{
						std::unique_lock writer(CGameState::mutex, std::try_to_lock);
						if(writer.owns_lock())
						{
							first.nullkiller->invalidatePathfinderData();
							return true;
						}
						std::this_thread::yield();
					} while(std::chrono::steady_clock::now() < deadline);
					return false;
				}).get();
				return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
			});
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_FALSE(Access::publication(*first.nullkiller));
	});
	ASSERT_TRUE(secondTurn.valid());
	secondTurn.get();
	EXPECT_TRUE(writerSucceeded);
	EXPECT_FALSE(Access::bound(*first.nullkiller));
	EXPECT_FALSE(Access::bound(*second.nullkiller));
}

TEST(PlanningRiskScheduler, ForeignThreadCannotObserveCapabilityDuringRepeatedTeardown)
{
	AIGateway gateway;
	std::atomic<bool> stop{false};
	std::atomic<bool> sawForeignCapability{false};
	auto observer = std::async(std::launch::async, [&]
	{
		while(!stop.load(std::memory_order_acquire))
		{
			if(Access::bound(*gateway.nullkiller) || Access::hooked(*gateway.nullkiller))
				sawForeignCapability.store(true, std::memory_order_release);
			Access::prepareStandalone(*gateway.nullkiller);
		}
	});
	auto stopObserver = vstd::makeScopeGuard([&] { stop.store(true, std::memory_order_release); });
	for(unsigned iteration = 0; iteration < 100; ++iteration)
		Access::locked(gateway, [&](auto & lock)
		{
			Access::scoped(gateway, lock, [&] { EXPECT_TRUE(Access::hooked(*gateway.nullkiller)); });
		});
	stop.store(true, std::memory_order_release);
	observer.get();
	EXPECT_FALSE(sawForeignCapability.load(std::memory_order_acquire));
}

namespace
{
class TurnCallback final : public CCallback
{
public:
	using CCallback::getResourceAmount;
	EPlayerStatus playerStatus = EPlayerStatus::INGAME;
	unsigned endRequests = 0;
	std::function<void()> afterEndRequest;
	explicit TurnCallback(std::shared_ptr<CGameState> state)
		: CCallback(std::move(state), PlayerColor(0), nullptr) {}
	EPlayerStatus getPlayerStatus(PlayerColor, bool = true) const override { return playerStatus; }
	TResources getResourceAmount() const override { return {}; }
	void endTurn() override
	{
		++endRequests;
		if(afterEndRequest) afterEndRequest();
	}
};

class PlanningRiskTurnLifecycle : public ::testing::Test
{
protected:
	std::shared_ptr<CGameState> state = std::make_shared<CGameState>();
	AIGateway gateway;
	std::shared_ptr<TurnCallback> callback = std::make_shared<TurnCallback>(state);
	void SetUp() override
	{
		gateway.playerID = PlayerColor(0);
		gateway.cc = callback;
		gateway.status.startedTurn();
		state->actingPlayers.insert(gateway.playerID);
		callback->afterEndRequest = [&] { gateway.playerEndsTurn(gateway.playerID); };
	}
};
}

TEST_F(PlanningRiskTurnLifecycle, ChangedGenerationAfterBuildStopsBeforeAnalyzers)
{
	Access::locked(gateway, [&](auto & gameLock)
	{
		Access::installReadiness(gateway, gameLock);
		EXPECT_THROW(Access::updateWithObserver(*gateway.nullkiller, [&]
		{
			Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
				[&](auto requests, uint64_t turn, unsigned budget, auto memo)
				{
					Access::advanceGeneration(gateway);
					return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
				});
		}), InterruptionRequestedException);
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
		EXPECT_THROW(gateway.endTurn(), InterruptionRequestedException);
		EXPECT_EQ(callback->endRequests, 0u);
	});
}

TEST_F(PlanningRiskTurnLifecycle, RealServerTurnLossAndEliminationOverrideStaleLocalFlag)
{
	for(int loss = 0; loss < 2; ++loss)
	{
		state->actingPlayers.insert(gateway.playerID);
		callback->playerStatus = EPlayerStatus::INGAME;
		Access::locked(gateway, [&](auto & gameLock)
		{
			Access::installReadiness(gateway, gameLock);
			EXPECT_THROW(Access::updateWithObserver(*gateway.nullkiller, [&]
			{
				Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
					[&](auto requests, uint64_t turn, unsigned budget, auto memo)
					{
						std::unique_lock writer(CGameState::mutex);
						if(loss == 0) state->actingPlayers.erase(gateway.playerID);
						else callback->playerStatus = EPlayerStatus::LOSER;
						return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
					});
			}), InterruptionRequestedException);
			EXPECT_TRUE(gameLock.owns_lock());
			EXPECT_TRUE(gateway.status.haveTurn()); // Deliberately stale local flag.
			EXPECT_FALSE(Access::publication(*gateway.nullkiller));
			EXPECT_THROW(gateway.endTurn(), InterruptionRequestedException);
		});
	}
	EXPECT_EQ(callback->endRequests, 0u);
}

TEST_F(PlanningRiskTurnLifecycle, PendingQueryAfterBuildReleasesGameLockBeforeContinuing)
{
	std::promise<void> readinessReached;
	auto readinessFuture = readinessReached.get_future();
	std::future<bool> resolver;
	bool announced = false;
	Access::locked(gateway, [&](auto & gameLock)
	{
		Access::installReadiness(gateway, gameLock, [&]
		{
			if(!announced && gateway.status.getQueriesCount() > 0)
			{
				announced = true;
				readinessReached.set_value();
			}
		});
		Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
			[&](auto requests, uint64_t turn, unsigned budget, auto memo)
			{
				gateway.status.addQuery(QueryID(77), "scheduler fixture");
				gateway.status.attemptedAnsweringQuery(QueryID(77), 777);
				resolver = std::async(std::launch::async, [&]
				{
					if(readinessFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
					{
						gateway.status.receivedAnswerConfirmation(777, 1);
						return false;
					}
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					do
					{
						std::unique_lock writer(CGameState::mutex, std::try_to_lock);
						if(writer.owns_lock())
						{
							gateway.status.receivedAnswerConfirmation(777, 1);
							return true;
						}
						std::this_thread::yield();
					} while(std::chrono::steady_clock::now() < deadline);
					gateway.status.receivedAnswerConfirmation(777, 1); // Unblock a failing test.
					return false;
				});
				return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
			});
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_TRUE(gateway.status.isIdle());
	});
	ASSERT_TRUE(resolver.valid());
	EXPECT_TRUE(resolver.get());
	EXPECT_TRUE(announced);
}

TEST_F(PlanningRiskTurnLifecycle, NewGenerationCancelsPendingQueryWaitAndRelocks)
{
	std::promise<void> readinessReached;
	auto readinessFuture = readinessReached.get_future();
	std::future<bool> nextTurn;
	bool announced = false;
	Access::locked(gateway, [&](auto & gameLock)
	{
		Access::installReadiness(gateway, gameLock, [&]
		{
			if(!announced && gateway.status.getQueriesCount() > 0)
			{
				announced = true;
				readinessReached.set_value();
			}
		});
		EXPECT_THROW(Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
			[&](auto requests, uint64_t turn, unsigned budget, auto memo)
			{
				gateway.status.addQuery(QueryID(78), "scheduler cancellation fixture");
				gateway.status.attemptedAnsweringQuery(QueryID(78), 778);
				nextTurn = std::async(std::launch::async, [&]
				{
					if(readinessFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
					{
						Access::advanceGeneration(gateway);
						return false;
					}
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
					do
					{
						std::unique_lock writer(CGameState::mutex, std::try_to_lock);
						if(writer.owns_lock())
						{
							Access::advanceGeneration(gateway);
							return true;
						}
						std::this_thread::yield();
					} while(std::chrono::steady_clock::now() < deadline);
					Access::advanceGeneration(gateway); // Unblock a failing wait.
					return false;
				});
				return PlanningRiskTable::build(std::move(requests), turn, budget, memo);
			}), InterruptionRequestedException);
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
		gateway.status.receivedAnswerConfirmation(778, 1);
	});
	EXPECT_TRUE(nextTurn.get());
}

TEST_F(PlanningRiskTurnLifecycle, OlderQueuedWorkerRejectsWhileNewGenerationStillRuns)
{
	const auto oldGeneration = Access::generation(gateway);
	std::future<bool> oldWorker;
	std::future<bool> newWorker;
	Access::locked(gateway, [&](auto &)
	{
		oldWorker = std::async(std::launch::async, [&]
		{
			try { Access::lockedGeneration(gateway, oldGeneration, [](auto &) {}); }
			catch(const InterruptionRequestedException &) { return true; }
			return false;
		});
		Access::advanceGeneration(gateway);
		newWorker = std::async(std::launch::async, [&]
		{
			bool ran = false;
			Access::locked(gateway, [&](auto &) { ran = true; });
			return ran;
		});
	});
	EXPECT_TRUE(oldWorker.get());
	EXPECT_TRUE(newWorker.get());
}

TEST_F(PlanningRiskTurnLifecycle, LateEndTurnAcknowledgmentCannotClearNewerTurn)
{
	gateway.playerEndsTurn(gateway.playerID);
	EXPECT_FALSE(gateway.status.haveTurn());
	Access::advanceGeneration(gateway);
	gateway.status.startedTurn();
	PackageApplied acknowledgment;
	acknowledgment.packType = CTypeList::getInstance().getTypeID<EndTurn>(nullptr);
	acknowledgment.result = true;
	gateway.requestRealized(&acknowledgment);
	EXPECT_TRUE(gateway.status.haveTurn());
}

TEST_F(PlanningRiskTurnLifecycle, OrdinaryForecastFailureFallsBackAndStillAllowsEndTurn)
{
	Access::locked(gateway, [&](auto & gameLock)
	{
		Access::installReadiness(gateway, gameLock);
		Access::setPreparation(*gateway.nullkiller, [&]
		{
			Access::bestEffort(gateway, [&]
			{
				Access::batch(gateway, gameLock, 1, onePair(), defaultCapture(), eligible,
					[](auto, uint64_t, unsigned, auto) -> std::shared_ptr<const PlanningRiskTable>
					{
						throw std::runtime_error("unsupported forecast fixture");
					});
			});
		});
		EXPECT_NO_THROW(Access::prepareStandalone(*gateway.nullkiller));
		EXPECT_TRUE(gameLock.owns_lock());
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, PlanningRiskTable::MAX_SCENARIOS_PER_TURN);
		EXPECT_NO_THROW(gateway.endTurn());
	});
	EXPECT_EQ(callback->endRequests, 1u);
}

TEST_F(PlanningRiskTurnLifecycle, ForecastFallbackDoesNotSwallowCancellationOrTermination)
{
	EXPECT_THROW(Access::bestEffort(gateway, [] { throw InterruptionRequestedException(); }), InterruptionRequestedException);
	EXPECT_THROW(Access::bestEffort(gateway, [] { throw TerminationRequestedException(); }), TerminationRequestedException);
}

namespace
{
struct SchedulerDecisionClock
{
	std::atomic<int64_t> milliseconds{0};
	DecisionRiskBudget makeBudget(unsigned draws = 128)
	{
		DecisionRiskBudget::Settings settings;
		settings.wallTime = std::chrono::milliseconds(100);
		settings.maximumDraws = draws;
		return DecisionRiskBudget(settings, {}, [this]
		{
			return DecisionRiskBudget::Clock::time_point(std::chrono::milliseconds(milliseconds.load()));
		});
	}
};
}

TEST(PlanningRiskScheduler, ExpiredDecisionDoesNotCaptureOrBuild)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	clock.milliseconds = 100;
	unsigned captures = 0, builds = 0;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::boundedBatch(gateway, lock, 1, onePair(),
			[&](const auto & pair, uint64_t epoch) { ++captures; return captureUnknown(pair, epoch); }, eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto memo)
			{
				++builds;
				return PlanningRiskTable::build(std::move(requests), turn, draws, memo);
			});
		EXPECT_TRUE(lock.owns_lock());
	});
	EXPECT_EQ(captures, 0u);
	EXPECT_EQ(builds, 0u);
	EXPECT_FALSE(Access::publication(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, CaptureTimeConsumesTheSameDecisionDeadline)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	unsigned captures = 0, builds = 0;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::boundedBatch(gateway, lock, 1, onePair(),
			[&](const auto & pair, uint64_t epoch)
			{
				++captures;
				clock.milliseconds = 100;
				return captureUnknown(pair, epoch);
			}, eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto memo)
			{
				++builds;
				return PlanningRiskTable::build(std::move(requests), turn, draws, memo);
			});
		EXPECT_TRUE(lock.owns_lock());
	});
	EXPECT_EQ(captures, 1u);
	EXPECT_EQ(builds, 0u);
	EXPECT_FALSE(Access::publication(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, ExpiredDetachedWorkRelocksAndRetainsSpentMemoWithoutRecapture)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	unsigned captures = 0;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::boundedBatch(gateway, lock, 1, onePair(),
			[&](const auto & pair, uint64_t epoch) { ++captures; return captureUnknown(pair, epoch); }, eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto)
			{
				EXPECT_FALSE(lock.owns_lock());
				EXPECT_TRUE(budget.tryStartDraw());
				clock.milliseconds = 100;
				EXPECT_TRUE(budget.shouldStop());
				auto spent = std::make_shared<PlanningRiskMemo>();
				spent->turn = turn;
				spent->consumedScenarios = budget.drawsStarted();
				return PlanningRiskTable::build(std::move(requests), turn, draws, spent);
			});
		EXPECT_TRUE(lock.owns_lock());
		ASSERT_TRUE(Access::memo(gateway));
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 1u);
	});
	EXPECT_EQ(captures, 1u);
	EXPECT_FALSE(Access::publication(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, RecaptureTimeCannotPublishAnExpiredDecision)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	unsigned captures = 0;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::boundedBatch(gateway, lock, 1, onePair(),
			[&](const auto & pair, uint64_t epoch)
			{
				if(++captures == 2) clock.milliseconds = 100;
				return captureUnknown(pair, epoch);
			}, eligible, budget);
		EXPECT_TRUE(lock.owns_lock());
	});
	EXPECT_EQ(captures, 2u);
	EXPECT_FALSE(Access::publication(*gateway.nullkiller));
}

TEST(PlanningRiskScheduler, AdaptiveExceptionChargesExactClaimsAndDoesNotRenewAllowance)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget(3);
	Access::locked(gateway, [&](auto & lock)
	{
		EXPECT_THROW(Access::boundedBatch(gateway, lock, 1, onePair(), defaultCapture(), eligible, budget,
			[&](auto, uint64_t, unsigned, auto) -> std::shared_ptr<const PlanningRiskTable>
			{
				EXPECT_TRUE(budget.tryStartDraw());
				EXPECT_TRUE(budget.tryStartDraw());
				throw StopBeforeStateAnalyzers();
			}), StopBeforeStateAnalyzers);
		EXPECT_TRUE(lock.owns_lock());
		ASSERT_TRUE(Access::memo(gateway));
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 2u);
		EXPECT_EQ(budget.remainingDraws(), 1u);
		Access::boundedBatch(gateway, lock, 1, onePair(), defaultCapture(), eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto memo)
			{
				EXPECT_EQ(memo->consumedScenarios, 2u);
				EXPECT_TRUE(budget.tryStartDraw());
				EXPECT_FALSE(budget.tryStartDraw());
				auto spent = std::make_shared<PlanningRiskMemo>(*memo);
				spent->consumedScenarios = budget.drawsStarted();
				return PlanningRiskTable::build(std::move(requests), turn, draws, spent);
			});
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 3u);
	});
}

TEST(PlanningRiskScheduler, ReplanningSharesDecisionUntilAnActionAttemptClosesIt)
{
	AIGateway gateway;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::scoped(gateway, lock, [&]
		{
			Access::prepareStandalone(*gateway.nullkiller); // No callback: capture fails closed.
			auto first = Access::decision(*gateway.nullkiller);
			ASSERT_TRUE(first);
			EXPECT_TRUE(first->tryStartDraw());
			Access::prepareStandalone(*gateway.nullkiller);
			auto repeated = Access::decision(*gateway.nullkiller);
			ASSERT_TRUE(repeated);
			EXPECT_EQ(repeated->startedAt(), first->startedAt());
			EXPECT_EQ(repeated->drawsStarted(), 1u);
			Access::finishDecision(*gateway.nullkiller);
			EXPECT_TRUE(first->shouldStop());
			EXPECT_FALSE(Access::decision(*gateway.nullkiller));
			Access::prepareStandalone(*gateway.nullkiller);
			auto next = Access::decision(*gateway.nullkiller);
			ASSERT_TRUE(next);
			EXPECT_EQ(next->drawsStarted(), 0u);
		});
	});
}

TEST_F(PlanningRiskTurnLifecycle, CooperativeGenerationCancellationRelocksBeforeRejectingOldWorker)
{
	const auto generation = Access::generation(gateway);
	DecisionRiskBudget budget({}, [&] { return Access::generation(gateway) != generation; });
	Access::locked(gateway, [&](auto & lock)
	{
		Access::installReadiness(gateway, lock);
		EXPECT_THROW(Access::boundedBatch(gateway, lock, 1, onePair(), defaultCapture(), eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto memo)
			{
				EXPECT_FALSE(lock.owns_lock());
				EXPECT_TRUE(budget.tryStartDraw());
				Access::advanceGeneration(gateway);
				EXPECT_TRUE(budget.shouldStop());
				return PlanningRiskTable::build(std::move(requests), turn, draws, memo);
			}), InterruptionRequestedException);
		EXPECT_TRUE(lock.owns_lock());
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
		EXPECT_THROW(gateway.endTurn(), InterruptionRequestedException);
	});
	EXPECT_EQ(callback->endRequests, 0u);
}

TEST(PlanningRiskScheduler, AliasingLookupRejectsChangedEpochAndRetainsSafeRecordLifetime)
{
	AIGateway gateway;
	std::shared_ptr<const PlanningRiskRecord> held;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::batch(gateway, lock, 1, onePair(), defaultCapture(), eligible);
		held = gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 0});
		ASSERT_TRUE(held);
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {false, 0, 0}));
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 1, 0}));
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 1}));
		gateway.garrisonsChanged(ObjectInstanceID(1), ObjectInstanceID(100));
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 0}));
	});
	ASSERT_TRUE(held);
	EXPECT_FALSE(held->allScenariosFinished); // Memory remains valid, not fresh authority.
}

TEST(PlanningRiskScheduler, ArtifactNotificationInvalidatesBeforeAConsumerLookup)
{
	AIGateway gateway;
	Access::locked(gateway, [&](auto & lock)
	{
		Access::batch(gateway, lock, 1, onePair(), defaultCapture(), eligible);
		ASSERT_TRUE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 0}));
		gateway.artifactMoved(ArtifactLocation(), ArtifactLocation());
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 0}));
	});
}

TEST(PlanningRiskScheduler, MalformedAdaptiveAccountingCannotRefundSpentAttempts)
{
	AIGateway gateway;
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	Access::locked(gateway, [&](auto & lock)
	{
		Access::boundedBatch(gateway, lock, 1, onePair(), defaultCapture(), eligible, budget,
			[&](auto requests, uint64_t turn, unsigned draws, auto memo)
			{
				EXPECT_TRUE(budget.tryStartDraw());
				// Intentionally broken adapter: omits the charged attempt from memo.
				return PlanningRiskTable::build(std::move(requests), turn, draws, memo);
			});
		EXPECT_TRUE(lock.owns_lock());
		ASSERT_TRUE(Access::memo(gateway));
		EXPECT_EQ(Access::memo(gateway)->consumedScenarios, 1u);
		EXPECT_FALSE(Access::publication(*gateway.nullkiller));
	});
}

TEST(PlanningRiskScheduler, PresenceFastPathRejectsAnOtherwiseRetainedStalePublication)
{
	AIGateway gateway;
	EXPECT_FALSE(gateway.nullkiller->hasPlanningRisk()); // No callback is installed.
	Access::locked(gateway, [&](auto & lock)
	{
		Access::batch(gateway, lock, 1, onePair(), defaultCapture(), eligible);
		ASSERT_TRUE(gateway.nullkiller->hasPlanningRisk());
		Access::advanceRiskEpochOnly(*gateway.nullkiller);
		ASSERT_TRUE(Access::publication(*gateway.nullkiller)); // Deliberately retain it.
		EXPECT_FALSE(gateway.nullkiller->hasPlanningRisk());
		EXPECT_FALSE(gateway.nullkiller->findPlanningRisk(ObjectInstanceID(1), ObjectInstanceID(100), {true, 0, 0}));
	});
}

namespace
{
// Install the actual canonical map through the public serialization boundary.
// Visibility is controlled by the game-state override, not a replacement for
// CGameInfoCallback::getAllVisitableObjs or the production discovery method.
struct InstallRiskDiscoveryMap
{
	enum class Version { NO_RAW_POINTERS_IN_SERIALIZER, RANDOMIZATION_REWORK, SERVER_STATISTICS };
	bool saving = true;
	bool loadingGamestate = false;
	std::unique_ptr<CMap> map;
	bool hasFeature(Version) const { return true; }
	template<typename T> void operator&(T &) {}
	void operator&(std::unique_ptr<CMap> & target) { target = std::move(map); }
};
class RiskDiscoveryState final : public CGameState
{
public:
	using CGameState::isVisibleFor;
	std::set<ObjectInstanceID> hidden;
	bool isVisibleFor(const CGObjectInstance * object, PlayerColor) const override
	{
		return hidden.count(object->id) == 0;
	}
};
class RiskDiscoveryCallback final : public CCallback
{
	std::shared_ptr<RiskDiscoveryState> state;
public:
	const CGHeroInstance * hero = nullptr;
	explicit RiskDiscoveryCallback(std::shared_ptr<RiskDiscoveryState> state)
		: CCallback(state, PlayerColor(0), nullptr), state(std::move(state)) {}
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return {hero}; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		if(hero && hero->id == id) return hero;
		for(const auto & object : state->getMap().objects)
			if(object->id == id && !state->hidden.count(id)) return object.get();
		return nullptr;
	}
};
class DerivedRiskDiscoveryCreature final : public CGCreature
{
public:
	using CGCreature::CGCreature;
};
}

TEST(PlanningRiskScheduler, ActualDiscoveryAcceptsBothNativeNeutralOwnersAndRejectsOtherContexts)
{
	auto state = std::make_shared<RiskDiscoveryState>();
	auto callback = std::make_shared<RiskDiscoveryCallback>(state);
	InstallRiskDiscoveryMap install;
	install.map = std::make_unique<CMap>(callback.get());
	state->serialize(install);
	AIGateway gateway;
	gateway.cc = callback;
	gateway.playerID = PlayerColor(0);
	auto appearance = std::make_shared<ObjectTemplate>();
	JsonNode geometry;
	geometry["mask"].Vector().push_back(JsonNode("A"));
	appearance->readJson(geometry, false);
	CGHeroInstance hero(callback.get());
	hero.id = ObjectInstanceID(1);
	hero.tempOwner = gateway.playerID;
	hero.appearance = appearance;
	hero.setAnchorPos(int3(1, 0, 0));
	hero.setHeroType(HeroTypeID(0));
	hero.setMovementPoints(100);
	callback->hero = &hero;
	auto target = std::make_shared<CGCreature>(callback.get());
	target->id = ObjectInstanceID(0);
	target->ID = Obj::MONSTER;
	target->subID = MapObjectSubID(0);
	target->appearance = appearance;
	target->setAnchorPos(int3(0, 0, 0));
	state->getMap().objects = {target};
	SchedulerDecisionClock clock;
	auto budget = clock.makeBudget();
	Access::locked(gateway, [&](auto & lock)
	{
		for(const auto owner : {PlayerColor::NEUTRAL, PlayerColor::UNFLAGGABLE})
		{
			target->tempOwner = owner;
			const auto candidates = Access::discover(gateway, lock, budget);
			ASSERT_EQ(candidates.size(), 1u);
			EXPECT_EQ(candidates.front().hero, hero.id);
			EXPECT_EQ(candidates.front().target, target->id);
		}
		target->tempOwner = PlayerColor(1);
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		target->tempOwner = PlayerColor::UNFLAGGABLE;
		state->hidden.insert(target->id);
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		state->hidden.clear();
		target->ID = Obj::EVENT;
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		target->ID = Obj::MONSTER;
		hero.appearance.reset();
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		hero.appearance = appearance;
		auto derived = std::make_shared<DerivedRiskDiscoveryCreature>(callback.get());
		derived->id = target->id;
		derived->ID = Obj::MONSTER;
		derived->tempOwner = PlayerColor::UNFLAGGABLE;
		derived->appearance = appearance;
		derived->setAnchorPos(int3(0, 0, 0));
		state->getMap().objects = {derived};
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		state->getMap().objects = {target};
		clock.milliseconds = 100;
		EXPECT_TRUE(Access::discover(gateway, lock, budget).empty());
		EXPECT_TRUE(lock.owns_lock());
	});
}
