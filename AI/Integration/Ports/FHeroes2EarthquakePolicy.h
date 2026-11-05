/*
 * FHeroes2EarthquakePolicy.h, VCMI tactical AI source projection.
 *
 * ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/ai/ai_battle_spell.cpp: BattlePlanner::spellEarthquakeValue.
 * GPL-2.0-or-later.
 */
#pragma once

#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
// Earthquake improves the tactical prospects of grounded melee attackers
// besieging an archery-heavy army. Native VCMI supplies actual standing
// walls and spell cost; "damage points" is a conservative expected effect,
// not an authoritative prediction of the random siege packet.
inline double earthquakeValue(bool attackingFortifiedTown, int groundMeleeStacks,
	double groundMeleeStrength, double friendlyStrength,
	int standingTargets, int totalTargets, double estimatedDamage,
	double enemyShooterStrength, double enemyStrength)
{
	if(groundMeleeStacks < 0 || standingTargets < 0 || totalTargets < 0
		|| standingTargets > totalTargets
		|| !std::isfinite(groundMeleeStrength) || groundMeleeStrength < 0
		|| !std::isfinite(friendlyStrength) || friendlyStrength < 0
		|| !std::isfinite(estimatedDamage) || estimatedDamage < 0
		|| !std::isfinite(enemyShooterStrength) || enemyShooterStrength < 0
		|| !std::isfinite(enemyStrength) || enemyStrength < 0
		|| enemyShooterStrength > enemyStrength || groundMeleeStrength > friendlyStrength)
		throw std::invalid_argument("FHeroes2 Earthquake requires finite valid army and wall values");
	if(!attackingFortifiedTown || !groundMeleeStacks || !totalTargets || !standingTargets
		|| !friendlyStrength || !enemyStrength || !enemyShooterStrength || !estimatedDamage)
		return 0;
	const double meleeRatio = groundMeleeStrength / friendlyStrength;
	const double targetRatio = static_cast<double>(standingTargets) / totalTargets;
	const double archerRatio = enemyShooterStrength / enemyStrength;
	return groundMeleeStacks * groundMeleeStrength * meleeRatio
		* targetRatio * estimatedDamage * archerRatio * 0.2;
}
}
