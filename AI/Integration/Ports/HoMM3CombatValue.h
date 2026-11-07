/*
 * HoMM3CombatValue.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 *
 * Adapted from sushi-shi/homm3-decomp, src/army.cpp:2285-2380,
 * e12906accf96c2662b1abadbc49ddc111991bf21 (CC0-1.0).
 * These are reconstructed source formulas, not retail runtime validation.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace AIIntegration::Ports::HoMM3
{
struct UnitCombatValueFacts
{
	int64_t attackDifference = 0;
	int64_t defenseDifference = 0;
	double defenderDamageFactor = 1;
	double baseFightValue = 0;
	bool ranged = false;
	bool canShoot = false;
	bool shooter = false;
	bool doubleRangedAttack = false;
	bool ballista = false;
	int artilleryLevel = 0;
	bool alteredDamage = false;
	double baseAverageDamage = 1;
	double averageDamage = 1;
	bool siegeOrSummoned = false;
	int64_t totalHitPoints = 0;
	int64_t ordinarySideHitPoints = 0;
};

inline double unitCombatValue(const UnitCombatValueFacts & facts)
{
	if(!std::isfinite(facts.defenderDamageFactor) || facts.defenderDamageFactor < 0
		|| !std::isfinite(facts.baseFightValue) || facts.baseFightValue < 0
		|| facts.artilleryLevel < 0 || facts.artilleryLevel > 3
		|| facts.totalHitPoints < 0 || facts.ordinarySideHitPoints < 0
		|| !std::isfinite(facts.averageDamage) || facts.averageDamage < 0
		|| !std::isfinite(facts.baseAverageDamage) || facts.baseAverageDamage <= 0)
		throw std::invalid_argument("Invalid HoMM3 unit combat value facts");

	const double defense = (static_cast<double>(facts.defenseDifference) * 0.05 + 1.0)
		* facts.defenderDamageFactor;
	const bool ranged = facts.ranged && facts.canShoot;
	double attack = static_cast<double>(facts.attackDifference) * 0.05 + 1.0;
	if(!ranged && facts.shooter)
		attack *= 0.5;
	if(facts.ballista)
	{
		constexpr double artilleryFactors[] = {1.0, 1.5, 1.5, 2.0};
		if(facts.artilleryLevel > 1)
			attack += attack;
		attack *= artilleryFactors[facts.artilleryLevel];
	}
	if(facts.alteredDamage)
		attack = facts.averageDamage / facts.baseAverageDamage * attack;
	if(ranged && facts.doubleRangedAttack)
		attack += attack;
	// Modded native modifiers can leave the source formula's real domain.
	// Saturate there instead of propagating NaN into action ordering.
	double result = std::sqrt(std::max(0.0, attack * defense)) * facts.baseFightValue;
	if(facts.siegeOrSummoned)
	{
		if(facts.totalHitPoints == 0)
			result = 0.1;
		else
			result = static_cast<double>(facts.ordinarySideHitPoints) * result
				/ (static_cast<double>(facts.totalHitPoints) + static_cast<double>(facts.ordinarySideHitPoints));
	}
	if(!std::isfinite(result))
		throw std::overflow_error("HoMM3 unit combat value overflow");
	return result;
}

// Retail longs are signed 32-bit. Saturation defines the native modded
// overflow case; values in the source domain retain truncation toward zero.
inline int32_t combatValueInteger(double value)
{
	if(!std::isfinite(value) || value < 0)
		throw std::invalid_argument("Invalid HoMM3 combat value");
	return static_cast<int32_t>(std::min(value,
		static_cast<double>(std::numeric_limits<int32_t>::max())));
}

inline int32_t lossCombatValue(double unitValue, int64_t count, int64_t hitPoints,
	int64_t topWound, bool clone, int64_t damage, bool killsOnly)
{
	if(!std::isfinite(unitValue) || unitValue < 0 || count < 0 || hitPoints <= 0
		|| topWound < 0 || topWound >= hitPoints || damage < 0)
		throw std::invalid_argument("Invalid HoMM3 loss combat value facts");
	if(clone)
		return combatValueInteger(static_cast<double>(count) * unitValue / 5.0);
	if(killsOnly)
		unitValue = 1000.0;
	const int64_t lost = damage % hitPoints >= hitPoints - topWound ? topWound : 0;
	return combatValueInteger((static_cast<double>(lost) + static_cast<double>(damage))
		* unitValue / static_cast<double>(hitPoints));
}
}
