/*
 * FHeroes2DragonSlayer.h, experimental Heroes II to VCMI combat port.
 * GPL-2.0-or-later.
 * Reference: fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * ai_battle_spell.cpp:BattlePlanner::spellDragonSlayerValue.
 */
#pragma once

#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
// The pinned source scales a Bloodlust-like attack bonus by the strength
// fraction represented by live enemy dragons. Heroes III's native SLAYER
// differs: its magnitude depends on mastery and qualifying targets. Use
// source Bloodlust's conservative 0.10 as a base, and only when the native
// hypothetical spellcast actually adds a Slayer effect.
inline double dragonSlayerValue(double friendlyStackStrength, double enemyDragonsStrength,
	double enemyArmyStrength, bool newSlayerEffect)
{
	if(!std::isfinite(friendlyStackStrength) || !std::isfinite(enemyDragonsStrength)
		|| !std::isfinite(enemyArmyStrength) || friendlyStackStrength < 0
		|| enemyDragonsStrength < 0 || enemyArmyStrength < 0
		|| enemyDragonsStrength > enemyArmyStrength)
		throw std::invalid_argument("FHeroes2 Dragon Slayer requires valid nonnegative strengths");
	if(!newSlayerEffect || enemyArmyStrength == 0 || enemyDragonsStrength == 0)
		return 0;
	return friendlyStackStrength * 0.10 * enemyDragonsStrength / enemyArmyStrength;
}
}
