/*
 * SourceTactics.h, part of VCMI engine
 *
 * fheroes2 predicates adapted from src/fheroes2/ai/ai_battle.cpp.
 * Copyright (C) 2024 - 2026 fheroes2 contributors.
 * License: GNU General Public License v2.0 or later.
 * Full text of license available in license.txt file, in main folder.
 *
 * HoMM3 predicate adapted from sushi-shi/homm3-decomp, src/ai_tactical.cpp
 * (project-authored reconstruction published under CC0-1.0).
 * TUM predicates are independent implementations of AI Improvements.txt.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace AIIntegration::Ports
{
namespace HoMM3
{
struct RangedTarget
{
	int32_t attackEffect = 0;
	bool incapacitated = false;
	bool cannotAttack = false;
	bool hasTarget = false;
	int32_t targetTime = 0;
};

/// Port of type_AI_combat_parameters::getRangedAttackValue, after damage projection.
inline int32_t rangedAttackValue(const RangedTarget & target, bool easyComputer)
{
	if(easyComputer)
		return target.attackEffect;
	if(target.incapacitated)
		return target.attackEffect / 10;
	if(!target.cannotAttack && target.hasTarget && target.targetTime <= 5)
	{
		if(target.targetTime <= 0)
			throw std::invalid_argument("HoMM3 target time must be positive");
		return target.attackEffect / target.targetTime;
	}
	return target.attackEffect / 5;
}
}

namespace FHeroes2
{
struct TacticsInput
{
	double armyStrength = 0;
	double enemyArmyStrength = 0;
	double shootersStrength = 0;
	double enemyShootersStrength = 0;
	bool insideDefendedArea = true;
	bool flying = false;
	bool defendingCastle = false;
};

struct Tactics
{
	bool defensive = false;
	bool cautiousOffensive = false;
};

/// Port of analyzeBattleState's stance decision; inputs include siege/spell modifiers.
inline Tactics selectTactics(const TacticsInput & input)
{
	if(!std::isfinite(input.armyStrength) || input.armyStrength <= 0
		|| !std::isfinite(input.enemyArmyStrength) || input.enemyArmyStrength <= 0
		|| !std::isfinite(input.shootersStrength) || input.shootersStrength < 0
		|| !std::isfinite(input.enemyShootersStrength) || input.enemyShootersStrength < 0)
		throw std::invalid_argument("fheroes2 stance requires finite positive armies and nonnegative ranged strengths");

	const double ownRatio = input.shootersStrength / input.armyStrength;
	const double enemyRatio = input.enemyShootersStrength / input.enemyArmyStrength;
	const bool defensive = [&]
	{
		if(!input.insideDefendedArea)
			return false;
		const double overPowerRatio = input.flying ? 6 : 10;
		if(input.armyStrength > input.enemyArmyStrength * overPowerRatio)
			return false;
		if(input.shootersStrength < input.enemyShootersStrength)
			return false;
		if(input.defendingCastle)
			return true;
		if(ownRatio < 0.15)
			return false;
		if(enemyRatio > 0.66)
			return false;
		return true;
	}();
	return {defensive, enemyRatio < 0.15};
}

struct MeleeOutcome
{
	int32_t fromIndex = -1;
	double attackValue = std::numeric_limits<double>::lowest();
	double positionValue = std::numeric_limits<double>::lowest();
	bool canAttackImmediately = false;
};

/// Mirrors ValueHasImproved and IsOutcomeImproved, including the upstream tolerance.
inline bool isOutcomeImproved(const MeleeOutcome & candidate, const MeleeOutcome & previous)
{
	const auto valueHasImproved = [](double primary, double primaryMax, double secondary, double secondaryMax)
	{
		return primaryMax < primary || (secondaryMax < secondary && std::fabs(primaryMax - primary) < 0.001);
	};
	return (candidate.canAttackImmediately && !previous.canAttackImmediately)
		|| (candidate.canAttackImmediately == previous.canAttackImmediately
			&& valueHasImproved(candidate.positionValue, previous.positionValue, candidate.attackValue, previous.attackValue));
}
}

namespace TUM
{
struct ShooterProtection
{
	bool controlledByNeutral = false;
	bool defendingSiege = false;
	bool opponentIsNeutral = false;
	bool hasShooters = false;
	bool opponentHasShooters = false;
	bool opponentCanReachAnyFriendlyStack = false;
};

/// AI Improvements.txt: hold melee against neutrals until an enemy can reach the line.
inline bool holdMeleeForShooters(const ShooterProtection & state)
{
	if(state.controlledByNeutral && !state.defendingSiege)
		return false;
	return state.opponentIsNeutral && state.hasShooters
		&& !state.opponentHasShooters && !state.opponentCanReachAnyFriendlyStack;
}

/// AI Improvements.txt: a ballista must not break Blind, Paralyze or Petrification.
inline bool ballistaMayTarget(bool blind, bool paralyzed, bool petrified)
{
	return !blind && !paralyzed && !petrified;
}

/// AI Improvements.txt: do not repeat an already active creature buff or debuff.
inline bool creatureMayApplyEffect(bool alreadyActive)
{
	return !alreadyActive;
}
}
}
