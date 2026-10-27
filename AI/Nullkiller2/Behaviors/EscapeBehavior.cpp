/*
* EscapeBehavior.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#include "StdInc.h"
#include "EscapeBehavior.h"

#include "../AIUtility.h"
#include "../Analyzers/DangerHitMapAnalyzer.h"
#include "../Engine/Nullkiller.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Pathfinding/Actions/DimensionDoorAction.h"
#include "../pforeach.h"

namespace NK2AI
{

using namespace Goals;

namespace
{
bool actionUsesDimensionDoor(const std::shared_ptr<const SpecialAction> & action)
{
	if(!action)
		return false;

	if(dynamic_cast<const AIPathfinding::DimensionDoorAction *>(action.get()))
		return true;

	for(const auto & part : action->getParts())
	{
		if(actionUsesDimensionDoor(part))
			return true;
	}

	return false;
}

bool pathUsesDimensionDoor(const AIPath & path)
{
	return std::any_of(path.nodes.begin(), path.nodes.end(), [](const AIPathNodeInfo & node)
	{
		return actionUsesDimensionDoor(node.specialAction);
	});
}

bool isHeroImmediatelyThreatened(
	const CGHeroInstance * hero,
	const HitMapInfo & threat,
	const float safeAttackRatio)
{
	return threat.turn < 1
		&& threat.danger
		&& !isSafeToVisit(hero, threat.danger, safeAttackRatio);
}

struct EscapePathChoice
{
	AIPath path;
	float score = 0.0f;
};

bool isBetterEscapePath(const AIPath & path, float score, const EscapePathChoice & previous)
{
	if(score > previous.score)
		return true;

	if(vstd::isAlmostEqual(score, previous.score))
		return path.movementCost() < previous.path.movementCost();

	return false;
}

EscapePathCandidate makeEscapePathCandidate(
	const AIPath & path,
	const HitMapInfo & currentThreat,
	const HitMapInfo & destinationThreat,
	uint64_t destinationDanger,
	float safeAttackRatio)
{
	const auto destination = path.targetTile();

	EscapePathCandidate candidate;
	candidate.currentTileThreatensHero = true;
	candidate.sameDay = path.turn() == 0;
	candidate.sameTile = destination == path.targetHero->visitablePos();
	candidate.blockedAction = path.getFirstBlockedAction() != nullptr;
	candidate.singleHeroPath = path.exchangeCount <= 1;
	candidate.destinationSafe = isSafeToVisit(path.targetHero, path.heroArmy, destinationDanger, safeAttackRatio);
	candidate.destinationIsSafer = destinationThreat.threat < currentThreat.threat;
	candidate.usesDimensionDoor = pathUsesDimensionDoor(path);
	candidate.threatReduction = currentThreat.threat - destinationThreat.threat;
	candidate.movementCost = path.movementCost();
	return candidate;
}

void considerEscapePath(
	const AIPath & path,
	const HeroMap<HitMapInfo> & threatenedHeroes,
	const Nullkiller * aiNk,
	float safeAttackRatio,
	std::mutex & sync,
	HeroMap<EscapePathChoice> & bestPaths)
{
	const auto threatenedHero = threatenedHeroes.find(path.targetHero);
	if(threatenedHero == threatenedHeroes.end())
		return;

	if(aiNk->arePathHeroesLocked(path))
		return;

	const auto destination = path.targetTile();
	const auto & currentThreat = threatenedHero->second;
	const auto & destinationThreat = aiNk->dangerHitMap->getTileThreat(destination).fastestDanger;
	const auto & destinationNode = aiNk->dangerHitMap->getTileThreat(destination);
	uint64_t immediateDestinationDanger = destinationThreat.turn < 1 ? destinationThreat.danger : 0;
	if(destinationNode.maximumDanger.turn == 0)
		immediateDestinationDanger = std::max(immediateDestinationDanger, destinationNode.maximumDanger.danger);
	const uint64_t destinationDanger = std::max(path.getTotalDanger(), immediateDestinationDanger);
	const auto candidate = makeEscapePathCandidate(
		path,
		currentThreat,
		destinationThreat,
		destinationDanger,
		safeAttackRatio);

	const auto evaluation = evaluateEscapePathCandidate(candidate);
	if(!evaluation.accepted)
		return;

	std::lock_guard lock(sync);
	const auto previous = bestPaths.find(path.targetHero);
	if(previous == bestPaths.end() || isBetterEscapePath(path, evaluation.score, previous->second))
		bestPaths[path.targetHero] = {path, evaluation.score};
}
}

EscapePathEvaluation evaluateEscapePathCandidate(const EscapePathCandidate & candidate)
{
	EscapePathEvaluation result;

	if(!candidate.currentTileThreatensHero
		|| !candidate.sameDay
		|| candidate.sameTile
		|| candidate.blockedAction
		|| !candidate.singleHeroPath
		|| !candidate.destinationSafe
		|| !candidate.destinationIsSafer
		|| !std::isfinite(candidate.threatReduction) || candidate.threatReduction <= 0
		|| !std::isfinite(candidate.movementCost) || candidate.movementCost <= 0)
	{
		return result;
	}

	result.accepted = true;
	result.score = candidate.threatReduction / std::max(0.1f, candidate.movementCost);
	return result;
}

std::vector<int3> boundedEscapeCandidateTiles(const int3 & origin,
	const int3 & mapSize, int movementPoints, const std::vector<int3> & ownedTowns)
{
	std::set<int3> candidates;
	const int radius = std::clamp(movementPoints / 100 + 2, 6, 24);
	const int stride = std::max(1, (radius + 7) / 8);
	for(int dx = -radius; dx <= radius; ++dx)
		for(int dy = -radius; dy <= radius; ++dy)
		{
			if(dx == 0 && dy == 0)
				continue;
			if(!(std::abs(dx) <= 2 && std::abs(dy) <= 2)
				&& (dx % stride != 0 || dy % stride != 0))
				continue;
			const int3 tile(origin.x + dx, origin.y + dy, origin.z);
			if(tile.x >= 0 && tile.y >= 0 && tile.x < mapSize.x && tile.y < mapSize.y)
				candidates.insert(tile);
		}
	for(const auto & town : ownedTowns)
		if(town.z == origin.z && town != origin && town.x >= 0 && town.y >= 0
			&& town.x < mapSize.x && town.y < mapSize.y)
			candidates.insert(town);
	return {candidates.begin(), candidates.end()};
}

std::string EscapeBehavior::toString() const
{
	return "Escape";
}

Goals::TGoalVec EscapeBehavior::decompose(const Nullkiller * aiNk) const
{
	Goals::TGoalVec tasks;
	HeroMap<HitMapInfo> threatenedHeroes;
	const float safeAttackRatio = aiNk->settings->getSafeAttackRatio();

	for(const CGHeroInstance * candidateHero : aiNk->cc->getHeroesInfo())
	{
		if(aiNk->isHeroLocked(candidateHero))
			continue;

		if(candidateHero->movementPointsRemaining() <= 0)
			continue;
		const auto & node = aiNk->dangerHitMap->getTileThreat(candidateHero->visitablePos());
		const auto & first = node.fastestDanger;
		const auto & strongest = node.maximumDanger;
		if(isHeroImmediatelyThreatened(candidateHero, first, safeAttackRatio))
			threatenedHeroes[candidateHero] = first;
		else if(isHeroImmediatelyThreatened(candidateHero, strongest, safeAttackRatio))
			threatenedHeroes[candidateHero] = strongest;
	}

	if(threatenedHeroes.empty())
		return tasks;

	std::mutex sync;
	HeroMap<EscapePathChoice> bestPaths;
	const int3 mapSize = aiNk->cc->getMapSize();

	// Start with local, already visible tiles plus owned towns (including
	// remote Town Portal landings). This avoids a full-map path enumeration on
	// most threatened turns. A fallback retains unusual Dimension Door escapes.
	std::vector<int3> ownTownTiles;
	for(const auto * town : aiNk->cc->getTownsInfo())
		ownTownTiles.push_back(town->visitablePos());
	for(const auto & [hero, threat] : threatenedHeroes)
	{
		const auto candidates = boundedEscapeCandidateTiles(
			hero->visitablePos(), mapSize, hero->movementPointsRemaining(), ownTownTiles);
		for(const auto & tile : candidates)
		{
			if(!aiNk->cc->isVisible(tile))
				continue;
			for(const auto & path : aiNk->pathfinder->getPathInfo(tile))
				considerEscapePath(path, threatenedHeroes, aiNk, safeAttackRatio, sync, bestPaths);
		}
	}

	// The bounded fast path must not silently delete legal unusual escapes.
	HeroMap<HitMapInfo> unresolved;
	for(const auto & [hero, threat] : threatenedHeroes)
		if(bestPaths.find(hero) == bestPaths.end())
			unresolved.emplace(hero, threat);
	if(!unresolved.empty())
	{
		pforeachTilePaths(mapSize, aiNk,
			[&unresolved, aiNk, safeAttackRatio, &sync, &bestPaths](const int3 &, const std::vector<AIPath> & paths)
		{
			for(const auto & path : paths)
				considerEscapePath(path, unresolved, aiNk, safeAttackRatio, sync, bestPaths);
		});
	}

	for(const auto & bestPath : bestPaths)
		tasks.push_back(sptr(ExecuteHeroChain(bestPath.second.path)));

	return tasks;
}

}
