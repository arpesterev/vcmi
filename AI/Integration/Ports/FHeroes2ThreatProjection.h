/*
 * FHeroes2ThreatProjection.h, experimental source-native tactical port.
 * GPL-2.0-or-later.
 *
 * Pinned source: ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/battle/battle_troop.cpp Unit::evaluateThreatForUnit.
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
struct ThreatProjection
{
	double expectedDamage = 0; // VCMI game-rule damage preview
	int footprintDistance = 0; // smallest distance between occupied cells
	int movementSpeed = 0;
	bool shooter = false;
	bool flying = false;
	bool towerTarget = false;
	bool moved = false;
	bool disabled = false;
	int extraAttacks = 0;
	bool clone = false;
};

// The original evaluates potential damage, then reduces the priority of
// slow ground troops outside their next-turn movement range. VCMI supplies
// hitpoint damage from its *own* legal combat rules, not fheroes2 GetStrength.
// Additional attacks are an upper bound: exact source retaliation simulation
// and special creature abilities are not yet identical between games.
inline double evaluateSourceThreat(const ThreatProjection & threat)
{
	if(!std::isfinite(threat.expectedDamage) || threat.expectedDamage < 0
		|| threat.footprintDistance < 0 || threat.movementSpeed < 0
		|| threat.extraAttacks < 0 || threat.extraAttacks > 20)
		throw std::invalid_argument("FHeroes2 threat inputs must be nonnegative and finite");
	if(threat.disabled || threat.expectedDamage == 0)
		return 0;
	double value = threat.expectedDamage;
	if(!threat.towerTarget && !threat.flying && !threat.shooter
		&& threat.movementSpeed > 0
		&& threat.footprintDistance > threat.movementSpeed + 1)
	{
		value /= 1.5 * threat.footprintDistance / threat.movementSpeed;
	}
	value *= 1 + threat.extraAttacks;
	if(threat.clone)
		value *= 10;
	if(threat.moved)
		value /= 1.25;
	return value;
}
}
