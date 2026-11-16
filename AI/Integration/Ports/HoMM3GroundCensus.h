/* Source-derived ordinary ground rows of combatManager::findAITargets,
 * homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21 (CC0-1.0).
 * VCMI adaptation: GPL-2.0-or-later. No native admission is implied. */
#pragma once
#include "HoMM3GroundPath.h"
#include "HoMM3OrdinaryExchange.h"
#include "HoMM3TargetCensus.h"

namespace AIIntegration::Ports::HoMM3
{
struct SourceGroundCandidate
{
	int32_t rosterIndex = 0;
	int head = -1;
	bool arrowTower = false;
	std::optional<SourceOrdinaryUnit> unit;
	std::optional<SourceOrdinaryAttack> attack, retaliation;
	std::optional<bool> canRetaliate;
};
struct SourceGroundCensusInput
{
	SourceOrdinaryUnit actor;
	std::optional<SourceGroundSearchInput> geometry;
	bool immobilized = false, siegeWeapon = false, ballista = false;
	int32_t stoneGazeTurns = 0, blindTurns = 0, hypnotizeTurns = 0, berserkTurns = 0;
	bool currentArmyExcluded = false, meleeOnly = false, simulated = false, killsOnly = false;
	std::optional<bool> retaliationPolicy;
	// Complete original enemy roster, including retained ordinary dead heads.
	std::optional<std::vector<SourceGroundCandidate>> enemies;
};
struct SourceGroundRow
{
	SourceTargetCensus census;
	std::vector<SourceTargetCandidate> candidates;
	// Source passes zero to damage when arrival exceeds current speed. Ordinary
	// damage has no distance consumer; this records the contract, not Joust support.
	std::vector<int32_t> damageDistances;
};
inline std::optional<SourceGroundRow> produceSourceGroundCensus(const SourceGroundCensusInput & input)
{
	if(input.stoneGazeTurns < 0 || input.blindTurns < 0 || input.hypnotizeTurns < 0 || input.berserkTurns < 0)
		return std::nullopt;
	if(input.immobilized || (input.siegeWeapon && !input.ballista) || input.stoneGazeTurns > 1 || input.blindTurns > 1
		|| input.hypnotizeTurns || input.berserkTurns || input.currentArmyExcluded)
		return SourceGroundRow{};
	if(input.ballista || input.meleeOnly || input.simulated || input.actor.canShoot || !input.geometry || !input.enemies)
		return std::nullopt;
	const auto health = OrdinaryExchangeDetail::health(input.actor);
	if(!health || *health <= 0) return std::nullopt;
	const auto path = searchSourceGround(*input.geometry);
	if(!path) return std::nullopt;
	const int enemySide = 1 - input.geometry->actor.side;
	int32_t previous = -1;
	// Validate all identities before filtering unreachable heads. A missing enemy
	// is not permission to call its occupied cell an irrelevant static blocker.
	for(const auto & peer : *input.enemies)
	{
		if(peer.rosterIndex != ++previous || peer.rosterIndex > 30
			|| !GroundPathDetail::available(peer.head)) return std::nullopt;
		if(peer.arrowTower) return std::nullopt; // An open-field map cannot contain siege towers.
		if(peer.unit)
		{
			const auto peerHealth = OrdinaryExchangeDetail::health(*peer.unit);
			if(!peerHealth) return std::nullopt;
			const auto occupant = input.geometry->map[peer.head].occupant;
			const SourceGroundIdentity identity{enemySide, peer.rosterIndex};
			if((*peerHealth > 0 && occupant != identity) || (*peerHealth == 0 && occupant == identity))
				return std::nullopt;
		}
	}
	for(int hex = 0; hex < 187; ++hex)
	{
		const auto occupant = input.geometry->map[hex].occupant;
		if(occupant && occupant->side == enemySide
			&& (occupant->index >= static_cast<int>(input.enemies->size())
				|| (*input.enemies)[occupant->index].head != hex)) return std::nullopt;
	}
	SourceGroundRow row;
	for(const auto & peer : *input.enemies)
	{
		if(!path->validMove[peer.head]) continue;
		const int32_t distance = path->cost[peer.head];
		if(input.geometry->speed == 0 && distance > 0) continue;
		if(!peer.unit || !peer.attack || !peer.canRetaliate || !input.retaliationPolicy)
			return std::nullopt;
		SourceOrdinaryExchangeInput exchange;
		exchange.attacker = input.actor;
		exchange.defender = *peer.unit;
		exchange.attack[0] = *peer.attack;
		exchange.canRetaliate = *peer.canRetaliate;
		exchange.retaliationPolicy = *input.retaliationPolicy;
		if(exchange.canRetaliate && exchange.retaliationPolicy)
		{
			if(!peer.retaliation) return std::nullopt;
			exchange.retaliation = *peer.retaliation;
		}
		exchange.killsOnly = input.killsOnly;
		const auto effect = evaluateSourceOrdinaryExchange(exchange);
		if(!effect) return std::nullopt;
		row.candidates.push_back({peer.rosterIndex, effect->value, distance});
		row.damageDistances.push_back(distance > input.geometry->speed ? 0 : distance);
	}
	const auto census = reduceSourceTargetCensus(input.geometry->speed, false, row.candidates);
	if(!census) return std::nullopt;
	row.census = *census;
	return row;
}
}
