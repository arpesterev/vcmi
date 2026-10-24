/*
* Nullkiller.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "Nullkiller.h"

#include "../../../lib/CPlayerState.h"
#include "../../lib/StartInfo.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../AIGateway.h"
#include "../Behaviors/BuildingBehavior.h"
#include "../Behaviors/BuyArmyBehavior.h"
#include "../Behaviors/CaptureObjectsBehavior.h"
#include "../Behaviors/ClusterBehavior.h"
#include "../Behaviors/DefenceBehavior.h"
#include "../Behaviors/ExplorationBehavior.h"
#include "../Behaviors/GatherArmyBehavior.h"
#include "../Behaviors/RecruitHeroBehavior.h"
#include "../Behaviors/StayAtTownBehavior.h"
#include "../Goals/Invalid.h"
#include "Goals/RecruitHero.h"
#include "../Goals/Composition.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Goals/AdventureSpellCast.h"
#include <sstream>
#include <stdexcept>
#include "../../../lib/gameState/CGameState.h"
#include "ResourceTrader.h"
#include <boost/range/algorithm/sort.hpp>

namespace NK2AI
{

using namespace Goals;

// while we play vcmieagles graph can be shared
std::unique_ptr<ObjectGraph> Nullkiller::baseGraph;
thread_local Nullkiller::PlanningTurnLocks * Nullkiller::planningTurnLocks = nullptr;

Nullkiller::Nullkiller()
	: activeHero(nullptr)
	, scanDepth(ScanDepth::MAIN_FULL)
	, useHeroChain(true)
	, pathfinderInvalidated(true)
	, memory(std::make_unique<AIMemory>())
{

}

Nullkiller::~Nullkiller() = default;

bool canUseOpenMap(const std::shared_ptr<CCallback>& cb, const PlayerColor playerID)
{
	if(!cb->getStartInfo()->extraOptionsInfo.cheatsAllowed)
	{
		return false;
	}

	const TeamState * team = cb->getPlayerTeam(playerID);

	const auto hasHumanInTeam = vstd::contains_if(
		team->players,
		[cb](const PlayerColor teamMateID) -> bool
		{
			return cb->getPlayerState(teamMateID)->isHuman();
		}
	);

	return !hasHumanInTeam;
}

void Nullkiller::init(const std::shared_ptr<CCallback> & cbInput, AIGateway * aiGwInput)
{
	cc = cbInput;
	aiGw = aiGwInput;
	playerID = aiGwInput->playerID;

	settings = std::make_unique<Settings>(cc->getStartInfo()->difficulty);

	PathfinderOptions pathfinderOptions(*cc);
	pathfinderOptions.useTeleportTwoWay = true;
	pathfinderOptions.useTeleportOneWay = settings->isOneWayMonolithUsageAllowed();
	pathfinderOptions.useTeleportOneWayRandom = settings->isOneWayMonolithUsageAllowed();

	pathfinderCache = std::make_unique<PathfinderCache>(cc.get(), pathfinderOptions);

	if(canUseOpenMap(cc, playerID))
	{
		useObjectGraph = settings->isObjectGraphAllowed();
		openMap = settings->isOpenMap() || useObjectGraph;
	}
	else
	{
		useObjectGraph = false;
		openMap = false;
	}

	baseGraph.reset();
	priorityEvaluator.reset(new PriorityEvaluator(this));
	priorityEvaluators.reset(
		new SharedPool<PriorityEvaluator>(
			[&]()->std::unique_ptr<PriorityEvaluator>
			{
				return std::make_unique<PriorityEvaluator>(this);
			}));

	dangerHitMap.reset(new DangerHitMapAnalyzer(this));
	buildAnalyzer.reset(new BuildAnalyzer(this));
	objectClusterizer.reset(new ObjectClusterizer(this));
	dangerEvaluator.reset(new FuzzyHelper(this));
	pathfinder.reset(new AIPathfinder(this));
	armyManager.reset(new ArmyManager(cc.get(), this));
	heroManager.reset(new HeroManager(cc.get(), this));
	decomposer.reset(new DeepDecomposer(this));
	armyFormation.reset(new ArmyFormation(cc, this));
}

namespace
{
// Composition descriptions contain transient path details. Use structural goal
// fields instead, so rebuilding a failed goal cannot evade turn-local suppression.
void appendTaskIdentity(std::ostream & out, const AbstractGoal & goal)
{
	out << int(goal.goalType) << ':' << goal.objid << ':' << goal.tile.toString()
		<< ':' << goal.bid << ':' << goal.resID << ':';
	const auto * task = dynamic_cast<const ITask *>(&goal);
	const auto * hero = task ? task->getHero() : goal.hero;
	if(hero)
		out << hero->id.getNum();
	if(task)
	{
		auto objects = task->getAffectedObjects();
		std::sort(objects.begin(), objects.end());
		for(auto id : objects)
			out << 'o' << id.getNum();
	}
	if(const auto * spell = dynamic_cast<const AdventureSpellCast *>(&goal))
		out << 's' << spell->getSpellID().getNum();
	if(goal.town)
		out << 't' << goal.town->id.getNum();
	if(const auto * composition = dynamic_cast<const Composition *>(&goal))
		for(const auto & part : composition->decompose(nullptr))
			appendTaskIdentity(out, *part);
	out << ';';
}

std::string taskIdentity(const ITask * task)
{
	std::ostringstream out;
	if(const auto * goal = dynamic_cast<const AbstractGoal *>(task))
		appendTaskIdentity(out, *goal);
	else
		out << task->toString();
	return out.str();
}

struct TaskIntent
{
	const ExecuteHeroChain * chain = nullptr;
	bool defence = false;
};

void inspectIntent(const AbstractGoal & goal, TaskIntent & result)
{
	result.defence |= goal.goalType == DEFEND_TOWN || goal.goalType == DEFENCE;
	if(const auto * chain = dynamic_cast<const ExecuteHeroChain *>(&goal))
		result.chain = chain;
	if(const auto * composition = dynamic_cast<const Composition *>(&goal))
		for(const auto & part : composition->decompose(nullptr))
			inspectIntent(*part, result);
}

TaskIntent taskIntent(const ITask * task)
{
	TaskIntent result;
	if(const auto * goal = dynamic_cast<const AbstractGoal *>(task))
		inspectIntent(*goal, result);
	return result;
}
}

TaskPlanItem::TaskPlanItem(const TSubgoal & task, float rankingPriority)
	: affectedObjects(task->asTask()->getAffectedObjects())
	, task(task)
	, rankingPriority(rankingPriority)
{
}

Goals::TTaskVec TaskPlan::getTasks() const
{
	Goals::TTaskVec result;

	for(auto & item : tasks)
	{
		result.push_back(taskptr(*item.task));
	}

	// Every item is cloned once. Sorting these distinct shared_ptrs to remove
	// duplicates would replace ranked plan order with allocator address order.

	return result;
}

void TaskPlan::mergeAndFilter(const TSubgoal & task, float rankingPriority)
{
	TGoalVec blockers;
	if(rankingPriority < 0)
		rankingPriority = task->asTask()->priority;

	if (task->asTask()->priority <= 0)
		return;

	for(auto & item : tasks)
	{
		for(auto objid : item.affectedObjects)
		{
			if(task == item.task || task->asTask()->isObjectAffected(objid) || (task->asTask()->getHero() != nullptr && task->asTask()->getHero() == item.task->asTask()->getHero()))
			{
				if(item.rankingPriority >= rankingPriority)
					return;

				blockers.push_back(item.task);
				break;
			}
		}
	}

	vstd::erase_if(tasks, [&](const TaskPlanItem & item)
		{
			return vstd::contains(blockers, item.task);
		});

	tasks.emplace_back(task, rankingPriority);
}

Goals::TTask Nullkiller::choseBestTask(Goals::TGoalVec & tasks) const
{
	vstd::erase_if(tasks, [this](const TSubgoal & task) { return failedTasks.count(taskIdentity(task->asTask())) != 0; });
	if(tasks.empty())
		return taskptr(Invalid());

	for(const TSubgoal & task : tasks)
	{
		if(task->asTask()->priority <= 0)
			task->asTask()->priority = priorityEvaluator->evaluate(task);
	}

	auto bestTask = *vstd::maxElementByFun(tasks, [](const Goals::TSubgoal& task) -> float
		{
			return task->asTask()->priority;
		});

	return taskptr(*bestTask);
}

Goals::TTaskVec Nullkiller::buildPlanAndFilter(TGoalVec & tasks, int priorityTier) const
{
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, tasks.size()),
		[this, &tasks, priorityTier](const tbb::blocked_range<size_t> & r)
		{
			const auto evaluator = this->priorityEvaluators->acquire();
			for(size_t i = r.begin(); i != r.end(); i++)
			{
				const auto & task = tasks[i];
				if(task->asTask()->priority <= 0 || priorityTier != PriorityEvaluator::PriorityTier::BUILDINGS)
					task->asTask()->priority = evaluator->evaluate(task, priorityTier);
			}
		}
	);

	return selectEvaluatedTasks(tasks, settings->isUseFuzzy() ? MIN_PRIORITY : 0);
}

Goals::TTaskVec Nullkiller::selectEvaluatedTasks(const TGoalVec & tasks, float executionFloor) const
{
	std::set<ObjectInstanceID> defenders;
	for(const auto & task : tasks)
	{
		if(!std::isfinite(task->asTask()->priority) || task->asTask()->priority <= 0)
			continue;
		const auto intent = taskIntent(task->asTask());
		if(intent.defence)
			for(auto id : task->asTask()->getAffectedObjects())
				defenders.insert(id);
	}

	std::vector<std::pair<float, TSubgoal>> ranked;
	for(const auto & task : tasks)
	{
		const auto * executable = task->asTask();
		if(!std::isfinite(executable->priority) || executable->priority <= 0 || failedTasks.count(taskIdentity(executable)))
			continue;
		float score = executable->priority;
		const auto intent = taskIntent(executable);
		const auto affected = executable->getAffectedObjects();
		if(intent.chain)
		{
			if(std::any_of(affected.begin(), affected.end(), [this](auto id) { return !cc->getObj(id, false); }))
				continue;
		}
		const bool conflictsWithDefence = std::any_of(affected.begin(), affected.end(),
			[&defenders](auto id) { return defenders.count(id) != 0; });
		// Retention must not promote an ineligible action or consume a courier
		// needed by defence, even when the final carrier is a different hero.
		if(intent.chain && !intent.defence && score >= executionFloor && !conflictsWithDefence)
		{
			const auto found = intentions.find(intent.chain->hero->id);
			const auto * target = intent.chain->objid >= 0 ? cc->getObj(ObjectInstanceID(intent.chain->objid), false) : nullptr;
			if(found != intentions.end() && (found->second.target.getNum() < 0 || found->second.target.getNum() == intent.chain->objid)
				&& found->second.tile == intent.chain->tile
				&& cc->getObj(intent.chain->hero->id, false) == intent.chain->hero
				&& (found->second.target.getNum() < 0 || (target && target->getOwner() == found->second.targetOwner)))
				score = static_cast<float>(std::min(static_cast<double>(score) * INTENTION_PRIORITY_BONUS,
					static_cast<double>(std::numeric_limits<float>::max())));
		}
		ranked.emplace_back(score, task);
	}
	// Stable ties preserve deterministic decomposition order, not hash/pointer order.
	std::stable_sort(ranked.begin(), ranked.end(), [](const auto & a, const auto & b) { return a.first > b.first; });
	TaskPlan taskPlan;
	for(const auto & [score, task] : ranked)
		taskPlan.mergeAndFilter(task, score);
	return taskPlan.getTasks();
}

Goals::TTaskVec Nullkiller::prepareExecutionPlan(Goals::TTaskVec selectedTasks) const
{
	// Intention ranking resolves competing assignments during selection. The
	// remaining independent actions execute by intrinsic urgency; stable ties
	// retain decomposition order. This also keeps below-floor fuzzy goals last.
	std::stable_sort(selectedTasks.begin(), selectedTasks.end(), [](const auto & a, const auto & b)
	{
		return a->priority > b->priority;
	});
	if(selectedTasks.empty())
		selectedTasks.push_back(taskptr(Goals::Invalid()));
	return selectedTasks;
}

void Nullkiller::beginPlanningTurn()
{
	invalidatePlanningRisk();
	++planningTurn;
	failedTasks.clear();
	recoveryCount = 0;
	vstd::erase_if(intentions, [this](const auto & item)
	{
		if(planningTurn - item.second.lastProgressTurn > 2 || !cc->getObj(item.first, false))
			return true;
		if(item.second.target.getNum() < 0)
			return false;
		const auto * target = cc->getObj(item.second.target, false);
		return !target || target->getOwner() != item.second.targetOwner;
	});
}

bool Nullkiller::expandSearch()
{
	if(scanDepth == ScanDepth::ALL_FULL)
		return false;
	scanDepth = ScanDepth::ALL_FULL;
	useHeroChain = false;
	// Path settings are part of the cache key even when no map event occurred.
	invalidatePathfinderData();
	return true;
}

void Nullkiller::decompose(Goals::TGoalVec & results, const Goals::TSubgoal& behavior, int decompositionMaxDepth) const
{
	makingTurnInterruption.interruptionPoint();
	logAi->debug("Decomposing behavior %s", behavior->toString());
	const auto start = std::chrono::high_resolution_clock::now();
	decomposer->decompose(results, behavior, decompositionMaxDepth);

	makingTurnInterruption.interruptionPoint();
	logAi->debug("Decomposing behavior %s done in %ld", behavior->toString(), timeElapsed(start));
}

void Nullkiller::resetState()
{
	std::unique_lock lockGuard(aiStateMutex);

	beginPlanningTurn();
	lockedResources = TResources();
	scanDepth = ScanDepth::MAIN_FULL;
	lockedHeroes.clear();
	dangerHitMap->resetHitmap();
	useHeroChain = true;
	objectClusterizer->reset();

	if(!baseGraph && isObjectGraphAllowed())
	{
		baseGraph = std::make_unique<ObjectGraph>();
		baseGraph->updateGraph(this);
	}
}

void Nullkiller::invalidatePlanningRisk()
{
	planningRiskEpoch.fetch_add(1, std::memory_order_acq_rel);
	preparedPlanningRisk.store(
		std::shared_ptr<const PreparedPlanningRisk>{}, std::memory_order_release);
}

void Nullkiller::preparePlanningRiskForStateUpdate()
{
	// This thread sees only its own scoped capability. An unbound standalone
	// call neither touches another thread's callback nor guesses lock ownership.
	const auto * capability = planningTurnLocks;
	if(!capability || capability->planner != this)
		return;
	preparedPlanningRisk.store(
		std::shared_ptr<const PreparedPlanningRisk>{}, std::memory_order_release);
	if(capability->ensureReady)
		capability->ensureReady();
	if(capability->prepareRisk)
		capability->prepareRisk();
	if(capability->ensureReady)
		capability->ensureReady();
}

void Nullkiller::invalidatePathfinderData()
{
	invalidatePlanningRisk();
	pathfinderInvalidated = true;
}

PathfinderSettings Nullkiller::getPathfinderSettings() const
{
	PathfinderSettings cfg;
	cfg.useHeroChain = useHeroChain;
	cfg.allowBypassObjects = true;
	if(scanDepth == ScanDepth::SMALL || isObjectGraphAllowed())
		cfg.mainTurnDistanceLimit = settings->getMainHeroTurnDistanceLimit();
	if(scanDepth != ScanDepth::ALL_FULL || isObjectGraphAllowed())
		cfg.scoutTurnDistanceLimit = settings->getScoutHeroTurnDistanceLimit();
	return cfg;
}

void Nullkiller::updateState()
{
	const auto start = std::chrono::high_resolution_clock::now();
	auto startMethod = start;
	std::map<std::string, uint64_t> methodToElapsedMs = {};
	logAi->trace("PERFORMANCE: NK2 AI updateState started");

	makingTurnInterruption.interruptionPoint();
	preparePlanningRiskForStateUpdate();
	std::unique_lock lockGuard(aiStateMutex);

	activeHero = nullptr;
	setTargetObject(-1);
	decomposer->reset();

	buildAnalyzer->update();
	methodToElapsedMs.emplace("buildAnalyzer->update", timeElapsed(startMethod));

	if(!pathfinderInvalidated && dangerHitMap->isHitMapUpToDate() && dangerHitMap->isTileOwnersUpToDate())
		logAi->trace("Skipping full state regeneration - up to date");
	else
	{
		startMethod = std::chrono::high_resolution_clock::now();
		memory->removeInvisibleOrDeletedObjects(*cc);
		methodToElapsedMs.emplace("memory->removeInvisibleOrDeletedObjects(*cc)", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		dangerHitMap->updateHitMap();
		methodToElapsedMs.emplace("dangerHitMap->updateHitMap()", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		dangerHitMap->calculateTileOwners();
		methodToElapsedMs.emplace("dangerHitMap->calculateTileOwners()", timeElapsed(startMethod));

		startMethod = std::chrono::high_resolution_clock::now();
		makingTurnInterruption.interruptionPoint();
		heroManager->update();
		methodToElapsedMs.emplace("heroManager->update()", timeElapsed(startMethod));

		logAi->trace("Updating paths");
		const auto cfg = getPathfinderSettings();

		makingTurnInterruption.interruptionPoint();
		startMethod = std::chrono::high_resolution_clock::now();
		const auto heroes = getHeroesForPathfinding();
		methodToElapsedMs.emplace("getHeroesForPathfinding()", timeElapsed(startMethod));
		startMethod = std::chrono::high_resolution_clock::now();
		pathfinder->updatePaths(heroes, cfg);
		methodToElapsedMs.emplace("pathfinder->updatePaths(heroes, cfg)", timeElapsed(startMethod));

		if(isObjectGraphAllowed())
		{
			startMethod = std::chrono::high_resolution_clock::now();
			pathfinder->updateGraphs(
				heroes,
				scanDepth == ScanDepth::SMALL ? PathfinderSettings::MaxTurnDistanceLimit : 10, // TODO: Mircea: Move to constant
				scanDepth == ScanDepth::ALL_FULL ? PathfinderSettings::MaxTurnDistanceLimit : 3); // TODO: Mircea: Move to constant
			methodToElapsedMs.emplace("pathfinder->updateGraphs", timeElapsed(startMethod));
		}

		makingTurnInterruption.interruptionPoint();
		startMethod = std::chrono::high_resolution_clock::now();
		objectClusterizer->clusterize();
		methodToElapsedMs.emplace("objectClusterizer->clusterize()", timeElapsed(startMethod));
		pathfinderInvalidated = false;
	}

	startMethod = std::chrono::high_resolution_clock::now();
	armyManager->update();
	methodToElapsedMs.emplace("armyManager->update()", timeElapsed(startMethod));

	if(const auto timeElapsedMs = timeElapsed(start); timeElapsedMs > 250)
	{
		logAi->warn("PERFORMANCE: NK2 updateState took %ld ms", timeElapsedMs);

#if NK2AI_TRACE_LEVEL >= 1
		for(const auto & [name, elapsedMs] : methodToElapsedMs)
		{
			if(elapsedMs > 25)
				logAi->warn("PERFORMANCE: NK2 updateState %s took %ld ms", name, elapsedMs);
		}
#endif
	}
	else
	{
		logAi->info("PERFORMANCE: NK2 updateState took %ld ms", timeElapsedMs);
	}
}

bool Nullkiller::isHeroLocked(const CGHeroInstance * hero) const
{
	return getHeroLockedReason(hero) != HeroLockedReason::NOT_LOCKED;
}

bool Nullkiller::arePathHeroesLocked(const AIPath & path) const
{
	if(getHeroLockedReason(path.targetHero) == HeroLockedReason::STARTUP)
	{
#if NK2AI_TRACE_LEVEL >= 1
		logAi->trace("Hero %s is locked by STARTUP. Discarding %s", path.targetHero->getObjectName(), path.toString());
#endif
		return true;
	}

	for(const auto & node : path.nodes)
	{
		auto lockReason = getHeroLockedReason(node.targetHero);

		if(lockReason != HeroLockedReason::NOT_LOCKED)
		{
#if NK2AI_TRACE_LEVEL >= 1
			logAi->trace("Hero %s is locked by %d. Discarding %s", path.targetHero->getObjectName(), (int)lockReason,  path.toString());
#endif
			return true;
		}
	}

	return false;
}

HeroLockedReason Nullkiller::getHeroLockedReason(const CGHeroInstance * hero) const
{
	const auto found = lockedHeroes.find(hero);
	return found != lockedHeroes.end() ? found->second : HeroLockedReason::NOT_LOCKED;
}

void Nullkiller::makeTurn()
{
	// The gateway acquires shared path storage BEFORE the game-state lock.
	// Acquiring it here while a caller holds the game lock would invert that
	// order when another AI releases game state for a detached rollout.
	if(!planningTurnLocks || planningTurnLocks->planner != this
		|| !planningTurnLocks->sharedStorage.owns_lock()
		|| planningTurnLocks->sharedStorage.mutex() != &AISharedStorage::locker
		|| !planningTurnLocks->gameState.owns_lock()
		|| planningTurnLocks->gameState.mutex() != &CGameState::mutex)
		throw std::logic_error("Nullkiller makeTurn requires gateway-owned planning locks");
	pathfinderTurnStorageMisses.store(0);
	const int MAX_DEPTH = 10;
	resetState();
	Goals::TGoalVec tasks;
	tracePlayerStatus(true);

	for(int pass = 1; pass <= settings->getMaxPass() && cc->getPlayerStatus(playerID) == EPlayerStatus::INGAME; pass++)
	{
		if (pathfinderTurnStorageMisses.load() != 0)
		{
			logAi->warn("Nullkiller::makeTurn AINodeStorage had %d nodeAllocationFailures in previous pass", pathfinderTurnStorageMisses.load());
			pathfinderTurnStorageMisses.store(0);
		}

		if (!updateStateAndExecutePriorityPass(tasks, pass))
			return;

		tasks.clear();
		decompose(tasks, sptr(CaptureObjectsBehavior()), 1);
		decompose(tasks, sptr(ClusterBehavior()), MAX_DEPTH);
		decompose(tasks, sptr(DefenceBehavior()), MAX_DEPTH);
		decompose(tasks, sptr(GatherArmyBehavior()), MAX_DEPTH);
		// decompose(tasks, sptr(StayAtTownBehavior()), MAX_DEPTH);

		if(!isOpenMap())
			decompose(tasks, sptr(ExplorationBehavior()), MAX_DEPTH);

		TTaskVec selectedTasks;
		int prioOfTask = 0;
		for(int prio = PriorityEvaluator::PriorityTier::INSTAKILL; prio <= PriorityEvaluator::PriorityTier::MAX_PRIORITY_TIER; ++prio)
		{
			prioOfTask = prio;
			selectedTasks = buildPlanAndFilter(tasks, prio);
			if (!selectedTasks.empty() || settings->isUseFuzzy())
			{
				// Activate for deep debugging, otherwise too noisy even for trace level 2
// #if NK2AI_TRACE_LEVEL >= 2
// 				for(auto t : tasks)
// 				{
// 					logAi->info("task of prio %d: %s, prio: %f", prio, t->toString(), t->asTask()->priority);
// 				}
// 				for(auto t : selectedTasks)
// 				{
// 					logAi->info("selected task of prio %d: %s, prio: %f", prio, t->toString(), t->priority);
// 				}
// #endif
				break;
			}
		}

		selectedTasks = prepareExecutionPlan(std::move(selectedTasks));

		bool hasAnySuccess = false;
		bool needsReplan = false;
		for(const auto& selectedTask : selectedTasks)
		{
			if(cc->getPlayerStatus(playerID) != EPlayerStatus::INGAME)
				return;

			if(!areAffectedObjectsPresent(selectedTask))
			{
				logAi->debug("Affected object not found. Canceling task.");
				continue;
			}

			std::string taskDescription = selectedTask->toString();
			HeroRole heroRole = getTaskRole(selectedTask);

			if(heroRole != HeroRole::MAIN || selectedTask->getHeroExchangeCount() <= 1)
				useHeroChain = false;

			// TODO: Mircea: AI suffers from lack of capability, no point to activate SMALL, to test more before deleting the code entirely
			// old-todo: better to check turn distance here instead of priority
			// if((heroRole != HeroRole::MAIN || selectedTask->priority < SMALL_SCAN_MIN_PRIORITY)
			// 	&& scanDepth == ScanDepth::MAIN_FULL)
			// {
			// 	useHeroChain = false;
			// 	scanDepth = ScanDepth::SMALL;
			//
			// 	logAi->trace(
			// 		"Goal %s has low priority %f so decreasing scan depth to gain performance.",
			// 		taskDescription,
			// 		selectedTask->priority);
			// }

			if((settings->isUseFuzzy() && selectedTask->priority < MIN_PRIORITY) || (!settings->isUseFuzzy() && selectedTask->priority <= 0))
			{
				auto heroes = cc->getHeroesInfo();
				const auto hasMp = vstd::contains_if(heroes, [](const CGHeroInstance * h) -> bool
					{
						return h->movementPointsRemaining() > 100;
					});

				if(hasMp && scanDepth != ScanDepth::ALL_FULL)
				{
					logAi->info(
						"Pass %d: Heroes can still move but goal %s has too low priority %f. Increasing to ScanDepth::ALL_FULL",
						pass,
						taskDescription,
						selectedTask->priority);

					expandSearch();
					hasAnySuccess = true;
					break;
				}

				logAi->trace("Pass %d: Goal %s has too low priority. It is not worth doing it.", pass, taskDescription);
				continue;
			}

			logAi->info("Pass %d: Performing task (prioOfTask %d) %s with prio: %d", pass, prioOfTask, selectedTask->toString(), selectedTask->priority);

			if(HeroPtr heroPtr(selectedTask->getHero(), cc.get()); selectedTask->getHero() && !heroPtr.isVerified(false))
			{
				logAi->error("Nullkiller::makeTurn Skipping pass due to unverified hero: %s", heroPtr.nameOrDefault());
			}
			else
			{
				const auto outcome = executeTask(selectedTask);
				if(outcome == TaskExecutionResult::STOP)
					return;
				if(outcome == TaskExecutionResult::REPLAN)
				{
					needsReplan = true;
					break;
				}
				hasAnySuccess = true;
			}
		}

		if(needsReplan)
			continue; // Never execute the rest of a potentially stale plan.

		hasAnySuccess |= ResourceTrader::trade(*buildAnalyzer, *cc, getFreeResources());
		if(!hasAnySuccess)
		{
			logAi->trace("Nothing was done this turn pass. Ending turn.");
			tracePlayerStatus(false);
			return;
		}

		for(const auto * heroInfo : cc->getHeroesInfo())
			AIGateway::pickBestArtifacts(cc, heroInfo);

		if(pass == settings->getMaxPass())
			logAi->warn("MaxPass reached. Terminating AI turn.");
	}
}

bool Nullkiller::updateStateAndExecutePriorityPass(Goals::TGoalVec & tempResults, const int passIndex)
{
	updateState();

	Goals::TTask bestPrioPassTask = taskptr(Goals::Invalid());
	for(int i = 1; i <= settings->getMaxPriorityPass() && cc->getPlayerStatus(playerID) == EPlayerStatus::INGAME; i++)
	{
		tempResults.clear();

		tempResults = DefenceBehavior::decomposeUrgentPurchases(this);
		bestPrioPassTask = choseBestTask(tempResults);
		if(bestPrioPassTask->priority <= 0)
		{
			tempResults.clear();
			decompose(tempResults, sptr(RecruitHeroBehavior()), 1);
			decompose(tempResults, sptr(BuyArmyBehavior()), 1);
			decompose(tempResults, sptr(BuildingBehavior()), 1);
			bestPrioPassTask = choseBestTask(tempResults);
		}

		if(bestPrioPassTask->priority > 0)
		{
			logAi->info("Pass %d: priorityPass %d: Performing task %s with prio: %d", passIndex, i, bestPrioPassTask->toString(), bestPrioPassTask->priority);

			const bool isRecruitHeroGoal = dynamic_cast<RecruitHero*>(bestPrioPassTask.get()) != nullptr;
			HeroPtr heroPtr(bestPrioPassTask->getHero(), cc.get());
			if(!isRecruitHeroGoal && bestPrioPassTask->getHero() && !heroPtr.isVerified(false))
			{
				logAi->error("Nullkiller::updateStateAndExecutePriorityPass Skipping priorityPass due to unverified hero: %s", heroPtr.nameOrDefault());
			}
			else if(executeTask(bestPrioPassTask) == TaskExecutionResult::STOP)
				return false;

			updateState();
		}
		else
		{
			break;
		}

		if(i == settings->getMaxPriorityPass())
		{
			logAi->warn("MaxPriorityPass reached. Terminating priorityPass loop.");
		}
	}
	return true;
}

bool Nullkiller::areAffectedObjectsPresent(const Goals::TTask & task) const
{
	auto affectedObjs = task->getAffectedObjects();

	for(auto oid : affectedObjs)
	{
		if(!cc->getObj(oid, false))
			return false;
	}

	return true;
}

HeroRole Nullkiller::getTaskRole(const Goals::TTask & task) const
{
	HeroPtr heroPtr(task->getHero(), cc.get());
	HeroRole heroRole = HeroRole::MAIN;

	if(heroPtr.isVerified())
		heroRole = heroManager->getHeroRoleOrDefault(heroPtr);

	return heroRole;
}

TaskExecutionResult Nullkiller::executeTask(const Goals::TTask & task)
{
	auto start = std::chrono::high_resolution_clock::now();
	std::string taskDescr = task->toString();
	const auto key = taskIdentity(task.get());
	const auto intent = taskIntent(task.get());
	const auto heroId = intent.chain ? intent.chain->hero->id : ObjectInstanceID(-1);
	const auto affectedObjects = task->getAffectedObjects();
	const auto destination = intent.chain ? intent.chain->tile : int3(-1, -1, -1);
	const auto destinationId = ObjectInstanceID(intent.chain ? intent.chain->objid : -1);
	std::map<ObjectInstanceID, int3> previousPositions;
	std::map<ObjectInstanceID, int3> waypoints;
	if(intent.chain)
	{
		// Nodes are destination-first: the first occurrence is this participant's
		// final rendezvous, not necessarily the recipient's strategic destination.
		for(const auto & node : intent.chain->getPath().nodes)
			if(node.targetHero)
			{
				previousPositions.emplace(node.targetHero->id, node.targetHero->visitablePos());
				waypoints.emplace(node.targetHero->id, node.coord);
			}
		previousPositions.emplace(heroId, intent.chain->hero->visitablePos());
		waypoints[heroId] = destination;
	}

	makingTurnInterruption.interruptionPoint();
	logAi->debug("Trying to realize %s (value %2.3f)", taskDescr, task->priority);

	try
	{
		task->accept(aiGw);
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(goalFulfilledException &)
	{
		logAi->trace("Task %s completed in %lld", taskDescr, timeElapsed(start));
	}
	catch(cannotFulfillGoalException & e)
	{
		logAi->error("Failed to realize subgoal %s: %s", taskDescr, e.what());
		failedTasks.insert(key);
		for(auto id : affectedObjects)
			intentions.erase(id);
		// A local goal error is not proof that an outstanding server command
		// failed. Never issue alternatives while a query/move/battle is pending.
		if(!aiGw->status.isIdle() || recoveryCount >= MAX_LOCAL_RECOVERIES)
			return TaskExecutionResult::STOP;
		++recoveryCount;
		// Chain locks describe the abandoned plan, not an enduring defense duty.
		// Keep STARTUP/DEFENCE locks; let refreshed routes reuse other couriers.
		vstd::erase_if(lockedHeroes, [](const auto & entry) { return entry.second == HeroLockedReason::HERO_CHAIN; });
		invalidatePaths();
		invalidatePathfinderData();
		dangerHitMap->resetHitmap();
		dangerHitMap->resetTileOwners();
		return TaskExecutionResult::REPLAN;
	}

	if(intent.chain)
	{
		const auto * recipient = dynamic_cast<const CGHeroInstance *>(cc->getObj(heroId, false));
		bool progressed = false;
		for(const auto & [id, oldPosition] : previousPositions)
			if(const auto * hero = dynamic_cast<const CGHeroInstance *>(cc->getObj(id, false)))
				progressed |= hero->visitablePos() != oldPosition;
		for(const auto & [id, waypoint] : waypoints)
		{
			const auto * hero = dynamic_cast<const CGHeroInstance *>(cc->getObj(id, false));
			if(!recipient || !hero || intent.defence || recipient->visitablePos() == destination || hero->visitablePos() == waypoint)
				intentions.erase(id);
			else if(progressed)
			{
				const auto targetId = id == heroId ? destinationId : ObjectInstanceID(-1);
				const auto * target = targetId.getNum() >= 0 ? cc->getObj(targetId, false) : nullptr;
				intentions[id] = Intention{targetId, waypoint, target ? target->getOwner() : PlayerColor::NEUTRAL, planningTurn};
			}
		}
	}
	return TaskExecutionResult::SUCCESS;
}

TResources Nullkiller::getFreeResources() const
{
	auto freeRes = cc->getResourceAmount() - lockedResources;
	freeRes.positive();
	return freeRes;
}

void Nullkiller::lockResources(const TResources & res)
{
	lockedResources += res;
}

std::shared_ptr<const CPathsInfo> Nullkiller::getPathsInfo(const CGHeroInstance * h) const
{
	return pathfinderCache->getPathsInfo(h);
}

void Nullkiller::invalidatePaths()
{
	pathfinderCache->invalidatePaths();
}

void Nullkiller::tracePlayerStatus(bool beginning) const
{
#if NK2AI_TRACE_LEVEL >= 1
	float totalHeroesStrength = 0;
	int totalTownsLevel = 0;
	for (const auto *heroInfo : cc->getHeroesInfo())
	{
		totalHeroesStrength += heroInfo->getTotalStrength();
	}
	for (const auto *townInfo : cc->getTownsInfo())
	{
		totalTownsLevel += townInfo->getTownLevel();
	}

	const auto *firstWord = beginning ? "Beginning:" : "End:";
	logAi->info("%s totalHeroesStrength: %f, totalTownsLevel: %d, resources: %s", firstWord, totalHeroesStrength, totalTownsLevel, cc->getResourceAmount().toString());
#endif
}

std::map<const CGHeroInstance *, HeroRole> Nullkiller::getHeroesForPathfinding() const
{
	std::map<const CGHeroInstance *, HeroRole> activeHeroes;
	for(auto hero : cc->getHeroesInfo())
	{
		if(getHeroLockedReason(hero) == HeroLockedReason::DEFENCE)
			continue;

		activeHeroes[hero] = heroManager->getHeroRoleOrDefaultInefficient(hero);
	}
	return activeHeroes;
}

}
