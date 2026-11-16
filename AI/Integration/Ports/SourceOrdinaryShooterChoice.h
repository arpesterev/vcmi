/*
 * Source: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21,
 * ai.cpp::chooseShooterAction/chooseShooterTarget (CC0-1.0).
 * VCMI adaptation: GPL-2.0-or-later.
 */
#pragma once

#include "HoMM3OrdinaryExchange.h"
#include "HoMM3TargetCensus.h"

namespace AIIntegration::Ports::HoMM3
{
struct SourceShooterCandidate
{
	int32_t rosterIndex = 0;
	bool excluded = false;
	bool incapacitated = false;
	bool cannotAttack = false;
	SourceOrdinaryUnit unit;
	SourceOrdinaryAttack attack;
	std::optional<SourceTargetCensus> enemyCensus;
};

struct SourceShooterChoiceInput
{
	SourceOrdinaryUnit actor;
	int32_t friendlyCombatValue = 0;
	int32_t enemyCombatValue = 0;
	int difficulty = 0;
	bool human = false;
	std::vector<SourceShooterCandidate> enemies;
};

struct SourceShooterChoice
{
	std::optional<int32_t> targetIndex;
	int32_t value = 0;
	bool killsOnly = false;
	bool retriedWithoutKillsOnly = false;
};

/// A live ordinary shot: no move order, expected-damage ledger, or battle-outcome prediction.
/// The producer supplies the complete roster, source-normalized values and enemy target census.
inline std::optional<SourceShooterChoice> chooseSourceOrdinaryShooter(const SourceShooterChoiceInput & input)
{
	const auto health = OrdinaryExchangeDetail::health(input.actor);
	if(!health || *health == 0 || !input.actor.canShoot || input.difficulty < 0 || input.difficulty > 4
		|| input.friendlyCombatValue < 0 || input.friendlyCombatValue > INT32_MAX / 2
		|| input.enemyCombatValue < 0 || input.enemies.size() > 21)
		return std::nullopt;
	for(size_t i = 0; i < input.enemies.size(); ++i)
		if(input.enemies[i].rosterIndex != static_cast<int32_t>(i)) return std::nullopt;

	SourceShooterChoice result;
	result.killsOnly = input.friendlyCombatValue * 2 < input.enemyCombatValue
		&& (input.difficulty > 0 || input.human);
	const auto choose = [&](bool killsOnly) -> bool
	{
		result.targetIndex.reset();
		result.value = 0;
		bool bestIncapacitated = false;
		for(const auto & target : input.enemies)
		{
			if(target.excluded) continue;
			SourceOrdinaryExchangeInput exchange;
			exchange.attacker = input.actor;
			exchange.defender = target.unit;
			exchange.attack[1] = target.attack;
			exchange.ranged = true;
			exchange.killsOnly = killsOnly;
			const auto effect = evaluateSourceOrdinaryExchange(exchange);
			if(!effect) return false;
			int32_t value = effect->value;
			if(input.difficulty != 0 || input.human)
			{
				int divisor = 5;
				if(target.incapacitated) divisor = 10;
				else if(!target.cannotAttack)
				{
					if(!target.enemyCensus) return false;
					if(target.enemyCensus->target && target.enemyCensus->target->attackTime <= 5)
						divisor = target.enemyCensus->target->attackTime;
				}
				if(divisor <= 0) return false;
				value /= divisor;
			}
			if(result.targetIndex && ((target.incapacitated && !bestIncapacitated)
				|| ((target.incapacitated || !bestIncapacitated) && value < result.value)))
				continue;
			result.targetIndex = target.rosterIndex;
			result.value = value;
			bestIncapacitated = target.incapacitated;
		}
		return true;
	};
	if(!choose(result.killsOnly)) return std::nullopt;
	if(result.targetIndex && result.killsOnly && result.value == 0)
	{
		result.retriedWithoutKillsOnly = true;
		if(!choose(false)) return std::nullopt;
	}
	return result;
}
}
