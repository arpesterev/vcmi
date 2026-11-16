/*
 * FHeroes2OrdinaryThreat.h, part of VCMI engine
 * GPL-2.0-or-later.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace AIIntegration::Ports::FHeroes2
{
struct OrdinaryThreatUnit
{
	uint32_t count = 0;
	uint32_t health = 0;
	uint32_t hitPoints = 0;
	uint32_t minimumDamage = 0;
	uint32_t maximumDamage = 0;
	int attack = 0;
	int defense = 0;
	bool canRetaliate = true;
	uint32_t archeryPercent = 0;
	bool noMeleePenalty = false;
	int damageMode = 0; // -1 Curse, 0 ordinary, +1 Bless.
	bool shield = false;
};

struct OrdinaryThreatDamage
{
	uint32_t potential = 0;
	uint32_t retaliation = 0;
};

inline std::optional<OrdinaryThreatDamage> ordinaryThreatDamage(
	const OrdinaryThreatUnit & attacker, const OrdinaryThreatUnit & defender, bool archerMeleePenalty = false, bool rangedAttack = false)
{
	const auto valid = [](const OrdinaryThreatUnit & unit)
	{
		return unit.count > 0 && unit.hitPoints > 0 && unit.health > 0
			&& unit.health <= uint64_t(unit.count) * unit.hitPoints
			&& unit.health > uint64_t(unit.count - 1) * unit.hitPoints
			&& unit.minimumDamage <= unit.maximumDamage
			&& unit.attack >= 0 && unit.defense >= 0 && unit.archeryPercent <= 100 && unit.damageMode >= -1 && unit.damageMode <= 1;
	};
	if(!valid(attacker) || !valid(defender) || (archerMeleePenalty && rangedAttack))
		return std::nullopt;
	const auto difference = int64_t(attacker.attack) - defender.defense;
	const double multiplier = 1 + (difference > 0
		? 0.1 * std::min<int64_t>(difference, 20)
		: 0.05 * std::max<int64_t>(difference, -16));
	const auto endpoint = [&](uint32_t damage) -> std::optional<uint32_t>
	{
		const uint64_t raw = uint64_t(attacker.count) * damage;
		if(raw > std::numeric_limits<uint32_t>::max())
			return std::nullopt;
		// Pinned CalculateDamageUnit halves the floating endpoint before applying
		// attack/defense and the final integer truncation.
		double base = double(raw);
		// Pinned CalculateDamageUnit adds Archery to each floating endpoint
		// before attack/defense multiplication and final integer truncation.
		if(rangedAttack)
		{
			base += base * attacker.archeryPercent / 100;
			// Pinned Shield divides the floating ranged endpoint after Archery.
			if(defender.shield)
				base /= 2;
		}
		const double value = base * (archerMeleePenalty && !attacker.noMeleePenalty ? 0.5 : 1.0) * multiplier;
		if(value > std::numeric_limits<uint32_t>::max())
			return std::nullopt;
		return std::max<uint32_t>(static_cast<uint32_t>(value), 1);
	};
	const auto minimum = attacker.damageMode > 0 ? std::optional<uint32_t>(0) : endpoint(attacker.minimumDamage);
	const auto maximum = attacker.damageMode < 0 ? std::optional<uint32_t>(0) : endpoint(attacker.maximumDamage);
	if(!minimum || !maximum || (attacker.damageMode == 0
		&& uint64_t(*minimum) + *maximum > std::numeric_limits<uint32_t>::max()))
		return std::nullopt;
	OrdinaryThreatDamage result{attacker.damageMode < 0 ? *minimum
		: attacker.damageMode > 0 ? *maximum : (*minimum + *maximum) / 2, 0};
	if(!rangedAttack && defender.canRetaliate && result.potential < defender.health)
	{
		const uint32_t remainingHealth = defender.health - result.potential;
		const uint32_t survivors = remainingHealth / defender.hitPoints + (remainingHealth % defender.hitPoints != 0);
		const uint64_t perUnitSum = uint64_t(defender.minimumDamage) + defender.maximumDamage;
		const uint64_t perUnit = defender.damageMode < 0 ? defender.minimumDamage
			: defender.damageMode > 0 ? defender.maximumDamage : perUnitSum / 2;
		const uint64_t retaliation = survivors * perUnit;
		if(perUnitSum > std::numeric_limits<uint32_t>::max() || retaliation > std::numeric_limits<uint32_t>::max())
			return std::nullopt;
		result.retaliation = static_cast<uint32_t>(retaliation);
	}
	return result;
}
}
