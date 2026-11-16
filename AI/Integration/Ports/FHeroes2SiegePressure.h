/*
 * fheroes2 analyzeBattleState siege policy, ai_battle.cpp at
 * 0dbfe9f386f33c2500842f4a59cd284d7f03d541.
 * Copyright (C) 2024 - 2026 fheroes2 contributors.
 * License: GNU General Public License v2.0 or later.
 */
#pragma once
#include "SourceTactics.h"
#include <optional>

namespace AIIntegration::Ports::FHeroes2
{
struct SiegePressure
{
	bool active = false;
	bool ownDefender = false;
	double towers = 0;
	std::optional<bool> attackerIgnoresCover = false;
};

inline void applySiegePressure(TacticsInput & input, const SiegePressure & siege)
{
	input.defendingCastle = siege.active && siege.ownDefender;
	if(!siege.active)
		return;
	auto & defender = siege.ownDefender ? input.shootersStrength : input.enemyShootersStrength;
	auto & attacker = siege.ownDefender ? input.enemyShootersStrength : input.shootersStrength;
	defender += siege.towers;
	// Unknown native conditional bonuses cannot justify discounting enemy fire.
	if(siege.attackerIgnoresCover == false)
		attacker /= 1.5;
}
}
