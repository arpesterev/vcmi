/*
* CaptureObjectsBehavior.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "../AIGateway.h"
#include "../Engine/Nullkiller.h"
#include "../Goals/Composition.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Goals/Invalid.h"
#include "CaptureObjectsBehavior.h"
#include "DefenceBehaviorUtils.h"
#include "../AIUtility.h"

namespace NK2AI
{

using namespace Goals;

namespace
{
bool canInterceptTownThreat(const AIPath & path, const CGObjectInstance * target, const Nullkiller * ai)
{
	const auto * defender = path.targetHero;
	if(!target || target->ID != Obj::HERO || !target->getOwner().isValidPlayer()
		|| ai->cc->getPlayerRelations(ai->playerID, target->getOwner()) != PlayerRelations::ENEMIES
		|| ai->getHeroLockedReason(defender) != HeroLockedReason::DEFENCE
		|| !ai->dangerHitMap->isHitMapUpToDate()
		|| !ai->cc->isVisible(target->visitablePos())
		|| path.targetTile() != target->visitablePos()
		|| path.turn() != 0 || path.exchangeCount != 1 || path.heroArmy != defender
		|| path.getFirstBlockedAction()
		|| !isSafeToVisit(defender, path.heroArmy, path.getTotalDanger(), ai->settings->getSafeAttackRatio()))
		return false;

	for(const auto * town : ai->cc->getTownsInfo())
	{
		if(town->getOwner() != ai->playerID
			|| (town->getVisitingHero() != defender && town->getGarrisonHero() != defender))
			continue;

		auto threats = ai->dangerHitMap->getTownThreats(town);
		const auto & tileThreat = ai->dangerHitMap->getObjectThreat(town);
		threats.push_back(tileThreat.fastestDanger);
		threats.push_back(tileThreat.maximumDanger);
		const auto remainingDefence = estimateTownDefence(*town, nullptr);
		bool interceptsThreat = false;
		for(const auto & threat : threats)
		{
			if(!threat.danger || threat.turn > 1)
				continue;
			if(threat.heroPtr.idOrNone() == target->id)
				interceptsThreat = true;
			else if(threat.turn == 0
				&& !isTownDefenceSufficient(remainingDefence, threat, ai->settings->getSafeAttackRatio()))
				return false;
		}
		// Defending includes removing an attacker today. Enemies that need
		// another day are reconsidered after the battle and on the next turn;
		// they must not permanently pin both armies in their home towns.
		return interceptsThreat;
	}
	return false;
}
}

template <typename T>
bool vectorEquals(const std::vector<T> & v1, const std::vector<T> & v2)
{
	// Sharing one object/type does not make two capture goals identical.
	return v1.size() == v2.size() && std::is_permutation(v1.begin(), v1.end(), v2.begin());
}

bool isBetterTownCapturePath(const AIPath & candidate, const AIPath & current)
{
	if(candidate.turn() != current.turn())
		return candidate.turn() < current.turn();

	if(!vstd::isAlmostEqual(candidate.movementCost(), current.movementCost()))
		return candidate.movementCost() < current.movementCost();

	if(candidate.getTotalArmyLoss() != current.getTotalArmyLoss())
		return candidate.getTotalArmyLoss() < current.getTotalArmyLoss();

	return candidate.getHeroStrength() > current.getHeroStrength();
}

bool isEnemyTownCaptureTarget(const CGObjectInstance * object, const Nullkiller * nullkiller)
{
	if(!object || object->ID != Obj::TOWN)
		return false;

	const auto owner = object->getOwner();
	return owner.isValidPlayer()
		&& nullkiller->cc->getPlayerRelations(nullkiller->playerID, owner) == PlayerRelations::ENEMIES;
}

std::string CaptureObjectsBehavior::toString() const
{
	return "Capture objects";
}

bool CaptureObjectsBehavior::operator==(const CaptureObjectsBehavior & other) const
{
	if(specificObjects != other.specificObjects || forceVisit != other.forceVisit)
		return false;

	if(specificObjects)
		return vectorEquals(objectsToCapture, other.objectsToCapture);

	return vectorEquals(objectTypes, other.objectTypes)
		&& vectorEquals(objectSubTypes, other.objectSubTypes);
}

Goals::TGoalVec CaptureObjectsBehavior::getVisitGoals(
	const std::vector<AIPath> & paths,
	const Nullkiller * nullkiller,
	const CGObjectInstance * objToVisit,
	bool force)
{
	Goals::TGoalVec tasks;
	tasks.reserve(paths.size());
	std::unordered_map<HeroRole, const AIPath *> closestWaysByRole;
	std::unordered_map<const CGHeroInstance *, size_t> bestEnemyTownPathsByHero;
	std::vector<TSubgoal> waysToVisitObj;
	const bool isEnemyTown = isEnemyTownCaptureTarget(objToVisit, nullkiller);
	const bool isEnemyTarget = isEnemyTown || (objToVisit && objToVisit->ID == Obj::HERO
		&& objToVisit->getOwner().isValidPlayer()
		&& nullkiller->cc->getPlayerRelations(nullkiller->playerID, objToVisit->getOwner()) == PlayerRelations::ENEMIES);

	for(const auto & path : paths)
	{
		tasks.push_back(sptr(Goals::Invalid()));
		// Keep one placeholder per input route: defence and army gathering
		// correlate the returned vector with their original path indices.
		if(!path.targetHero || !path.heroArmy || path.nodes.empty()
			|| !std::isfinite(path.movementCost()) || path.movementCost() < 0)
			continue;

#if NK2AI_TRACE_LEVEL >= 2
		logAi->trace("Path found %s", path.toString());
#endif

		if(objToVisit && !force && !shouldVisit(nullkiller, path.targetHero, objToVisit))
		{
#if NK2AI_TRACE_LEVEL >= 2
			logAi->trace("Ignore path. Hero %s should not visit obj %s", path.targetHero->getNameTranslated(), objToVisit->getObjectNameTextID());
#endif
			continue;
		}

		const auto * hero = path.targetHero;
		const auto danger = path.getTotalDanger();
		if (hero->getOwner() != nullkiller->playerID)
			continue;

		if(nullkiller->heroManager->getHeroRoleOrDefaultInefficient(hero) == HeroRole::SCOUT
			&& (path.getTotalDanger() == 0 || path.turn() > 0)
			&& path.exchangeCount > 1)
		{
#if NK2AI_TRACE_LEVEL >= 2
			logAi->trace("Ignore path. Hero %s is SCOUT, chain used and no danger", path.targetHero->getNameTranslated());
#endif
			continue;
		}

		const CGHeroInstance * releasedDefender =
			(nullkiller->canReleaseDefenderForTownCapture(hero, objToVisit, path)
				|| canInterceptTownThreat(path, objToVisit, nullkiller)) ? hero : nullptr;
		if(nullkiller->arePathHeroesLocked(path, releasedDefender))
			continue;

		// Do not end an ordinary same-day pickup within killing reach
		// of an already visible enemy. Forced defence and sieges decide separately.
		// When tests or a replanning pass have no current hitmap, never consult
		// stale / unallocated threat tiles.
		if(!force && !isEnemyTown && path.turn() == 0
			&& nullkiller->dangerHitMap->isHitMapUpToDate()
			&& isKnownUnsafeAtEndOfTurn(nullkiller->dangerHitMap->getTileThreat(path.targetTile()),
				hero, path.heroArmy, 0, nullkiller->settings->getSafeAttackRatio()))
			continue;

		// Match ExecuteHeroChain's safety rule, including its combat-target
		// exception. An enemy town or hero is evaluated as a battle, not a pickup.
		if(!isEnemyTarget && nullkiller->dangerHitMap->isHitMapUpToDate()
			&& hasUnsafeKnownOvernightStop(path, nullkiller))
			continue;

		auto firstBlockedAction = path.getFirstBlockedAction();
		if(firstBlockedAction)
		{
			auto subGoal = firstBlockedAction->decompose(nullkiller, path.targetHero);

#if NK2AI_TRACE_LEVEL >= 2
			// subGoal->toString() was crashing sometims before thread local fix with SET_GLOBAL_STATE_TBB
			logAi->trace("Decomposing special action %s returns %s", firstBlockedAction->toString(), subGoal->toString());
#endif

			if(!subGoal->invalid())
			{
				Composition composition;

				composition.addNext(ExecuteHeroChain(path, objToVisit));
				composition.addNext(subGoal);

				tasks[tasks.size() - 1] = sptr(composition);
			}

			continue;
		}

		auto isSafe = isSafeToVisit(hero, path.heroArmy, danger, nullkiller->settings->getSafeAttackRatio());

#if NK2AI_TRACE_LEVEL >= 2
		logAi->trace(
			"It is %s to visit %s by %s with army %lld, danger %lld and army loss %lld",
			isSafe ? "safe" : "not safe",
			objToVisit ? objToVisit->getObjectNameTextID() : path.targetTile().toString(),
			hero->getNameTextID(),
			path.getHeroStrength(),
			danger,
			path.getTotalArmyLoss());
#endif

		if(isSafe)
		{
			auto heroRole = nullkiller->heroManager->getHeroRoleOrDefaultInefficient(path.targetHero);

			auto & closestWay = closestWaysByRole[heroRole];
			if(!closestWay || closestWay->movementCost() > path.movementCost())
			{
				closestWay = &path;
			}

			if(isEnemyTown)
			{
				const size_t pathIndex = tasks.size() - 1;
				const auto [bestPath, inserted] = bestEnemyTownPathsByHero.try_emplace(path.targetHero, pathIndex);
				if(!inserted && isBetterTownCapturePath(path, paths[bestPath->second]))
					bestPath->second = pathIndex;

				continue;
			}

			auto sharedPtr = sptr(ExecuteHeroChain(path, objToVisit));

			waysToVisitObj.push_back(sharedPtr);
			tasks[tasks.size() - 1] = sharedPtr;
		}
	}

	for(const auto & heroPath : bestEnemyTownPathsByHero)
	{
		const size_t pathIndex = heroPath.second;
		auto sharedPtr = sptr(ExecuteHeroChain(paths[pathIndex], objToVisit));

		waysToVisitObj.push_back(sharedPtr);
		// Selecting one route per hero must preserve the input/output mapping.
		tasks[pathIndex] = sharedPtr;
	}

	for(const auto & wayGoal : waysToVisitObj)
	{
		auto * way = static_cast<ExecuteHeroChain *>(wayGoal.get());
		auto heroRole = nullkiller->heroManager->getHeroRoleOrDefaultInefficient(way->getPath().targetHero);
		const auto * closestWay = closestWaysByRole[heroRole];

		if(closestWay)
		{
			const float movementCost = way->getPath().movementCost();
			// A visit at the current position must not turn into a NaN priority.
			way->closestWayRatio = movementCost > 0
				? std::min(1.0f, closestWay->movementCost() / movementCost)
				: 1.0f;
		}
	}

	return tasks;
}

void CaptureObjectsBehavior::decomposeObjects(
	Goals::TGoalVec & result,
	const std::vector<const CGObjectInstance *> & objs,
	const Nullkiller * nullkiller) const
{
	if(objs.empty())
		return;

	std::mutex sync;
	logAi->debug("Scanning objects, count %d", objs.size());
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, objs.size(), 128),
		[this, &objs, &sync, &result, nullkiller](const tbb::blocked_range<size_t> & r)
		{
			std::vector<AIPath> paths;
			Goals::TGoalVec tasksLocal;

			for(auto i = r.begin(); i != r.end(); i++)
			{
				const auto * objToVisit = objs[i];
				if(!objectMatchesFilter(objToVisit))
					continue;

#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace("Checking object %s, %s", objToVisit->getObjectNameTextID(), objToVisit->visitablePos().toString());
#endif

				// FIXME: Mircea: This one uses the deleted hero
				nullkiller->pathfinder->calculatePathInfo(paths, objToVisit->visitablePos(), nullkiller->isObjectGraphAllowed());

#if NK2AI_TRACE_LEVEL >= 1
				logAi->trace("Found %d paths", paths.size());
#endif
				vstd::concatenate(tasksLocal, getVisitGoals(paths, nullkiller, objToVisit, forceVisit));
			}

			// Indexed placeholders are useful only to getVisitGoals callers.
			// They must not hide the absence of nearby tasks from the far scan.
			vstd::erase_if(tasksLocal, [](const TSubgoal & goal) { return goal->invalid(); });
			std::lock_guard lock(sync); // FIXME: consider using tbb::parallel_reduce instead to avoid mutex overhead
			vstd::concatenate(result, tasksLocal);
		}
	);
}

Goals::TGoalVec CaptureObjectsBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;

	if(specificObjects)
	{
		decomposeObjects(tasks, objectsToCapture, aiNk);
	}
	else if(objectTypes.size())
	{
		decomposeObjects(tasks, aiNk->memory->visitableIdsToObjsVector(*aiNk->cc), aiNk);
	}
	else
	{
		auto nearbyObjects = aiNk->objectClusterizer->getNearbyObjects();
		addVisibleEnemyTowns(nearbyObjects, aiNk);
		decomposeObjects(tasks, nearbyObjects, aiNk);

		if(tasks.empty() || aiNk->getScanDepth() != ScanDepth::SMALL)
		{
			auto farObjects = aiNk->objectClusterizer->getFarObjects();
			vstd::erase_if(farObjects, [&nearbyObjects](const CGObjectInstance * object)
			{
				return vstd::contains(nearbyObjects, object);
			});
			decomposeObjects(tasks, farObjects, aiNk);
		}
	}

	return tasks;
}

void CaptureObjectsBehavior::addVisibleEnemyTowns(std::vector<const CGObjectInstance *> & objects, const Nullkiller * aiNk) const
{
	for(const auto * visibleTown : aiNk->cc->getTownsInfo(false))
	{
		const auto owner = visibleTown->getOwner();
		if(!owner.isValidPlayer())
			continue;

		if(aiNk->cc->getPlayerRelations(aiNk->playerID, owner) != PlayerRelations::ENEMIES)
			continue;

		if(objectMatchesFilter(visibleTown) && !vstd::contains(objects, visibleTown))
			objects.push_back(visibleTown);
	}
}

bool CaptureObjectsBehavior::objectMatchesFilter(const CGObjectInstance * obj) const
{
	if(objectTypes.size() && !vstd::contains(objectTypes, obj->ID.num))
	{
		return false;
	}

	if(objectSubTypes.size() && !vstd::contains(objectSubTypes, obj->subID))
	{
		return false;
	}

	return true;
}

}
