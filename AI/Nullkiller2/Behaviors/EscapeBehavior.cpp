/*
 * EscapeBehavior.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "EscapeBehavior.h"

#include "../Engine/Nullkiller.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Analyzers/DangerHitMapAnalyzer.h"

#include <cmath>
#include <limits>
#include <optional>
#include <set>

namespace NK2AI
{
EscapePathEvaluation evaluateEscapePathCandidate(const EscapePathCandidate & candidate)
{
	if(!candidate.currentTileThreatened || !candidate.sameDay || !candidate.changedTile
		|| !candidate.singleHero || !candidate.visibleDestination || !candidate.unblocked
		|| !candidate.battleFree || candidate.safeTurns < 1
		|| !std::isfinite(candidate.movementCost) || candidate.movementCost <= 0.0f)
		return {};

	// Safety is primary. Within the same safety horizon, preserve movement.
	return {true, 10.0f * std::min(candidate.safeTurns, 3)
		+ 1.0f / (1.0f + candidate.movementCost)};
}

namespace
{
// Enemy paths use turn=0 for the enemy's next active move. Only already
// visible armies count, and a hero can stand their ground when stronger.
int firstUnsafeEnemyArrival(const CGHeroInstance * hero, const HitMapNode & tile, float ratio)
{
	int first = 3;
	for(const auto * threat : {&tile.fastestDanger, &tile.maximumDanger})
		if(threat->danger != 0 && threat->turn < first
			&& !isSafeToVisit(hero, threat->danger, ratio))
			first = threat->turn;
	return first;
}
}

std::string Goals::EscapeBehavior::toString() const
{
	return "Escape imminent enemy threat";
}

Goals::TGoalVec Goals::EscapeBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;
	const auto mapSize = aiNk->cc->getMapSize();
	const float ratio = aiNk->settings->getSafeAttackRatio();

	for(const auto * hero : aiNk->cc->getHeroesInfo())
	{
		if(!hero || hero->movementPointsRemaining() <= 0 || aiNk->isHeroLocked(hero))
			continue;
		const auto current = hero->visitablePos();
		if(firstUnsafeEnemyArrival(hero, aiNk->dangerHitMap->getTileThreat(current), ratio) != 0)
			continue;

		// This is an emergency-only bounded search, not a whole-map rescan
		// during every planning pass. Add friendly towns outside this radius
		// so an already-legal Town Portal escape is not missed.
		const int radius = std::clamp(hero->movementPointsRemaining() / 75 + 2, 8, 32);
		std::set<int3> candidates;
		for(int x = std::max(0, current.x - radius); x < std::min(mapSize.x, current.x + radius + 1); ++x)
			for(int y = std::max(0, current.y - radius); y < std::min(mapSize.y, current.y + radius + 1); ++y)
				candidates.emplace(x, y, current.z);
		for(const auto * town : aiNk->cc->getTownsInfo())
			candidates.insert(town->visitablePos());

		std::optional<AIPath> chosen;
		float bestScore = -std::numeric_limits<float>::infinity();
		float bestCost = std::numeric_limits<float>::infinity();
		for(const auto & destination : candidates)
		{
			if(destination == current || !aiNk->cc->isVisible(destination))
				continue;
			const int safeTurns = firstUnsafeEnemyArrival(hero,
				aiNk->dangerHitMap->getTileThreat(destination), ratio);
			if(safeTurns == 0)
				continue;
			for(const auto & path : aiNk->pathfinder->getPathInfo(destination))
			{
				EscapePathCandidate candidate;
				candidate.currentTileThreatened = true;
				candidate.sameDay = path.turn() == 0;
				candidate.changedTile = true;
				candidate.singleHero = path.targetHero == hero && path.heroArmy == hero && path.exchangeCount <= 1;
				candidate.visibleDestination = true;
				candidate.unblocked = !path.getFirstBlockedAction();
				candidate.battleFree = path.getTotalDanger() == 0 && path.getTotalArmyLoss() == 0
					&& !path.hasSingleForecastEncounter;
				candidate.safeTurns = safeTurns;
				candidate.movementCost = path.movementCost();
				const auto evaluation = evaluateEscapePathCandidate(candidate);
				if(!evaluation.accepted || path.targetTile() != destination)
					continue;
				if(evaluation.score > bestScore
					|| (evaluation.score == bestScore && candidate.movementCost < bestCost))
				{
					chosen = path;
					bestScore = evaluation.score;
					bestCost = candidate.movementCost;
				}
			}
		}
		if(chosen)
			tasks.push_back(Goals::sptr(Goals::ExecuteHeroChain(*chosen, nullptr, true)));
	}
	return tasks;
}
}
