/*
 * fheroes2 ranged-pressure policy from ai_battle.cpp at
 * 0dbfe9f386f33c2500842f4a59cd284d7f03d541.
 * Copyright (C) 2024 - 2026 fheroes2 contributors.
 * License: GNU General Public License v2.0 or later.
 */
#pragma once
#include <algorithm>
#include <cstdint>

namespace AIIntegration::Ports::FHeroes2
{
class MaximumSpellDamage
{
	int64_t mana;
	double maximum = 0;
public:
	explicit MaximumSpellDamage(int64_t availableMana) : mana(availableMana) {}
	void consider(bool combat, bool damage, int64_t cost, double magnitude)
	{
		if(combat && damage && cost >= 0 && mana >= cost)
			maximum = std::max(maximum, magnitude);
	}
	double value() const { return maximum; }
};

// analyzeBattleState applies this after siege modifiers, before stance ratios.
inline void addHeroRangedPressure(double & own, double & enemy, double ownSpell, double enemySpell)
{
	if(own > 1)
		own += ownSpell;
	enemy += enemySpell;
}
}
