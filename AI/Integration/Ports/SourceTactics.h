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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace AIIntegration::Ports
{
namespace HoMM3
{
// Source: ai_tactical.cpp, type_AI_attack_hex_chooser::check_adjacent_hexes.
// Once the attack value and turn count tie, Cavalier/Champion choose the
// costlier approach to preserve jousting; other troops prefer a cheaper route.
// This predicate applies only to positions reachable in the same turn.
inline bool preferEqualValueMeleeHex(int candidatePathCost, int currentPathCost, bool jouster)
{
	if(candidatePathCost < 0 || currentPathCost < 0)
		throw std::invalid_argument("HoMM3 melee tie break requires nonnegative path costs");
	return jouster ? candidatePathCost > currentPathCost : candidatePathCost < currentPathCost;
}

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

// Pinned fheroes2 BattlePlanner::isPositionLocatedInDefendedArea tests
// *both* cells of a wide unit, including the reflected footprint. Its
// castle case uses castle geometry rather than the field's x midpoint.
// The caller supplies authoritative VCMI occupied hexes and geometry.
template<typename Footprint, typename IsDefended>
inline bool isPositionInDefendedArea(const Footprint & occupied, IsDefended isDefended)
{
	return occupied.begin() != occupied.end()
		&& std::all_of(occupied.begin(), occupied.end(), isDefended);
}

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

// Source: fheroes2 AI::BattlePlanner::getSpellHasteRatio and
// getSpellSlowRatio (ai_battle_spell.cpp). The before/after speeds must
// come from actual VCMI spell simulation: if no speed changes, the spell
// has no movement value, including when its effect is already active.
inline double hasteSpeedRatio(int before, int after, double enemyAverageSpeed,
	bool currentlySlowed, bool shooter, bool defensiveTactics)
{
	if(before < 0 || after < 0 || !std::isfinite(enemyAverageSpeed) || enemyAverageSpeed < 0)
		throw std::invalid_argument("fheroes2 Haste ratio requires valid speeds");
	if(after <= before)
		return 0;
	double ratio = 0.05 * (after - before);
	if(before < enemyAverageSpeed)
		ratio *= 2;
	if(currentlySlowed)
		ratio *= 2;
	else if(shooter || defensiveTactics)
		ratio /= 2;
	return ratio;
}

inline double slowSpeedRatio(int before, int after, double friendlyAverageSpeed,
	bool shooter, bool attackingCastle, bool currentlyHasted, bool flying,
	int distanceFromApproachEdge)
{
	if(before < 0 || after < 0 || !std::isfinite(friendlyAverageSpeed)
		|| friendlyAverageSpeed < 0 || distanceFromApproachEdge < 1)
		throw std::invalid_argument("fheroes2 Slow ratio requires valid speeds and distance");
	if(after >= before)
		return 0;
	// Source special case for enemy archers or an attacking siege.
	if(shooter || attackingCastle)
		return 0.01;
	double ratio = 0.1 * (before - after);
	if(before < friendlyAverageSpeed)
		ratio /= 2;
	if(currentlyHasted)
		ratio *= 2;
	else if(!flying)
		ratio /= distanceFromApproachEdge;
	return ratio;
}

// Source: fheroes2 BattlePlanner::getSpellDisruptingRayRatio
// (ai_battle_spell.cpp). Spell::ExtraValue is three defense points in the
// pinned Heroes II policy. The VCMI caller supplies the *observed* defense
// reduction from a real hypothetical cast, not an assumed successful effect.
// It cannot score a no-op, a fully depleted target or a resisted spell.
inline double disruptingRayDefenseRatio(int beforeDefense, int afterDefense,
	double friendlyArmyStrength, double targetStrength)
{
	if(beforeDefense < 0 || afterDefense < 0 || !std::isfinite(friendlyArmyStrength)
		|| !std::isfinite(targetStrength) || friendlyArmyStrength <= 0 || targetStrength <= 0)
		throw std::invalid_argument("fheroes2 Disrupting Ray requires valid defense and positive armies");
	if(beforeDefense <= 1 || afterDefense >= beforeDefense)
		return 0;
	constexpr double sourceDefenseReduction = 3.0;
	const double actualReduction = std::min(
		static_cast<double>(beforeDefense - 1), static_cast<double>(beforeDefense - afterDefense));
	double ratio = 0.2 * std::min(1.0, actualReduction / sourceDefenseReduction);
	if(friendlyArmyStrength < targetStrength)
		ratio *= friendlyArmyStrength / targetStrength;
	return ratio;
}

// Pinned fheroes2 BattlePlanner::spellEffectValue gives Bless and Curse
// 15% of the affected stack's strength, doubled if counteracting the
// opposite spell. The caller must first confirm that the VCMI hypothetical
// cast actually changes the unit's damage; do not score immunity or a no-op.
// The original fheroes2 check excludes creatures with a fixed damage range.
inline double blessCurseDamageRatio(int baseMinDamage, int baseMaxDamage,
	bool hasOppositeEffect, bool effectiveNativeChange)
{
	if(baseMinDamage < 0 || baseMaxDamage < baseMinDamage)
		throw std::invalid_argument("fheroes2 Bless/Curse requires an ordered nonnegative damage range");
	if(baseMinDamage == baseMaxDamage || !effectiveNativeChange)
		return 0;
	return hasOppositeEffect ? 0.3 : 0.15;
}

// Pinned fheroes2 BattlePlanner::spellEffectValue gives Stone Skin 10%
// of the affected unit's strength. No-op spells must not be scored: use
// the defense before/after an actual VCMI hypothetical cast.
inline double stoneSkinDefenseRatio(int beforeDefense, int afterDefense)
{
	if(beforeDefense < 0 || afterDefense < 0)
		throw std::invalid_argument("fheroes2 Stone Skin requires nonnegative defense");
	return afterDefense > beforeDefense ? 0.1 : 0.0;
}

// Pinned fheroes2 BattlePlanner::spellEffectValue values Bloodlust at 10% of
// a friendly stack's strength. Bloodlust changes melee attack; a hypothetical
// VCMI cast must actually increase melee attack to receive source value.
inline double bloodlustAttackRatio(int beforeAttack, int afterAttack)
{
	if(beforeAttack < 0 || afterAttack < 0)
		throw std::invalid_argument("fheroes2 Bloodlust requires nonnegative attack");
	return afterAttack > beforeAttack ? 0.1 : 0.0;
}

// Pinned fheroes2 BattlePlanner::spellEffectValue values Blind at 80% of
// the affected enemy strength, or 40% if this is the last enemy with an
// available retaliation and without unlimited retaliations. The native VCMI
// simulation must actually apply the Blind effect, and a pre-existing
// disable must not be counted as another successful spell.
inline double blindControlRatio(bool isLastEnemy, bool unlimitedRetaliations,
	bool canRetaliate, bool alreadyDisabled, bool effectiveNativeChange)
{
	if(alreadyDisabled || !effectiveNativeChange)
		return 0.0;
	if(isLastEnemy && (unlimitedRetaliations || !canRetaliate))
		return 0.0;
	return isLastEnemy ? 0.4 : 0.8;
}

// fheroes2 meleeUnitOffense: choose a target that cannot evade engagement
// before using its threat / path-distance score. VCMI projects the threat
// with native combat value; this is not the original evaluateThreatForUnit.
struct DistantTarget
{
	double threat = 0;
	int pathDistance = 0;
	bool cannotEvade = false;
	int unitId = -1;
};

inline bool preferDistantTarget(const DistantTarget & candidate, const DistantTarget & previous)
{
	if(!std::isfinite(candidate.threat) || candidate.threat < 0 || candidate.pathDistance <= 0
		|| !std::isfinite(previous.threat) || previous.threat < 0
		|| (previous.unitId >= 0 && previous.pathDistance <= 0))
		throw std::invalid_argument("fheroes2 distant target needs finite threat and positive path distance");
	if(previous.unitId < 0)
		return true;
	if(candidate.cannotEvade != previous.cannotEvade)
		return candidate.cannotEvade;
	const double score = candidate.threat / candidate.pathDistance;
	const double previousScore = previous.threat / previous.pathDistance;
	// Pinned meleeUnitOffense replaces the previous movement target unless
	// its existing priority is strictly greater. Equal scores therefore select
	// the *last visited* eligible unit, not the lowest numeric unit ID.
	// VCMI visits candidates by stable unit ID; upstream enumerates army order.
	return score >= previousScore;
}

/** fheroes2 archerDecision avoids an area shot when projected allied HP
 * damage is at least three times the enemy damage. The caller must account
 * for every stack in the real attack footprint, not only the aimed target.
 */
inline bool catastrophicFriendlyFire(double friendlyDamage, double enemyDamage)
{
	if(!std::isfinite(friendlyDamage) || !std::isfinite(enemyDamage)
		|| friendlyDamage < 0 || enemyDamage < 0)
		throw std::invalid_argument("fheroes2 area shot requires finite nonnegative damage");
	return friendlyDamage >= 3.0 * enemyDamage;
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
// Treat every occupied cell as a possible siege breach. A wide stack's head
// can be outside a wall while its tail is already within the defended area.
// The caller supplies VCMI's authoritative wall geometry, not a local rule.
template<typename Footprint, typename InsideWalls>
inline bool footprintBreachesCastle(const Footprint & occupied, InsideWalls insideWalls)
{
	return std::any_of(occupied.begin(), occupied.end(), insideWalls);
}

// A fortified melee defender may hold only while its *entire* occupied
// footprint is behind the wall. An inside head with an outside tail
// must not be mistaken for a sheltered defender. Reject empty footprints.
template<typename Footprint, typename InsideWalls>
inline bool footprintFullyInsideCastle(const Footprint & occupied, InsideWalls insideWalls)
{
	return occupied.begin() != occupied.end()
		&& std::all_of(occupied.begin(), occupied.end(), insideWalls);
}

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

// Tactical distances between wide units must use their actual occupied cells.
// Both footprints come from the host game; an empty footprint cannot select
// a nearby decoy. The caller supplies the game's authoritative hex metric.
template<typename FirstFootprint, typename SecondFootprint, typename Distance>
inline int nearestFootprintDistance(const FirstFootprint & first, const SecondFootprint & second, Distance distance)
{
	int nearest = std::numeric_limits<int>::max();
	for(const auto & firstCell : first)
		for(const auto & secondCell : second)
			nearest = std::min(nearest, static_cast<int>(distance(firstCell, secondCell)));
	return nearest;
}

/// AI Improvements.txt: avoid chasing a faster nearby decoy when a
/// reachable slower opponent exists, within two movement turns. No source-level TUM scoring is public;
/// this predicate is a conservative, explicitly approximate interpretation.
/// The caller must not suppress legal immediate attacks or ranged shots.
inline bool ignoreNearbyFastDecoyForMovement(int ownSpeed, int enemySpeed, int slowerEnemySpeed, int hexDistance)
{
	if(ownSpeed <= 0 || enemySpeed <= 0 || slowerEnemySpeed <= 0 || hexDistance < 0)
		return false;
	return enemySpeed >= ownSpeed + 2
		&& slowerEnemySpeed <= ownSpeed
		&& slowerEnemySpeed < enemySpeed
		&& static_cast<int64_t>(hexDistance) <= static_cast<int64_t>(ownSpeed) * 2 + 1;
}

/// AI Improvements.txt: Hypnotized troops under AI control prefer attacks
/// against their original allies that maximize damage dealt PLUS received.
/// The actual damage inputs are projections from the host battle engine.
inline double hypnotizedAttackDamageSum(double dealt, double received)
{
	const double total = dealt + received;
	if(!std::isfinite(dealt) || !std::isfinite(received) || !std::isfinite(total)
		|| dealt < 0 || received < 0)
		throw std::invalid_argument("TUM Hypnotize score requires finite nonnegative damage");
	return total;
}

/// AI Improvements.txt: do not repeat an already active creature buff or debuff.
inline bool creatureMayApplyEffect(bool alreadyActive)
{
	return !alreadyActive;
}
}
}
