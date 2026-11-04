/*
 * FHeroes2BerserkOrder.h, VCMI experimental fheroes2 policy.
 * GPL-2.0-or-later.
 * Pinned fheroes2 BattlePlanner::berserkTurn (ai_battle.cpp):
 * scan nearest troops, including normally friendly ones, before
 * choosing an attack or a subsequent-turn approach.
 */
#pragma once

#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
struct BerserkCandidate
{
	int unitId = -1;
	int footprintDistance = 0;
};

// The source uses board's nearest-troop order, not normal combat value.
// VCMI's projection explicitly chooses stable unit-ID ordering on distance
// ties. Neither invulnerable nor dead units should enter this comparison.
inline bool preferBerserkCandidate(BerserkCandidate candidate, BerserkCandidate previous)
{
	if(candidate.unitId < 0 || candidate.footprintDistance < 0)
		throw std::invalid_argument("FHeroes2 Berserk candidate requires valid unit and distance");
	if(previous.unitId < 0)
		return true;
	if(previous.footprintDistance < 0)
		throw std::invalid_argument("FHeroes2 prior Berserk distance must be nonnegative");
	return candidate.footprintDistance < previous.footprintDistance
		|| (candidate.footprintDistance == previous.footprintDistance
			&& candidate.unitId < previous.unitId);
}
}
