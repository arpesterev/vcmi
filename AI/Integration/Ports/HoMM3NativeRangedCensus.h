/*
 * Native facts for source findAITargets ordinary ranged rows.
 * Source: sushi-shi/homm3-decomp e12906accf96c2662b1abadbc49ddc111991bf21.
 * License: GPL-2.0-or-later; reconstructed policy source CC0-1.0.
 */
#pragma once
#include "HoMM3NativeExchangeFacts.h"
#include "HoMM3RangedCensus.h"
#include <map>

namespace AIIntegration::Ports::HoMM3
{
struct NativeRangedCensus
{
	std::vector<uint32_t> friendlyIds, enemyIds;
	std::vector<std::optional<SourceTargetCensus>> rows;
	std::optional<bool> committed(uint32_t id) const
	{
		const auto found = std::find(enemyIds.begin(), enemyIds.end(), id);
		if(found == enemyIds.end()) return std::nullopt;
		return sourceTargetCommitted(static_cast<int32_t>(found - enemyIds.begin()), rows);
	}
};

/// Unsupported roster identity or shared normalization invalidates the snapshot;
/// unsupported individual exchanges invalidate their row, not its peers.
inline std::optional<NativeRangedCensus> captureNativeRangedCensus(
	const CPlayerBattleCallback & callback, BattleSide side, bool killsOnly = false, const CurrentDefendSnapshot * witness = nullptr)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;
	if(callback.battleGetDefendedTown()) return std::nullopt;
	auto roster = callback.battleGetUnitsIf([](const battle::Unit *) { return true; });
	std::sort(roster.begin(), roster.end(), [](const auto * lhs, const auto * rhs)
	{
		if(lhs->unitSide() != rhs->unitSide()) return lhs->unitSide() < rhs->unitSide();
		return lhs->unitSlot() < rhs->unitSlot();
	});
	std::array<int, 2> previousSlot{-1, -1};
	int32_t lowestAttack = INT32_MAX, lowestDefense = INT32_MAX;
	std::vector<const battle::Unit *> friendly, enemy;
	NativeRangedCensus result;
	for(const auto * unit : roster)
	{
		if((unit->unitSide() != BattleSide::ATTACKER && unit->unitSide() != BattleSide::DEFENDER)
			|| !unit->unitSlot().validSlot() || unit->isClone() || unit->isSummoned()
			|| unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT)
			|| unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
			|| unit->controllingSide() != unit->unitSide())
			return std::nullopt;
		const auto index = unit->unitSide() == BattleSide::ATTACKER ? 0u : 1u;
		if(previousSlot[index] == unit->unitSlot().getNum()) return std::nullopt;
		previousSlot[index] = unit->unitSlot().getNum();
		auto & units = unit->unitSide() == side ? friendly : enemy;
		auto & ids = unit->unitSide() == side ? result.friendlyIds : result.enemyIds;
		units.push_back(unit);
		ids.push_back(unit->unitId());
		if(units.size() > 31) return std::nullopt;
		if(!unit->alive()) continue;
		const auto modifiers = NativeExchangeFacts::modifiers(callback, *unit, witness);
		if(!modifiers) return std::nullopt;
		lowestAttack = std::min(lowestAttack, modifiers->first);
		lowestDefense = std::min(lowestDefense, modifiers->second);
	}
	if(lowestAttack == INT32_MAX || lowestDefense == INT32_MAX) return std::nullopt;
	std::map<uint32_t, std::optional<SourceOrdinaryUnit>> units;
	for(const auto * unit : roster)
		units.emplace(unit->unitId(), NativeExchangeFacts::unit(callback, *unit, lowestAttack, lowestDefense, witness));
	for(const auto * actor : friendly)
	{
		SourceRangedCensusInput row;
		row.killsOnly = killsOnly;
		if(!actor->alive())
		{
			row.immobilized = true;
			result.rows.push_back(produceSourceRangedCensus(row));
			continue;
		}
		const auto & actorFacts = units.at(actor->unitId());
		const int64_t speed = actor->getInitiative(0);
		if(!actorFacts || speed < 0 || speed > INT32_MAX)
		{
			result.rows.push_back(std::nullopt);
			continue;
		}
		row.actor = *actorFacts;
		row.speed = static_cast<int32_t>(speed);
		row.enemies.emplace();
		for(size_t i = 0; i < enemy.size(); ++i)
		{
			const auto * target = enemy[i];
			SourceRangedCandidate candidate;
			candidate.rosterIndex = static_cast<int32_t>(i);
			candidate.unit = units.at(target->unitId());
			candidate.attack = NativeExchangeFacts::rangedAttack(callback, *actor, *target, witness);
			row.enemies->push_back(std::move(candidate));
		}
		result.rows.push_back(produceSourceRangedCensus(row));
	}
	return result;
}

/// Sufficient source isLastAction proof without using the native threat heuristic.
inline bool nativeSourceLastAction(const CPlayerBattleCallback & callback, const battle::Unit & active)
{
	for(const auto * unit : callback.battleGetUnitsIf([](const battle::Unit *) { return true; }))
	{
		if(unit->unitSide() != active.unitSide() || !unit->alive() || unit->unitId() == active.unitId()
			|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON) || unit->moved() || unit->defended())
			continue;
		return false;
	}
	return true;
}
}
