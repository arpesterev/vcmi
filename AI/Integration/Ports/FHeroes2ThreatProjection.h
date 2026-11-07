/*
 * FHeroes2ThreatProjection.h, part of VCMI.
 * GPL-2.0-or-later.
 * Projected from ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * battle_troop.cpp:Battle::Unit::evaluateThreatForUnit.
 *
 * This preserves the source's basic damage, distance, double-strike and
 * state modifiers while keeping VCMI's damage/stat mechanics authoritative.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
struct ThreatProjection
{
	double damage = 0;
	double retaliationDamage = 0;
	double attackerHealth = 1;
	int distance = 1;
	int movement = 1;
	int attacks = 1;
	bool ranged = false;
	bool flying = false;
	bool targetIsTower = false;
	bool clone = false;
	bool originallyFriendly = false;
	bool mindControlled = false;
	bool disabled = false;
	bool alreadyActed = false;
};

inline double projectRelativeThreat(const ThreatProjection & state)
{
	if(!std::isfinite(state.damage) || !std::isfinite(state.retaliationDamage)
		|| !std::isfinite(state.attackerHealth) || state.damage < 0
		|| state.retaliationDamage < 0 || state.attackerHealth <= 0
		|| state.distance <= 0 || state.movement < 0 || state.attacks <= 0)
		throw std::invalid_argument("FHeroes2 threat projection needs valid combat values");
	double threat = state.damage;
	if(!state.targetIsTower && !state.ranged && !state.flying)
	{
		if(state.movement == 0)
			return 0; // immobile ground attacker cannot reach another unit
		if(state.distance > state.movement + 1)
			threat /= 1.5 * state.distance / state.movement;
	}
	// fheroes2's second strike is discounted when the first strike draws
	// retaliation. Additional native strikes beyond two remain a projection.
	if(state.attacks > 1)
	{
		const double survival = 1.0 - std::min(state.retaliationDamage / state.attackerHealth, 1.0);
		threat *= 1.0 + survival;
	}
	if(state.clone)
		threat *= 10.0;
	if(state.originallyFriendly)
		threat *= -2.0;
	else if(state.mindControlled)
		threat *= -1.0;
	else if(state.disabled)
		threat = 0.0;
	else if(state.alreadyActed)
		threat /= 1.25;
	return threat;
}
}

