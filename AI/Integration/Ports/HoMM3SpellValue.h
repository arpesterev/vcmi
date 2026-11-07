/*
 * HoMM3SpellValue.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 * Source: sushi-shi/homm3-decomp, e12906accf96c2662b1abadbc49ddc111991bf21,
 * src/ai_tactical.cpp (CC0-1.0). Combat values are native VCMI projections.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AIIntegration::Ports::HoMM3
{
// VCMI BattleSpellMechanics::isReflected, restricted to hero casting.
inline bool nativeHeroSpellMayReflect(bool massive, bool directRange, bool negative, int mirrorChance)
{
	return !massive && directRange && negative && mirrorChance > 0;
}

inline int spellPlanningRounds(double friendlyAwake, double enemyAwake)
{
	const double high = std::max(friendlyAwake, enemyAwake);
	const double low = std::min(friendlyAwake, enemyAwake);
	if(high <= 0 || low * 5 <= high)
		return 1;
	constexpr double odds[] = {2.6, 1.9, 1.5, 1.31, 1.2, 1.13};
	int rounds = 1;
	for(double ratio : odds)
	{
		if(high / low >= ratio)
			break;
		++rounds;
	}
	return rounds;
}

inline double spellDurationValue(int duration, int rounds, bool moved)
{
	return std::max(0.0, std::min(1.0, static_cast<double>(duration) / rounds)
		- (moved ? 1.0 / rounds : 0.0));
}

inline double spellDamageValue(double totalValue, int64_t health, int64_t damage, bool inactive)
{
	if(health <= 0 || damage <= 0)
		return 0;
	double value = totalValue * static_cast<double>(std::min(health, damage)) / health;
	if(inactive && value < totalValue)
		value = 2 * value - totalValue;
	return value;
}

inline double massSpellDamageValue(double enemyDamage, double friendlyDamage,
	double enemyTotal, double friendlyTotal)
{
	friendlyDamage = std::max(0.0, friendlyDamage);
	if(enemyTotal <= 0 || friendlyTotal <= 0 || enemyDamage <= friendlyDamage
		|| friendlyDamage >= friendlyTotal
		|| enemyDamage / enemyTotal <= friendlyDamage / friendlyTotal)
		return 0;
	return enemyDamage - friendlyDamage;
}

inline double manaWeightedSpellValue(double value, int64_t mana, int cost)
{
	if(value <= 0 || cost < 0 || mana < cost)
		return 0;
	// Zero-cost native spells have no retail counterpart.
	if(cost == 0 || mana / cost >= 7)
		return value * 2.5;
	return value * std::sqrt(static_cast<double>(mana / cost));
}

inline double attackSpellValue(double total, double before, double after,
	int64_t targetHealth, double duration)
{
	if(before <= 0 || after <= before)
		return 0;
	after = std::min(after, static_cast<double>(targetHealth));
	if(after <= before)
		return 0;
	return (std::sqrt(after / before) - 1) * total * duration;
}

inline double curseSpellValue(double total, double before, double after, double duration)
{
	if(before <= 0 || after >= before)
		return 0;
	return total * (1 - std::sqrt(std::max(0.0, after) / before)) * duration;
}

inline double removedAttackSpellValue(double total, double before, double after,
	int64_t targetHealth, double duration)
{
	if(after >= before)
		return curseSpellValue(total, after, before, duration);
	if(after <= 0)
		return -total * duration;
	return -attackSpellValue(total, after, before, targetHealth, duration);
}

inline double blindSpellValue(double total, double awakeEnemy, int retaliationPercent, double duration)
{
	if(total >= awakeEnemy)
		return total * (0.5 - std::sqrt(std::clamp(retaliationPercent, 0, 100) / 400.0));
	return total * duration;
}
}
