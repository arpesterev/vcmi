// SPDX-License-Identifier: CC0-1.0
#pragma once
#include "OwnershipPrefix.h"
#include <map>
#include <array>
#include <string>

namespace HoMM3Forecast
{
struct SurvivorFacts
{
	std::int64_t maximumHealth;
	std::int64_t topDamage;
	bool siegeWeapon;
};
struct Shot
{
	UnitId actor;
	UnitId target;
	std::int64_t damage;
};
struct MeleeExchange
{
	std::int64_t targetDamage;
	std::int64_t actorDamage;
};
struct Melee
{
	UnitId actor;
	UnitId target;
	MeleeExchange damage;
};
struct PrefixOutcome
{
	std::optional<Side> side;
	std::map<UnitId, ForecastUnit> initial;
	std::map<UnitId, SurvivorFacts> survivorFacts;
	bool winLikely = false;
	bool spellsNotRequired = false;
	std::optional<UnitId> opposingBoundary;
	std::map<UnitId, std::int64_t> health;
	std::vector<Shot> shots;
	std::vector<Melee> melees;
};
struct PrefixProjection
{
	std::optional<PrefixOutcome> outcome;
	std::string unavailable;
};

/// Recover capped expected damage only from the matching ordinary ownership prefix.
inline std::optional<std::int64_t> winningExpectedDamage(const PrefixProjection & forecast,
	Side side, UnitId id, std::int64_t liveHealth, std::int64_t maximumHealth, std::int64_t topDamage)
{
	if(!forecast.outcome || !forecast.outcome->winLikely || forecast.outcome->side != side)
		return std::nullopt;
	const auto & outcome = *forecast.outcome;
	const auto initial = outcome.initial.find(id);
	const auto health = outcome.health.find(id);
	const auto facts = outcome.survivorFacts.find(id);
	if(initial == outcome.initial.end() || health == outcome.health.end() || facts == outcome.survivorFacts.end())
		return std::nullopt;
	const auto & unit = initial->second;
	if(unit.order.owningSide != side || unit.controllingSide != side || unit.berserk
		|| unit.order.role != OrderRole::ORDINARY || unit.order.immobilized
		|| liveHealth <= 0 || liveHealth > std::numeric_limits<std::int32_t>::max()
		|| unit.liveHealth != liveHealth || health->second < 0 || health->second > liveHealth
		|| maximumHealth <= 0 || maximumHealth > std::numeric_limits<std::int32_t>::max() || topDamage < 0 || topDamage >= maximumHealth
		|| facts->second.siegeWeapon || facts->second.maximumHealth != maximumHealth || facts->second.topDamage != topDamage)
		return std::nullopt;
	const auto expectedDamage = liveHealth - health->second;
	// Source total HP multiplies the unwounded count before subtracting wounds.
	if(liveHealth > std::numeric_limits<std::int32_t>::max() - topDamage)
		return std::nullopt;
	return expectedDamage;
}

/// Source survivor/casualty scans after a bounded native-damage ownership prefix.
/// Model supplies native attack legality and damage estimates, never expected-damage totals.
template<class Model>
PrefixProjection projectShooterPrefix(const ForecastInput & input, Side side, Model & model)
{
	OwnershipPrefix prefix(input, side);
	if(prefix.moveOrder().containsComparatorTies)
		return {std::nullopt, "source comparator tie"};
	PrefixOutcome result;
	result.side = side;
	while(const auto actor = prefix.next())
	{
		std::optional<UnitId> target;
		std::size_t preferredCount = 0;
		bool preferredIncapacitated = true;
		for(const auto & unit : input.units)
		{
			if(unit.order.owningSide == side || unit.order.immobilized
				|| unit.order.role == OrderRole::ARROW_TOWER || prefix.projectedHealth(unit.order.id) == 0)
				continue;
			const bool incapacitated = unit.order.sourceSpell62Turns != 0
				|| unit.order.sourceSpell70Turns != 0 || unit.order.sourceSpell74Turns != 0;
			// chooseShooterTarget always prefers active enemies, regardless of attack value.
			if(target && !preferredIncapacitated && incapacitated) continue;
			if(!target || (preferredIncapacitated && !incapacitated))
			{
				preferredCount = 0;
				preferredIncapacitated = incapacitated;
			}
			++preferredCount;
			target = unit.order.id;
		}
		if(!target) continue;
		if(preferredCount != 1)
		{
			if(!preferredIncapacitated)
				return {std::nullopt, "multiple active shooter targets require source target-time graph"};
			if constexpr(requires { model.peerLosses(*actor, *target, std::int64_t{}, std::int64_t{}); })
			{
				std::array<std::optional<UnitId>, 4> winners;
				std::array<std::int32_t, 4> values{};
				std::array<std::int32_t, 4> indices{};
				for(const auto & unit : input.units)
				{
					if(unit.order.owningSide == side || unit.order.immobilized
						|| unit.order.role == OrderRole::ARROW_TOWER || prefix.projectedHealth(unit.order.id) == 0)
						continue;
					if(!model.canShoot(*actor, unit.order.id))
						return {std::nullopt, "melee or blocked shooting requires source action selection"};
					const auto losses = model.peerLosses(*actor, unit.order.id,
						prefix.projectedHealth(*actor), prefix.projectedHealth(unit.order.id));
					if(!losses) return {std::nullopt, "unsupported source peer combat value"};
					for(std::size_t mode = 0; mode < winners.size(); ++mode)
					{
						const auto value = (*losses)[mode / 2] / (mode % 2 ? 10 : 1);
						if(value < 0) return {std::nullopt, "negative source peer combat value"};
						if(!winners[mode] || value > values[mode]
							|| (value == values[mode] && unit.order.sourceIndex > indices[mode]))
						{
							winners[mode] = unit.order.id;
							values[mode] = value;
							indices[mode] = unit.order.sourceIndex;
						}
					}
				}
				// Unknown difficulty and constructor killsOnly must not affect the winner.
				if(!std::all_of(winners.begin(), winners.end(), [&](const auto & winner) { return winner == winners[0]; }))
					return {std::nullopt, "source peer winner depends on difficulty or kills-only mode"};
				target = winners[0];
			}
			else return {std::nullopt, "multiple preferred shooter targets require source tactical ranking"};
		}
		if(!model.canShoot(*actor, *target))
		{
			if constexpr(requires { model.meleeExchange(input, *actor, *target, std::int64_t{}, std::int64_t{}); })
			{
				const auto exchange = model.meleeExchange(input, *actor, *target,
					prefix.projectedHealth(*actor), prefix.projectedHealth(*target));
				if(!exchange || exchange->actorDamage < 0 || exchange->targetDamage < 0
					|| exchange->actorDamage > prefix.projectedHealth(*actor)
					|| exchange->targetDamage > prefix.projectedHealth(*target))
					return {std::nullopt, "unsupported source melee action or exchange"};
				prefix.addExpectedDamage(*target, exchange->targetDamage);
				prefix.addExpectedDamage(*actor, exchange->actorDamage);
				result.melees.push_back({*actor, *target, *exchange});
				continue;
			}
			return {std::nullopt, "melee or blocked shooting requires source action selection"};
		}
		const auto damage = model.shotDamage(*actor, *target, prefix.projectedHealth(*actor), prefix.projectedHealth(*target));
		if(!damage || *damage < 0 || *damage > std::numeric_limits<std::int32_t>::max())
			return {std::nullopt, "unsupported native damage estimate"};
		const auto victim = std::find_if(input.units.begin(), input.units.end(), [&](const ForecastUnit & unit)
		{
			return unit.order.id == *target;
		});
		const auto accumulated = victim->liveHealth - prefix.projectedHealth(*target);
		if(*damage > std::numeric_limits<std::int32_t>::max() - accumulated)
			return {std::nullopt, "source expected-damage addition would overflow"};
		prefix.addExpectedDamage(*target, *damage);
		result.shots.push_back({*actor, *target, *damage});
	}
	result.winLikely = true;
	result.spellsNotRequired = true;
	for(const auto & unit : input.units)
	{
		const auto health = prefix.projectedHealth(unit.order.id);
		result.health.emplace(unit.order.id, health);
		const auto facts = model.survivorFacts(unit.order.id);
		result.initial.emplace(unit.order.id, unit);
		result.survivorFacts.emplace(unit.order.id, facts);
		if(facts.maximumHealth <= 0 || facts.topDamage < 0 || facts.topDamage >= facts.maximumHealth)
			return {std::nullopt, "invalid survivor health facts"};
		if(unit.order.immobilized) continue;
		if(unit.order.owningSide != side)
		{
			if(health > 0 && !facts.siegeWeapon) result.winLikely = false;
		}
		else if(unit.order.role != OrderRole::ARROW_TOWER
			&& unit.liveHealth - health + facts.topDamage >= facts.maximumHealth)
			result.spellsNotRequired = false;
	}
	result.spellsNotRequired = result.spellsNotRequired && result.winLikely;
	result.opposingBoundary = prefix.opposingBoundary();
	return {std::move(result), {}};
}
}
