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

namespace AIIntegration::Ports::FHeroes2
{
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
