/*
 * PlannerContinuityRegression.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../StdInc.h"
#include "../../AI/Nullkiller2/AIGateway.h"
#include "../../AI/Nullkiller2/Goals/ExecuteHeroChain.h"
#include "../../AI/Nullkiller2/Goals/Composition.h"
#include "../../AI/Nullkiller2/Goals/ExchangeSwapTownHeroes.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include <future>

VCMI_LIB_USING_NAMESPACE

namespace NK2AI
{
// Access real planner entry points without exporting testing APIs in the AI DLL.
class PlannerRegressionAccess
{
public:
	static void initialize(Nullkiller & ai)
	{
		ai.useObjectGraph = false;
		ai.openMap = false;
		ai.pathfinderCache = std::make_unique<PathfinderCache>(ai.cc.get(), PathfinderOptions(*ai.cc));
		ai.dangerHitMap = std::make_unique<DangerHitMapAnalyzer>(&ai);
		ai.objectClusterizer = std::make_unique<ObjectClusterizer>(&ai);
		ai.settings = std::make_unique<Settings>(0);
		ai.beginPlanningTurn();
	}
	static void initializeFullState(Nullkiller & ai)
	{
		ai.useHeroChain = false;
		ai.buildAnalyzer = std::make_unique<BuildAnalyzer>(&ai);
		ai.heroManager = std::make_unique<HeroManager>(ai.cc.get(), &ai);
		ai.armyManager = std::make_unique<ArmyManager>(ai.cc.get(), &ai);
		ai.pathfinder = std::make_unique<AIPathfinder>(&ai);
		ai.decomposer = std::make_unique<DeepDecomposer>(&ai);
	}
	static void update(Nullkiller & ai) { ai.updateState(); }
	static void nextTurn(Nullkiller & ai) { ai.beginPlanningTurn(); }
	static Goals::TTaskVec select(Nullkiller & ai, const Goals::TGoalVec & tasks) { return ai.selectEvaluatedTasks(tasks); }
	static TaskExecutionResult execute(Nullkiller & ai, const Goals::TTask & task) { return ai.executeTask(task); }
	static bool expand(Nullkiller & ai) { return ai.expandSearch(); }
	static bool dirty(const Nullkiller & ai) { return ai.pathfinderInvalidated; }
	static void clean(Nullkiller & ai) { ai.pathfinderInvalidated = false; }
	static PathfinderSettings pathSettings(const Nullkiller & ai) { return ai.getPathfinderSettings(); }
};
}

namespace test
{
namespace
{
using NK2AI::PlannerRegressionAccess;
using NK2AI::TaskExecutionResult;
using namespace NK2AI::Goals;

class PlannerCallback final : public CCallback
{
public:
	GameSettings settings;
	mutable int pathRecalculations = 0;
	std::map<ObjectInstanceID, const CGObjectInstance *> objects;
	PlannerCallback() : CCallback(nullptr, std::nullopt, nullptr) {}
	const IGameSettings & getSettings() const override { return settings; }
	const CGObjectInstance * getObj(ObjectInstanceID id, bool = true) const override
	{
		auto found = objects.find(id);
		return found == objects.end() ? nullptr : found->second;
	}
	int3 getMapSize() const override { return int3(16, 16, 1); }
	std::vector<const CGHeroInstance *> getHeroesInfo() const override { return {}; }
	std::vector<const CGTownInstance *> getTownsInfo(bool = true) const override { return {}; }
	std::vector<const CGObjectInstance *> getMyObjects() const override { return {}; }
	TResources getResourceAmount() const override { return {}; }
	int getDate(Date) const override { return 1; }
	const TerrainTile * getTile(int3, bool = true) const override { return nullptr; }
	void calculatePaths(const std::shared_ptr<PathfinderConfig> &) const override { ++pathRecalculations; }
};

class ScriptedGoal final : public ElementarGoal<ScriptedGoal>
{
	int * executions;
	bool fail;
public:
	ScriptedGoal(int target, int & executions, bool fail)
		: ElementarGoal(CAPTURE_OBJECT), executions(&executions), fail(fail)
	{
		objid = target;
		priority = 1;
	}
	bool operator==(const ScriptedGoal & other) const override { return objid == other.objid; }
	std::string toString() const override { return "scripted goal " + std::to_string(objid); }
	void accept(NK2AI::AIGateway *) override
	{
		++*executions;
		if(fail)
			throw NK2AI::cannotFulfillGoalException("local prerequisite unavailable");
	}
};

NK2AI::AIPath pathTo(CGHeroInstance & hero, int3 destination, CGHeroInstance * courier = nullptr)
{
	NK2AI::AIPath path;
	path.targetHero = &hero;
	path.heroArmy = &hero;
	path.targetObjectDanger = 0;
	path.armyLoss = 0;
	path.targetObjectArmyLoss = 0;
	path.chainMask = 1;
	path.exchangeCount = courier ? 2 : 1;
	NK2AI::AIPathNodeInfo node{};
	node.targetHero = &hero;
	node.coord = destination;
	node.cost = 2;
	node.turns = 2;
	node.parentIndex = -1;
	node.chainMask = 1;
	path.nodes.push_back(node);
	if(courier)
	{
		node.targetHero = courier;
		path.nodes.push_back(node);
	}
	return path;
}

class ScriptedChain final : public ExecuteHeroChain
{
	CGHeroInstance * movingHero;
public:
	ScriptedChain(CGHeroInstance & hero, int3 destination, float score, CGHeroInstance * courier = nullptr)
		: ExecuteHeroChain(pathTo(hero, destination, courier)), movingHero(&hero)
	{
		priority = score;
	}
	ScriptedChain * clone() const override { return new ScriptedChain(*this); }
	std::string toString() const override { return "scripted journey " + tile.toString(); }
	void accept(NK2AI::AIGateway *) override { movingHero->pos.x += 1; }
};

class CourierProgressChain final : public ExecuteHeroChain
{
	CGHeroInstance * courier;
	static NK2AI::AIPath makePath(CGHeroInstance & recipient, CGHeroInstance & courier, int3 destination, int3 rendezvous)
	{
		auto path = pathTo(recipient, destination, &courier);
		path.nodes.back().coord = rendezvous;
		return path;
	}
public:
	CourierProgressChain(CGHeroInstance & recipient, CGHeroInstance & courier, int3 destination, int3 rendezvous)
		: ExecuteHeroChain(makePath(recipient, courier, destination, rendezvous)), courier(&courier)
	{
		priority = 1;
	}
	CourierProgressChain * clone() const override { return new CourierProgressChain(*this); }
	std::string toString() const override { return "courier-only progress"; }
	void accept(NK2AI::AIGateway *) override { courier->pos.x += 1; }
};

TSubgoal journey(CGHeroInstance & hero, int3 destination, float score, CGHeroInstance * courier = nullptr)
{
	return sptr(ScriptedChain(hero, destination, score, courier));
}
}

class Nullkiller2PlannerContinuityRegression : public ::testing::Test
{
protected:
	std::shared_ptr<PlannerCallback> callback = std::make_shared<PlannerCallback>();
	NK2AI::AIGateway gateway;
	CGHeroInstance first{callback.get()};
	CGHeroInstance second{callback.get()};
	NK2AI::Nullkiller & ai = *gateway.nullkiller;
	const int3 east{12, 0, 0};
	const int3 west{0, 12, 0};

	void SetUp() override
	{
		gateway.cc = callback;
		ai.cc = callback;
		ai.aiGw = &gateway;
		first.setHeroType(HeroTypeID(0));
		second.setHeroType(HeroTypeID(0));
		first.id = ObjectInstanceID(1);
		second.id = ObjectInstanceID(2);
		first.pos = second.pos = int3(0, 0, 0);
		first.appearance = second.appearance = std::make_shared<ObjectTemplate>();
		callback->objects[first.id] = &first;
		callback->objects[second.id] = &second;
		PlannerRegressionAccess::initialize(ai);
	}
	void progressEast(CGHeroInstance * courier = nullptr)
	{
		auto tasks = PlannerRegressionAccess::select(ai, {journey(first, east, 1, courier)});
		ASSERT_EQ(tasks.size(), 1u);
		ASSERT_EQ(PlannerRegressionAccess::execute(ai, tasks.front()), TaskExecutionResult::SUCCESS);
	}
	int3 selectedDestination(const TTaskVec & tasks)
	{
		EXPECT_EQ(tasks.size(), 1u);
		return dynamic_cast<const ExecuteHeroChain &>(*tasks.front()).tile;
	}
};

TEST_F(Nullkiller2PlannerContinuityRegression, WiderSearchInvalidatesCleanCacheAndRemovesScoutLimit)
{
	PlannerRegressionAccess::clean(ai);
	EXPECT_LT(PlannerRegressionAccess::pathSettings(ai).scoutTurnDistanceLimit, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
	ASSERT_TRUE(PlannerRegressionAccess::expand(ai));
	EXPECT_TRUE(PlannerRegressionAccess::dirty(ai));
	EXPECT_EQ(PlannerRegressionAccess::pathSettings(ai).scoutTurnDistanceLimit, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
	EXPECT_EQ(PlannerRegressionAccess::pathSettings(ai).mainTurnDistanceLimit, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
	EXPECT_FALSE(PlannerRegressionAccess::pathSettings(ai).useHeroChain);
	PlannerRegressionAccess::clean(ai);
	EXPECT_FALSE(PlannerRegressionAccess::expand(ai));
	EXPECT_FALSE(PlannerRegressionAccess::dirty(ai));
}

TEST_F(Nullkiller2PlannerContinuityRegression, LocalFailureAllowsIndependentTaskAfterFreshSelection)
{
	int failedExecutions = 0, successfulExecutions = 0;
	auto failed = sptr(ScriptedGoal(10, failedExecutions, true).setpriority(2));
	auto good = sptr(ScriptedGoal(11, successfulExecutions, false));
	ai.lockHero(&first, NK2AI::HeroLockedReason::HERO_CHAIN);
	ai.lockHero(&second, NK2AI::HeroLockedReason::DEFENCE);
	auto firstPass = PlannerRegressionAccess::select(ai, {failed, good});
	ASSERT_EQ(firstPass.size(), 2u);
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, firstPass.front()), TaskExecutionResult::REPLAN);
	EXPECT_TRUE(PlannerRegressionAccess::dirty(ai));
	EXPECT_FALSE(ai.isHeroLocked(&first));
	EXPECT_EQ(ai.getHeroLockedReason(&second), NK2AI::HeroLockedReason::DEFENCE);
	// Real task reconstruction, not pointer identity, must suppress the failure.
	auto freshFailed = sptr(ScriptedGoal(10, failedExecutions, true).setpriority(3));
	auto secondPass = PlannerRegressionAccess::select(ai, {freshFailed, good});
	ASSERT_EQ(secondPass.size(), 1u);
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, secondPass.front()), TaskExecutionResult::SUCCESS);
	EXPECT_EQ(failedExecutions, 1);
	EXPECT_EQ(successfulExecutions, 1);
	PlannerRegressionAccess::nextTurn(ai);
	EXPECT_EQ(PlannerRegressionAccess::select(ai, {freshFailed, good}).size(), 2u);
}

TEST_F(Nullkiller2PlannerContinuityRegression, RecoveryHasStrictPerTurnBudget)
{
	int executions = 0;
	for(int i = 0; i < 4; ++i)
	{
		auto task = taskptr(ScriptedGoal(10 + i, executions, true));
		EXPECT_EQ(PlannerRegressionAccess::execute(ai, task), i < 3 ? TaskExecutionResult::REPLAN : TaskExecutionResult::STOP);
	}
	EXPECT_EQ(executions, 4);
}

TEST_F(Nullkiller2PlannerContinuityRegression, PendingMovementDoesNotPermitAlternativeCommands)
{
	int executions = 0;
	gateway.status.setMove(true);
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, taskptr(ScriptedGoal(10, executions, true))), TaskExecutionResult::STOP);
	EXPECT_EQ(executions, 1);
	gateway.status.setMove(false);
}

TEST_F(Nullkiller2PlannerContinuityRegression, PendingQueryDoesNotPermitAlternativeCommands)
{
	int executions = 0;
	gateway.status.addQuery(QueryID(7), "regression pending answer");
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, taskptr(ScriptedGoal(10, executions, true))), TaskExecutionResult::STOP);
	gateway.status.removeQuery(QueryID(7));
}

TEST_F(Nullkiller2PlannerContinuityRegression, NearTiesStayStableAcrossRebuildsAndTurns)
{
	progressEast();
	for(int turn = 0; turn < 2; ++turn)
	{
		PlannerRegressionAccess::nextTurn(ai);
		auto plan = PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), journey(first, east, 1)});
		EXPECT_EQ(selectedDestination(plan), east);
		EXPECT_EQ(PlannerRegressionAccess::execute(ai, plan.front()), TaskExecutionResult::SUCCESS);
	}
}

TEST_F(Nullkiller2PlannerContinuityRegression, SignificantlyBetterOpportunityCanReplaceIntention)
{
	progressEast();
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.3f), journey(first, east, 1)})), west);
}

TEST_F(Nullkiller2PlannerContinuityRegression, NoProgressExpiresIntention)
{
	progressEast();
	for(int turn = 0; turn < 3; ++turn)
		PlannerRegressionAccess::nextTurn(ai);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), journey(first, east, 1)})), west);
}

TEST_F(Nullkiller2PlannerContinuityRegression, InvalidOrUnsafeGoalDoesNotGainPriorityFromCommitment)
{
	progressEast();
	// Upstream safety evaluator rejects the old journey by assigning zero.
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 0.5f), journey(first, east, 0)})), west);
	EXPECT_TRUE(PlannerRegressionAccess::select(ai, {journey(first, east, std::numeric_limits<float>::infinity())}).empty());
}

TEST_F(Nullkiller2PlannerContinuityRegression, HeroDisappearanceDoesNotApplyStaleCommitment)
{
	progressEast();
	callback->objects.erase(first.id);
	EXPECT_TRUE(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), journey(first, east, 1)}).empty());
}

TEST_F(Nullkiller2PlannerContinuityRegression, CourierCommitmentKeepsSharedRendezvousStable)
{
	progressEast(&second);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(second, west, 1.1f), journey(second, east, 1)})), east);
	// Normal TaskPlan affected-object coordination still allows only one chain.
	auto plan = PlannerRegressionAccess::select(ai, {journey(first, east, 1, &second), journey(second, east, 1.1f)});
	EXPECT_EQ(plan.size(), 1u);
}

TEST_F(Nullkiller2PlannerContinuityRegression, DefenceCandidatesDisableHysteresisForInvolvedHero)
{
	progressEast();
	auto defense = journey(first, west, 1.1f);
	// Marker is read through the same Composition decomposition as real defense.
	int unusedExecutions = 0;
	auto marker = sptr(ScriptedGoal(-1, unusedExecutions, false));
	marker->goalType = DEFEND_TOWN;
	Composition composition;
	composition.addNext(marker).addNext(defense);
	composition.priority = 1.1f;
	auto selected = PlannerRegressionAccess::select(ai, {journey(first, east, 1), sptr(composition)});
	ASSERT_EQ(selected.size(), 1u);
	EXPECT_NE(dynamic_cast<Composition *>(selected.front().get()), nullptr);
}
TEST_F(Nullkiller2PlannerContinuityRegression, TargetDisappearanceAndOwnershipChangeRemoveCommitmentBonus)
{
	CGHeroInstance target{callback.get()};
	target.id = ObjectInstanceID(30);
	target.tempOwner = PlayerColor(1);
	callback->objects[target.id] = &target;
	auto oldJourney = journey(first, east, 1);
	oldJourney->objid = target.id.getNum();
	auto plan = PlannerRegressionAccess::select(ai, {oldJourney});
	ASSERT_EQ(PlannerRegressionAccess::execute(ai, plan.front()), TaskExecutionResult::SUCCESS);
	auto candidate = journey(first, east, 1);
	candidate->objid = target.id.getNum();
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), candidate})), east);
	target.tempOwner = PlayerColor(0);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), candidate})), west);
	callback->objects.erase(target.id);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), candidate})), west);
}

TEST_F(Nullkiller2PlannerContinuityRegression, TurnBoundaryWaitsUntilOutstandingStateSettles)
{
	gateway.status.setMove(true);
	gateway.status.addQuery(QueryID(7), "pending server response");
	gateway.status.attemptedAnsweringQuery(QueryID(7), 42);
	EXPECT_FALSE(gateway.status.isIdle());
	auto waiter = std::async(std::launch::async, [&] { gateway.status.waitTillIdle(); });
	gateway.status.setMove(false);
	// A completed movement alone must not release the end-turn gate.
	EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
	gateway.status.receivedAnswerConfirmation(42, 1);
	EXPECT_EQ(waiter.wait_for(std::chrono::seconds(2)), std::future_status::ready);
	EXPECT_TRUE(gateway.status.isIdle());
}

TEST_F(Nullkiller2PlannerContinuityRegression, RealChainFailedArrivalTriggersSuppressionButNextDayWaitDoesNot)
{
	first.setMovementPoints(0);
	auto path = pathTo(first, east);
	path.nodes.front().turns = 0;
	auto broken = sptr(ExecuteHeroChain(path).setpriority(2));
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, taskptr(*broken)), TaskExecutionResult::REPLAN);
	EXPECT_TRUE(PlannerRegressionAccess::select(ai, {broken}).empty());
	// The same route on the next turn, explicitly requiring another day, is
	// normal partial progress. It must lock the hero rather than fail/retry.
	PlannerRegressionAccess::nextTurn(ai);
	path.nodes.front().turns = 1;
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, taskptr(ExecuteHeroChain(path).setpriority(2))), TaskExecutionResult::SUCCESS);
	EXPECT_EQ(ai.getHeroLockedReason(&first), NK2AI::HeroLockedReason::HERO_CHAIN);
}

TEST_F(Nullkiller2PlannerContinuityRegression, FailedTownSwapDoesNotSuppressAnotherTownsSwap)
{
	CGTownInstance townA{callback.get()}, townB{callback.get()};
	townA.id = ObjectInstanceID(20);
	townB.id = ObjectInstanceID(21);
	townA.ID = townB.ID = Obj::TOWN;
	townA.subID = townB.subID = 0;
	townA.setCustomName("Town A");
	townB.setCustomName("Town B");
	auto failed = sptr(ExchangeSwapTownHeroes(&townA).setpriority(2));
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, taskptr(*failed)), TaskExecutionResult::REPLAN);
	auto other = sptr(ExchangeSwapTownHeroes(&townB).setpriority(1));
	auto next = PlannerRegressionAccess::select(ai, {failed, other});
	ASSERT_EQ(next.size(), 1u);
	EXPECT_TRUE(next.front()->isObjectAffected(townB.id));
	EXPECT_FALSE(next.front()->isObjectAffected(townA.id));
}

TEST_F(Nullkiller2PlannerContinuityRegression, CourierOnlyProgressRemembersItsOwnRendezvous)
{
	auto chain = taskptr(CourierProgressChain(first, second, east, west));
	EXPECT_EQ(PlannerRegressionAccess::execute(ai, chain), TaskExecutionResult::SUCCESS);
	EXPECT_EQ(first.pos, int3(0, 0, 0));
	EXPECT_EQ(second.pos, int3(1, 0, 0));
	PlannerRegressionAccess::nextTurn(ai);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(second, east, 1.1f), journey(second, west, 1)})), west);
	EXPECT_EQ(selectedDestination(PlannerRegressionAccess::select(ai, {journey(first, west, 1.1f), journey(first, east, 1)})), east);
}

TEST_F(Nullkiller2PlannerContinuityRegression, WiderSearchReallyRebuildsOtherwiseCleanProductionState)
{
	PlannerRegressionAccess::initializeFullState(ai);
	PlannerRegressionAccess::update(ai);
	const int narrowRecalculations = callback->pathRecalculations;
	ASSERT_GT(narrowRecalculations, 0);
	ASSERT_FALSE(PlannerRegressionAccess::dirty(ai));
	ASSERT_TRUE(ai.dangerHitMap->isHitMapUpToDate());
	ASSERT_TRUE(ai.dangerHitMap->isTileOwnersUpToDate());
	PlannerRegressionAccess::update(ai);
	ASSERT_EQ(callback->pathRecalculations, narrowRecalculations);
	ASSERT_TRUE(PlannerRegressionAccess::expand(ai));
	PlannerRegressionAccess::update(ai);
	EXPECT_GT(callback->pathRecalculations, narrowRecalculations);
	EXPECT_FALSE(PlannerRegressionAccess::dirty(ai));
	EXPECT_EQ(PlannerRegressionAccess::pathSettings(ai).scoutTurnDistanceLimit, NK2AI::PathfinderSettings::MaxTurnDistanceLimit);
}

}
