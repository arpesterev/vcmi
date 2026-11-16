/*
 * Source-derived ordinary ranged rows of combatManager::findAITargets,
 * sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21 (CC0-1.0).
 * VCMI adaptation: GPL-2.0-or-later.
 */
#pragma once
#include "HoMM3OrdinaryExchange.h"
#include "HoMM3TargetCensus.h"

namespace AIIntegration::Ports::HoMM3
{
struct SourceRangedCandidate
{
	int32_t rosterIndex = 0;
	bool arrowTower = false;
	std::optional<SourceOrdinaryUnit> unit;
	std::optional<SourceOrdinaryAttack> attack;
	int32_t expectedDamage = 0;
};

struct SourceRangedCensusInput
{
	SourceOrdinaryUnit actor;
	int32_t speed = 0;
	bool immobilized = false, siegeWeapon = false, ballista = false;
	int32_t stoneGazeTurns = 0, blindTurns = 0, hypnotizeTurns = 0, berserkTurns = 0;
	bool currentArmyExcluded = false, meleeOnly = false, simulated = false, killsOnly = false;
	int32_t expectedDamage = 0;
	// Complete original roster order. Unavailable does not mean an empty roster.
	std::optional<std::vector<SourceRangedCandidate>> enemies;
};

inline std::optional<SourceTargetCensus> produceSourceRangedCensus(const SourceRangedCensusInput & input)
{
	if(input.speed < 0 || input.expectedDamage < 0 || input.stoneGazeTurns < 0 || input.blindTurns < 0
		|| input.hypnotizeTurns < 0 || input.berserkTurns < 0)
		return std::nullopt;
	// findAITargets clears the row before any exclusion. A filtered actor has
	// a KNOWN empty row even when there is no exchange producer for its peers.
	if(input.immobilized || (input.siegeWeapon && !input.ballista)
		|| input.stoneGazeTurns > 1 || input.blindTurns > 1 || input.hypnotizeTurns != 0
		|| input.berserkTurns != 0 || input.currentArmyExcluded)
		return SourceTargetCensus{};
	const auto actorHealth = OrdinaryExchangeDetail::health(input.actor);
	if(!actorHealth) return std::nullopt;
	if(input.simulated && *actorHealth <= input.expectedDamage)
		return SourceTargetCensus{};
	if(!input.actor.canShoot)
		return std::nullopt; // A ground path producer is required, not an empty row.
	if(input.meleeOnly)
		return SourceTargetCensus{};
	// Ballista shooting is source-special; this ordinary exchange producer cannot price it.
	if(input.ballista || !input.enemies)
		return std::nullopt;
	std::vector<SourceTargetCandidate> candidates;
	int32_t previousIndex = -1;
	for(const auto & enemy : *input.enemies)
	{
		if(enemy.rosterIndex != previousIndex + 1 || enemy.rosterIndex > 30 || enemy.expectedDamage < 0)
			return std::nullopt;
		previousIndex = enemy.rosterIndex;
		if(enemy.arrowTower) continue;
		if(!enemy.unit) return std::nullopt;
		const auto health = OrdinaryExchangeDetail::health(*enemy.unit);
		if(!health) return std::nullopt;
		if(input.simulated && *health <= enemy.expectedDamage) continue;
		if(!enemy.attack) return std::nullopt;
		SourceOrdinaryExchangeInput exchange;
		exchange.attacker = input.actor;
		exchange.defender = *enemy.unit;
		exchange.attack[1] = *enemy.attack;
		exchange.ranged = true;
		exchange.killsOnly = input.killsOnly;
		exchange.simulated = input.simulated;
		exchange.attackerExpectedDamage = input.expectedDamage;
		exchange.defenderExpectedDamage = enemy.expectedDamage;
		const auto effect = evaluateSourceOrdinaryExchange(exchange);
		if(!effect) return std::nullopt;
		candidates.push_back({enemy.rosterIndex, effect->value, 1});
	}
	return reduceSourceTargetCensus(input.speed, true, candidates);
}

// A known witness is sufficient for the existential commitment question.
// A negative answer requires every row; unknown rows never become false.
inline std::optional<bool> sourceTargetCommitted(int32_t target,
	const std::vector<std::optional<SourceTargetCensus>> & rows)
{
	if(target < 0 || target > 30) return std::nullopt;
	bool unknown = false;
	for(const auto & row : rows)
	{
		if(!row) unknown = true;
		else if(row->target && row->target->rosterIndex == target) return true;
	}
	return unknown ? std::nullopt : std::optional<bool>(false);
}
}
