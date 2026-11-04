/*
 * FHeroes2TeleportPolicy.h, VCMI tactical AI experimental source port.
 *
 * fheroes2/ai_battle_spell.cpp::BattlePlanner::spellTeleportValue
 * pinned at ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541.
 * GPL-2.0-or-later.
 */
#pragma once

#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
// The pinned fheroes2 planner values TELEPORT only for a non-flying
// melee creature that currently cannot engage but CAN strike after teleport.
// Heroes III movement, wall, hex occupancy and spell legality are delegated
// to VCMI. There is deliberately no "generic movement" score for a wasted
// or immune teleport.
inline double offensiveTeleportValue(double stackStrength, bool groundMelee,
	bool canMeleeThisTurn, bool movedByNativeSpell,
	int nearestEnemyBefore, int nearestEnemyAfter)
{
	if(!std::isfinite(stackStrength) || stackStrength < 0
		|| nearestEnemyBefore < 0 || nearestEnemyAfter < 0)
		throw std::invalid_argument("fheroes2 Teleport requires finite strength and nonnegative distances");
	if(!groundMelee || canMeleeThisTurn || !movedByNativeSpell
		|| nearestEnemyAfter > 1 || nearestEnemyBefore <= 1)
		return 0;
	return stackStrength * 0.10;
}
}
