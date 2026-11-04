/*
 * FHeroes2SiegeGeometry.h, VCMI experimental fheroes2 tactical port.
 * GPL-2.0-or-later.
 * Pinned fheroes2 BattlePlanner::meleeUnitOffense uses cellsUnderWallsIndexes
 * when a melee attacker cannot approach the defending garrison. VCMI supplies
 * different legal wall geometry and move distances; this is a projection.
 */
#pragma once

#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
struct SiegeWallApproach
{
	int remainingWallDistance = 0;
	int movementCost = 0;
	int headHex = -1;
};

// If the garrison has no improving attack approach, prioritize legal moves
// that get closer to an intact wall. Source fheroes2 prefers the cheapest
// reachable wall cell; this projection uses remaining distance first when
// no wall cell can be reached in the current activation.
inline bool preferSiegeWallStep(SiegeWallApproach candidate, SiegeWallApproach previous,
	int initialDistance)
{
	if(candidate.remainingWallDistance < 0 || candidate.movementCost < 0 || candidate.headHex < 0
		|| initialDistance < 0)
		throw std::invalid_argument("FHeroes2 siege approach needs valid native distances");
	if(candidate.remainingWallDistance >= initialDistance)
		return false;
	if(previous.headHex < 0)
		return true;
	if(candidate.remainingWallDistance != previous.remainingWallDistance)
		return candidate.remainingWallDistance < previous.remainingWallDistance;
	if(candidate.movementCost != previous.movementCost)
		return candidate.movementCost < previous.movementCost;
	return candidate.headHex < previous.headHex;
}
}
