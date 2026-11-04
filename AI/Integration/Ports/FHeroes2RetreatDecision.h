/*
 * FHeroes2RetreatDecision.h, part of VCMI engine
 * GPL-2.0-or-later.
 * Source: ihhub/fheroes2@0dbfe9f386f33c2500842f4a59cd284d7f03d541
 * src/fheroes2/ai/ai_battle.cpp, BattlePlanner::planUnitTurn.
 */
#pragma once

#include <cmath>
#include <stdexcept>

namespace AIIntegration::Ports::FHeroes2
{
enum class Withdrawal { FIGHT, RETREAT, SURRENDER };

struct WithdrawalContext
{
	double friendlyStrength = 0;
	double enemyStrength = 0;
	double difficultyRatio = 100.0 / 7.5; // pinned source NORMAL
	bool humanController = false;
	bool canFlee = false;
	bool canSurrender = false;
	bool canAffordSurrender = false;
	bool valuableArtifacts = false;
	bool canRehire = false;
	int primarySkillTotal = 0;
};

// Preserve the source's decision order: do not abandon a viable fight;
// preserve an experienced or artifact-bearing hero when retreat/surrender
// is legally available; surrender only when escape is blocked.
inline Withdrawal decideWithdrawal(const WithdrawalContext & state)
{
	if(!std::isfinite(state.friendlyStrength) || !std::isfinite(state.enemyStrength)
		|| !std::isfinite(state.difficultyRatio) || state.friendlyStrength < 0
		|| state.enemyStrength < 0 || state.difficultyRatio <= 0 || state.primarySkillTotal < 0)
		throw std::invalid_argument("FHeroes2 withdrawal requires finite nonnegative strengths and skills");
	if(state.humanController || state.enemyStrength == 0
		|| state.friendlyStrength * state.difficultyRatio >= state.enemyStrength)
		return Withdrawal::FIGHT;
	if(!state.valuableArtifacts && (!state.canRehire || state.primarySkillTotal < 10))
		return Withdrawal::FIGHT;
	if(state.canFlee)
		return Withdrawal::RETREAT;
	if(state.canSurrender && state.canAffordSurrender)
		return Withdrawal::SURRENDER;
	return Withdrawal::FIGHT;
}
}
