/*
 * Strategic magic and Anti-Magic ratios adapted from fheroes2
 * heroes_base.cpp::GetMagicStrategicValue, spell.cpp::getStrategicValue,
 * ai_battle_spell.cpp::spellEffectValue at
 * 0dbfe9f386f33c2500842f4a59cd284d7f03d541.
 * Copyright (C) 2024 - 2026 fheroes2 contributors.
 * License: GNU General Public License v2.0 or later.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace AIIntegration::Ports::FHeroes2
{
// Pinned ai_battle_spell.cpp::spellDamageValue discounts raw uint32 damage
// BEFORE zero, kills, lethal bonus and wake-up evaluation. Native adapter admits
// only nonwrapping uint32 products; this is not final-score probability weighting.
inline std::optional<uint32_t> resistedDamage(int64_t raw, int resistance)
{
 if(raw < 0 || raw > std::numeric_limits<uint32_t>::max()
  || resistance < 0 || resistance > 100)
  return std::nullopt;
 const uint32_t factor = 100 - resistance;
 if(factor && static_cast<uint64_t>(raw) > std::numeric_limits<uint32_t>::max() / factor)
  return std::nullopt;
 return static_cast<uint32_t>(raw) * factor / 100;
}

enum class StrategicSpellFamily { DAMAGE, HIGH_IMPACT, SUMMON, GENERIC };

inline double strategicSpellValue(StrategicSpellFamily family, double armyStrength,
	int64_t mana, int cost, double singleCastMagnitude)
{
	const int64_t casts = cost > 0 ? std::min<int64_t>(10, std::max<int64_t>(0, mana) / cost) : 0;
	if(casts == 0)
		return 0;
	const double amount = casts == 1 ? 1.0 : casts - 0.05 * casts * casts;
	switch(family)
	{
		case StrategicSpellFamily::DAMAGE: return singleCastMagnitude * amount;
		case StrategicSpellFamily::HIGH_IMPACT: return armyStrength * 0.1 * amount;
		case StrategicSpellFamily::SUMMON: return singleCastMagnitude;
		case StrategicSpellFamily::GENERIC: return armyStrength * 0.04 * amount;
	}
	return 0;
}

// Source: spellResurrectValue rewards permanent recovery when winning.
// The caller supplies native restored strength and observed permanence.
inline double resurrectionValue(double restoredStrength, double armyStrength,
	double enemyArmyStrength, bool permanent)
{
	return restoredStrength * (permanent && armyStrength > enemyArmyStrength ? 2.0 : 1.0);
}

inline double antiMagicRatio(double enemySpellStrength, double enemyArmyStrength,
	bool positiveMagic, bool negativeMagic, bool affordableResurrection,
	bool immunityGained, bool durationUseful)
{
	if(positiveMagic || enemySpellStrength <= 200 || !immunityGained || !durationUseful)
		return 0;
	double ratio = std::min(enemySpellStrength / 200 * 0.036, affordableResurrection ? 0.35 : 0.9);
	if(enemySpellStrength > enemyArmyStrength)
		ratio *= 1.5;
	if(negativeMagic)
		ratio *= 2;
	return ratio;
}
}
