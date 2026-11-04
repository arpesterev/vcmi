/*
 * Experimental VCMI port of fheroes2 BattlePlanner::spellEarthquakeValue.
 * Reference: ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * GPL-2.0-or-later. VCMI uses native wall types and spell legality.
 */
#pragma once
#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
inline double earthquakePriority(int groundMeleeUnits, double groundMeleeStrength,
	double friendlyStrength, double enemyShooterStrength, double enemyStrength,
	int damagedWallTargets, int totalWallTargets, double expectedWallDamage)
{
	if(groundMeleeUnits < 0 || damagedWallTargets < 0 || totalWallTargets < 0
		|| damagedWallTargets > totalWallTargets
		|| !std::isfinite(groundMeleeStrength) || !std::isfinite(friendlyStrength)
		|| !std::isfinite(enemyShooterStrength) || !std::isfinite(enemyStrength)
		|| !std::isfinite(expectedWallDamage) || groundMeleeStrength < 0
		|| friendlyStrength < groundMeleeStrength || enemyShooterStrength < 0
		|| enemyStrength < enemyShooterStrength || expectedWallDamage < 0)
		throw std::invalid_argument("FHeroes2 earthquake priority requires valid strength, walls and damage");
	if(groundMeleeUnits == 0 || friendlyStrength == 0 || enemyStrength == 0
		|| totalWallTargets == 0 || damagedWallTargets == 0)
		return 0;
	return groundMeleeUnits * groundMeleeStrength
		* (groundMeleeStrength / friendlyStrength)
		* (static_cast<double>(damagedWallTargets) / totalWallTargets)
		* expectedWallDamage * (enemyShooterStrength / enemyStrength) * 0.2;
}
}
