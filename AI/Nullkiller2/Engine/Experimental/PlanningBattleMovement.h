/* GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../../../lib/battle/CBattleInfoCallback.h"
#include "../../../../lib/battle/Unit.h"
#include <tuple>

namespace NK2AI::experimental
{
// Policy v2: movement only. Shooting and immediate melee retain their existing
// ordering. This is a bounded deterministic policy, not optimal tactical play.
inline BattleHex choosePlanningBattleMove(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const ReachabilityInfo & reach,
	const BattleHexArray & available)
{
	const auto start = actor.getPosition();
	BattleHex target = BattleHex::INVALID;
	std::tuple<uint32_t, uint32_t, int> best{ReachabilityInfo::INFINITE_DIST, 0, 0};
	for(const auto * enemy : battle.battleAliveUnits())
	{
		if(enemy->unitSide() == actor.unitSide() || !battle.battleCanAttackUnit(&actor, enemy))
			continue;
		// These are legal attack-origin primary hexes, including double-wide
		// orientation. Never route to an occupied enemy's own primary hex.
		for(const auto hex : enemy->getAttackableHexes(&actor))
		{
			if(!hex.isAvailable() || !reach.isReachable(hex))
				continue;
			const auto candidate = std::make_tuple(reach.distances[hex.toInt()], enemy->unitId(), static_cast<int>(hex.toInt()));
			if(candidate < best)
			{
				best = candidate;
				target = hex;
			}
		}
	}
	if(!target.isAvailable())
		return start;

	// getFlyingReachability does not populate ReachabilityInfo::params.
	// Use the same actor property as the engine, never the default cache flag.
	if(actor.hasBonusOfType(BonusType::FLYING))
	{
		// Flying predecessors point straight to the origin, not a walking
		// chain. Pick only engine-approved landings, with stable hex ties.
		auto result = start;
		for(const auto hex : available)
		{
			const auto candidate = std::make_pair(BattleHex::getDistance(hex, target), hex.toInt());
			const auto current = std::make_pair(BattleHex::getDistance(result, target), result.toInt());
			if(candidate < current)
				result = hex;
		}
		return result;
	}

	// Walk back along the engine's full BFS path until its furthest legal
	// point this activation. Temporary detours may increase geometric range
	// to the enemy, which the old greedy fallback could never choose.
	for(unsigned steps = 0; steps < GameConstants::BFIELD_SIZE; ++steps)
	{
		if(available.contains(target))
			return target;
		const auto previous = reach.predecessors[target.toInt()];
		if(!previous.isAvailable() || reach.distances[previous.toInt()] >= reach.distances[target.toInt()])
			return start;
		target = previous;
	}
	return start;
}
}
